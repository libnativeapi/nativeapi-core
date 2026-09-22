#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <mutex>
#include <unordered_map>
#include "../../foundation/id_allocator.h"
#include "../../window.h"
#include "../../window_shape.h"
#include "../../window_manager.h"
#include "../../window_registry.h"

// Import GTK headers
#include <gdk/gdk.h>
#include <dlfcn.h>
#include <functional>
#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#undef None  // Xlib macro conflicts with VisualEffect::None.
#endif

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

// Native shadow rendering is independent of Flutter and other content renderers.
#include "window_shadow_linux.h"

namespace nativeapi {

// Key to store/retrieve WindowId on GObjects
static const char* kWindowIdKey = "NativeAPIWindowId";
static const char* kInputShapeKey = "NativeAPIInputShape";
static const char* kTitleBarStyleKey = "NativeAPITitleBarStyle";

// The window manager draws a title bar and border around the client area, and the
// public geometry is the frame (window.h): positions are the frame's top-left corner
// in root coordinates and sizes include the decorations. GDK measures both, but only
// the GtkWindow API positions a toplevel: gdk_window_move() bypasses GTK's own
// bookkeeping, so the position is lost when the window is mapped and the window
// manager places the window wherever it likes instead.
struct Decorations {
  gint left = 0;
  gint top = 0;
  gint right = 0;
  gint bottom = 0;
};

// Where the frame and the content of a toplevel are, in root coordinates.
struct Layout {
  GdkRectangle frame = {};    // what the user sees as the window: title bar and content
  GdkRectangle content = {};  // the client area the application draws into
};

// A window the window manager decorates is simple: its GdkWindow is the content and
// gdk_window_get_frame_extents() is the frame. A window with client-side decorations is
// not — a header bar set as the titlebar (Flutter's runner does that on GNOME), and
// every GTK toplevel on Wayland. Its GdkWindow also holds the title bar and an
// invisible margin for the shadow, the window manager adds nothing, and both GDK
// answers describe that whole surface: 52 x 99 more than the content on GNOME 46.
// GTK's own window API already speaks in content sizes and shadowless positions
// (gtk_window_resize, gtk_window_move); this makes the measured side agree with it,
// from where GTK allocated the window's child and its title bar.
static GtkWidget* FindHeaderBar(GtkWidget* widget);

static Layout GetLayout(GtkWidget* widget, GdkWindow* gdk_window) {
  Layout layout;
  // An unmapped window has no frame yet, and GDK answers with an estimate that is not
  // one: taking it for decorations would misplace everything measured against it.
  if (!gdk_window || !gdk_window_is_viewable(gdk_window)) {
    return layout;
  }
  gint origin_x = 0;
  gint origin_y = 0;
  gdk_window_get_origin(gdk_window, &origin_x, &origin_y);
  layout.content = {origin_x, origin_y, gdk_window_get_width(gdk_window),
                    gdk_window_get_height(gdk_window)};
  gdk_window_get_frame_extents(gdk_window, &layout.frame);

  if (!widget || !GTK_IS_WINDOW(widget)) {
    return layout;
  }
  GtkWidget* child = gtk_bin_get_child(GTK_BIN(widget));
  GtkAllocation child_allocation = {};
  gint child_x = 0;
  gint child_y = 0;
  if (!child) {
    // A window nobody put content into - one this library created - has no child to
    // measure. GTK's own idea of the window's size leaves client-side decorations out,
    // which tells the content's size; where it sits inside the surface is not public,
    // so the shadow is taken to be as wide above as below.
    gint width = 0;
    gint height = 0;
    gtk_window_get_size(GTK_WINDOW(widget), &width, &height);
    if (width <= 1 || height <= 1 ||
        (width >= layout.content.width && height >= layout.content.height)) {
      return layout;
    }
    gint title_height = 0;
    GtkWidget* header_bar = FindHeaderBar(widget);
    if (header_bar && gtk_widget_get_mapped(header_bar)) {
      title_height = gtk_widget_get_allocated_height(header_bar);
    }
    const gint side = (layout.content.width - width) / 2;
    const gint shadow_top = (layout.content.height - height - title_height) / 2;
    if (side < 0 || shadow_top < 0) {
      return layout;
    }
    layout.frame = {origin_x + side, origin_y + shadow_top, width, height + title_height};
    layout.content = {origin_x + side, origin_y + shadow_top + title_height, width, height};
#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(gdk_window_get_display(gdk_window))) {
      layout.content.x -= layout.frame.x;
      layout.content.y -= layout.frame.y;
      layout.frame.x = 0;
      layout.frame.y = 0;
    }
#endif
    return layout;
  }
  if (!gtk_widget_get_mapped(child) ||
      !gtk_widget_translate_coordinates(child, widget, 0, 0, &child_x, &child_y)) {
    return layout;
  }
  gtk_widget_get_allocation(child, &child_allocation);
  const bool client_side = child_x > 0 || child_y > 0 ||
                           child_allocation.width < layout.content.width ||
                           child_allocation.height < layout.content.height;
  if (!client_side || child_allocation.width <= 1 || child_allocation.height <= 1) {
    return layout;
  }

  layout.content = {origin_x + child_x, origin_y + child_y, child_allocation.width,
                    child_allocation.height};
  // The frame is the content plus the title bar above it; the shadow is not part of it.
  gint frame_top = child_y;
  GtkWidget* titlebar = gtk_window_get_titlebar(GTK_WINDOW(widget));
  gint titlebar_x = 0;
  gint titlebar_y = 0;
  if (titlebar && gtk_widget_get_mapped(titlebar) &&
      gtk_widget_translate_coordinates(titlebar, widget, 0, 0, &titlebar_x, &titlebar_y) &&
      titlebar_y < frame_top) {
    frame_top = titlebar_y;
  }
  layout.frame = {origin_x + child_x, origin_y + frame_top, child_allocation.width,
                  child_allocation.height + (child_y - frame_top)};
#ifdef GDK_WINDOWING_WAYLAND
  // A Wayland client is not told where it is: its "origin" is the corner of its own
  // surface, shadow included. Report the frame at 0,0 as before, not at the shadow's
  // width — a position that looks real and is not.
  if (GDK_IS_WAYLAND_DISPLAY(gdk_window_get_display(gdk_window))) {
    layout.content.x -= layout.frame.x;
    layout.content.y -= layout.frame.y;
    layout.frame.x = 0;
    layout.frame.y = 0;
  }
#endif
  return layout;
}

// Zero until the window is mapped, and whatever is known on Wayland, where a client
// is not told where it is but still knows its own title bar.
static Decorations GetDecorations(GtkWidget* widget, GdkWindow* gdk_window) {
  const Layout layout = GetLayout(widget, gdk_window);
  Decorations decorations;
  decorations.left = layout.content.x - layout.frame.x;
  decorations.top = layout.content.y - layout.frame.y;
  decorations.right = layout.frame.width - layout.content.width - decorations.left;
  decorations.bottom = layout.frame.height - layout.content.height - decorations.top;
  // Before the window is mapped the answers do not agree yet; nothing is known about
  // the decorations then, rather than a negative thickness.
  if (decorations.left < 0 || decorations.top < 0 || decorations.right < 0 ||
      decorations.bottom < 0) {
    return Decorations{};
  }
  return decorations;
}

// Helper function to find header bar in widget hierarchy
static GtkWidget* FindHeaderBar(GtkWidget* widget) {
  if (!widget)
    return nullptr;

  // Check if this widget is a header bar
  if (GTK_IS_HEADER_BAR(widget))
    return widget;

  // If it's a container, search children. gtk_container_forall(), not
  // gtk_container_get_children(): the title bar GTK gives a window with client-side
  // decorations - every toplevel on Wayland, Flutter's windows among them - is an
  // internal child, which the latter leaves out.
  if (GTK_IS_CONTAINER(widget)) {
    GList* children = nullptr;
    gtk_container_forall(
        GTK_CONTAINER(widget),
        [](GtkWidget* child, gpointer data) {
          GList** list = static_cast<GList**>(data);
          *list = g_list_append(*list, child);
        },
        &children);
    for (GList* l = children; l != nullptr; l = l->next) {
      GtkWidget* child = GTK_WIDGET(l->data);
      GtkWidget* result = FindHeaderBar(child);
      if (result) {
        g_list_free(children);
        return result;
      }
    }
    g_list_free(children);
  }

  return nullptr;
}

// Requested WM capabilities belong to the native window, not a temporary wrapper.
// X11 WMs may ignore the Motif hints; Wayland has no equivalent capabilities mask.
static const char* kWindowControlsKey = "NativeAPIWindowControls";
static const char* kHeaderLayoutKey = "NativeAPIWindowControlsLayout";
struct WindowControls {
  bool movable = true;
  bool minimizable = true;
  bool maximizable = true;
  bool closable = true;  // Used only for a bare GdkWindow.
  bool pending_maximize = false;
  bool pending_minimize = false;
};
struct HeaderLayout {
  explicit HeaderLayout(const gchar* value) : original(g_strdup(value)) {}
  ~HeaderLayout() { g_free(original); }
  gchar* original;
};

static void ApplyWindowControls(GtkWidget* widget,
                                GdkWindow* surface,
                                const WindowControls& controls) {
  const bool gtk_window = widget && GTK_IS_WINDOW(widget);
  if (gtk_window)
    surface = gtk_widget_get_window(widget);
  const auto state = surface ? gdk_window_get_state(surface) : static_cast<GdkWindowState>(0);
  const bool closable =
      gtk_window ? gtk_window_get_deletable(GTK_WINDOW(widget)) : controls.closable;
  int functions = 0;
  if (!gtk_window || gtk_window_get_resizable(GTK_WINDOW(widget)))
    functions |= GDK_FUNC_RESIZE;
  if (controls.movable)
    functions |= GDK_FUNC_MOVE;
  if (controls.minimizable || controls.pending_minimize || (state & GDK_WINDOW_STATE_ICONIFIED))
    functions |= GDK_FUNC_MINIMIZE;
  if (controls.maximizable || controls.pending_maximize || (state & GDK_WINDOW_STATE_MAXIMIZED))
    functions |= GDK_FUNC_MAXIMIZE;
  if (closable)
    functions |= GDK_FUNC_CLOSE;
  if (surface)
    gdk_window_set_functions(surface, static_cast<GdkWMFunction>(functions));

  // GTK uses decoration-layout for both explicit and automatically created CSD
  // header bars. Filter the original layout, retaining button order and sides.
  GtkWidget* header = nullptr;
  if (gtk_window) {
    struct Search {
      GtkWidget* content;
      GtkWidget* header = nullptr;
    } search{gtk_bin_get_child(GTK_BIN(widget))};
    // The automatic GTK title bar is an internal child, not get_titlebar().
    // Exclude application content: it may contain unrelated GtkHeaderBars.
    gtk_container_forall(GTK_CONTAINER(widget), +[](GtkWidget* child, gpointer data) {
      auto* search = static_cast<Search*>(data);
      if (child != search->content && !search->header) search->header = FindHeaderBar(child);
    }, &search);
    header = search.header;
  }
  if (!header)
    return;
  auto* original =
      static_cast<HeaderLayout*>(g_object_get_data(G_OBJECT(header), kHeaderLayoutKey));
  if (!original && controls.minimizable && controls.maximizable)
    return;
  if (!original) {
    original = new HeaderLayout(gtk_header_bar_get_decoration_layout(GTK_HEADER_BAR(header)));
    g_object_set_data_full(
        G_OBJECT(header), kHeaderLayoutKey, original,
        +[](gpointer data) { delete static_cast<HeaderLayout*>(data); });
  }
  gchar* settings_layout = nullptr;
  const gchar* layout = original->original;
  const bool filtered = !controls.minimizable || !controls.maximizable;
  if (filtered && !layout) {
    g_object_get(gtk_widget_get_settings(widget), "gtk-decoration-layout", &settings_layout,
                 nullptr);
    layout = settings_layout;
  }
  std::string result;
  if (filtered && layout) {
    gchar** sides = g_strsplit(layout, ":", 2);
    for (int side = 0; sides[side]; ++side) {
      if (side)
        result += ':';
      gchar** buttons = g_strsplit(sides[side], ",", -1);
      bool first = true;
      for (int i = 0; buttons[i]; ++i) {
        if ((!controls.minimizable && g_str_equal(buttons[i], "minimize")) ||
            (!controls.maximizable && g_str_equal(buttons[i], "maximize")))
          continue;
        if (!first)
          result += ',';
        result += buttons[i];
        first = false;
      }
      g_strfreev(buttons);
    }
    g_strfreev(sides);
  }
  const gchar* desired = filtered ? result.c_str() : original->original;
  if (g_strcmp0(gtk_header_bar_get_decoration_layout(GTK_HEADER_BAR(header)), desired) != 0) {
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header), desired);
  }
  g_free(settings_layout);
}

static WindowControls* GetWindowControls(GtkWidget* widget, GdkWindow* surface, bool create) {
  GObject* object = widget ? G_OBJECT(widget) : surface ? G_OBJECT(surface) : nullptr;
  if (!object)
    return nullptr;
  auto* controls = static_cast<WindowControls*>(g_object_get_data(object, kWindowControlsKey));
  if (!controls && create) {
    controls = new WindowControls;
    g_object_set_data_full(
        object, kWindowControlsKey, controls,
        +[](gpointer data) { delete static_cast<WindowControls*>(data); });
    if (widget && GTK_IS_WINDOW(widget)) {
      auto update = +[](GtkWidget* widget, gpointer) {
        auto* state =
            static_cast<WindowControls*>(g_object_get_data(G_OBJECT(widget), kWindowControlsKey));
        ApplyWindowControls(widget, gtk_widget_get_window(widget), *state);
      };
      g_signal_connect_after(widget, "map", G_CALLBACK(update), nullptr);
      g_signal_connect_after(widget, "size-allocate",
                             G_CALLBACK(+[](GtkWidget* widget, GtkAllocation*, gpointer) {
                               auto* state = static_cast<WindowControls*>(
                                   g_object_get_data(G_OBJECT(widget), kWindowControlsKey));
                               ApplyWindowControls(widget, gtk_widget_get_window(widget), *state);
                             }),
                             nullptr);
      g_signal_connect_after(
          widget, "window-state-event",
          G_CALLBACK(+[](GtkWidget* widget, GdkEventWindowState* event, gpointer) -> gboolean {
            auto* state = static_cast<WindowControls*>(
                g_object_get_data(G_OBJECT(widget), kWindowControlsKey));
            if (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED)
              state->pending_maximize = false;
            if (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED)
              state->pending_minimize = false;
            ApplyWindowControls(widget, gtk_widget_get_window(widget), *state);
            return FALSE;
          }),
          nullptr);
      // set_deletable() rewrites the entire GDK functions mask, so reapply the
      // combined state after GTK changes either native property.
      auto notify = +[](GObject* object, GParamSpec*, gpointer) {
        auto* widget = GTK_WIDGET(object);
        auto* state = static_cast<WindowControls*>(g_object_get_data(object, kWindowControlsKey));
        ApplyWindowControls(widget, gtk_widget_get_window(widget), *state);
      };
      g_signal_connect_after(widget, "notify::deletable", G_CALLBACK(notify), nullptr);
      g_signal_connect_after(widget, "notify::resizable", G_CALLBACK(notify), nullptr);
    }
  }
  return controls;
}

// Mutter also checks Motif functions for application requests. Permit an
// explicit request until its state arrives; keep that function while in the
// requested state so the WM does not undo it. Restoration reapplies the policy.
static void AllowProgrammaticWindowControl(GtkWidget* widget, GdkWindow* surface, bool maximize) {
  auto* controls = GetWindowControls(widget, surface, false);
  if (!controls || (maximize ? controls->maximizable : controls->minimizable))
    return;
  if (maximize)
    controls->pending_maximize = true;
  else
    controls->pending_minimize = true;
  ApplyWindowControls(widget, surface, *controls);
  // A compositor may reject the request. Do not leave a pending permission
  // enabled indefinitely in that case. The source holds only the native object.
  GObject* object = widget ? G_OBJECT(widget) : G_OBJECT(surface);
  g_timeout_add_full(
      G_PRIORITY_DEFAULT, 1000,
      +[](gpointer data) -> gboolean {
        auto* object = G_OBJECT(data);
        auto* controls =
            static_cast<WindowControls*>(g_object_get_data(object, kWindowControlsKey));
        controls->pending_maximize = controls->pending_minimize = false;
        auto* widget = GTK_IS_WIDGET(object) ? GTK_WIDGET(object) : nullptr;
        auto* surface = widget ? gtk_widget_get_window(widget) : GDK_WINDOW(object);
        if (surface && !gdk_window_is_destroyed(surface))
          ApplyWindowControls(widget, surface, *controls);
        return G_SOURCE_REMOVE;
      },
      g_object_ref(object), g_object_unref);
}

// Private implementation class
class Window::Impl {
 public:
  Impl(GtkWidget* widget, GdkWindow* gdk_window)
      : widget_(widget),
        gdk_window_(gdk_window),
        title_bar_style_(TitleBarStyle::Normal),
        background_color_(Color::White) {}

  ~Impl() {
    if (hints_refresh_source_) {
      g_source_remove(hints_refresh_source_);
    }
    if (hints_widget_) {
      g_signal_handlers_disconnect_by_data(hints_widget_, this);
      g_object_remove_weak_pointer(G_OBJECT(hints_widget_),
                                   reinterpret_cast<gpointer*>(&hints_widget_));
    }
  }

  void ApplyGeometryHints() {
    if (widget_ && GTK_IS_WINDOW(widget_) && !hints_widget_) {
      hints_widget_ = widget_;
      g_object_add_weak_pointer(G_OBJECT(hints_widget_),
                                reinterpret_cast<gpointer*>(&hints_widget_));
      // Decorations are not measurable until mapping. Recompute after allocations
      // too, since maximizing/restoring or changing the title bar changes them.
      g_signal_connect_after(hints_widget_, "map", G_CALLBACK(+[](GtkWidget*, gpointer data) {
                               static_cast<Impl*>(data)->ScheduleGeometryHints();
                             }),
                             this);
      g_signal_connect_after(hints_widget_, "size-allocate",
                             G_CALLBACK(+[](GtkWidget*, GtkAllocation*, gpointer data) {
                               static_cast<Impl*>(data)->ScheduleGeometryHints();
                             }),
                             this);
    }

    GdkGeometry geometry = {};
    int flags = 0;
    auto dimension = [](double value, int offset) {
      return static_cast<gint>(std::clamp(value + offset, 1.0, static_cast<double>(G_MAXINT)));
    };
    if (minimum_size_.width > 0 || minimum_size_.height > 0) {
      geometry.min_width =
          minimum_size_.width > 0 ? dimension(minimum_size_.width, hint_width_offset_) : 0;
      geometry.min_height =
          minimum_size_.height > 0 ? dimension(minimum_size_.height, hint_height_offset_) : 0;
      flags |= GDK_HINT_MIN_SIZE;
    }
    if (maximum_size_.width > 0 || maximum_size_.height > 0) {
      geometry.max_width =
          maximum_size_.width > 0 ? dimension(maximum_size_.width, hint_width_offset_) : G_MAXINT;
      geometry.max_height = maximum_size_.height > 0
                                ? dimension(maximum_size_.height, hint_height_offset_)
                                : G_MAXINT;
      flags |= GDK_HINT_MAX_SIZE;
    }
    if (aspect_ratio_ > 0) {
      geometry.min_aspect = geometry.max_aspect =
          surface_aspect_ratio_ > 0 ? surface_aspect_ratio_ : aspect_ratio_;
      flags |= GDK_HINT_ASPECT;
    }
    // GTK keeps these hints across allocations; setting only GDK hints on a
    // GtkWindow would let GTK overwrite them on its next resize.
    if (hints_widget_) {
      gtk_window_set_geometry_hints(GTK_WINDOW(hints_widget_), nullptr, &geometry,
                                    static_cast<GdkWindowHints>(flags));
    } else if (gdk_window_) {
      gdk_window_set_geometry_hints(gdk_window_, &geometry, static_cast<GdkWindowHints>(flags));
    }
  }

  void ScheduleGeometryHints() {
    if (hints_refresh_source_) {
      return;
    }
    // GTK finishes its resize bookkeeping after size-allocate. Updating hints
    // inside that signal can lose the queued resize, and native frame extents
    // can still describe the previous allocation. Wait until GTK has settled.
    hints_refresh_source_ = g_idle_add(
        +[](gpointer data) -> gboolean {
          auto* self = static_cast<Impl*>(data);
          self->hints_refresh_source_ = 0;
          if (self->hints_widget_ && gtk_widget_get_realized(self->hints_widget_)) {
            self->gdk_window_ = gtk_widget_get_window(self->hints_widget_);
            self->RefreshGeometryHints();
          }
          return G_SOURCE_REMOVE;
        },
        this);
  }

  void RefreshGeometryHints() {
    if (!gdk_window_ || !gdk_window_is_viewable(gdk_window_)) {
      return;
    }
    const Layout layout = GetLayout(widget_, gdk_window_);
    // Hints describe the GDK surface, whereas the API describes the visible
    // frame: add CSD shadows, or subtract server-side decorations. The CSD
    // title bar already belongs to both the surface and the public frame.
    const int surface_width = gdk_window_get_width(gdk_window_);
    const int surface_height = gdk_window_get_height(gdk_window_);
    const int width = surface_width - layout.frame.width;
    const int height = surface_height - layout.frame.height;
    // The API ratio is the content's, but GTK (client-side, on Wayland) and the
    // X11 window manager apply GDK_HINT_ASPECT to the whole surface. With
    // client-side decorations that includes the header bar and the shadow: a
    // fixed extra size no constant surface ratio can describe. Use the surface
    // ratio that gives the content the requested ratio at its current height,
    // and follow every allocation, so a resize converges on the content ratio.
    double surface_aspect_ratio = 0.0;
    if (aspect_ratio_ > 0 && layout.content.height > 0) {
      const double content_height = layout.content.height;
      surface_aspect_ratio =
          (aspect_ratio_ * content_height + (surface_width - layout.content.width)) /
          (content_height + (surface_height - layout.content.height));
    }
    if (width != hint_width_offset_ || height != hint_height_offset_ ||
        std::abs(surface_aspect_ratio - surface_aspect_ratio_) > 1e-4) {
      hint_width_offset_ = width;
      hint_height_offset_ = height;
      surface_aspect_ratio_ = surface_aspect_ratio;
      ApplyGeometryHints();
    }
  }

  guint hints_refresh_source_ = 0;
  GtkWidget* hints_widget_ = nullptr;  // Weak; wrapped widgets may die before us.
  Size minimum_size_ = {0, 0};
  Size maximum_size_ = {-1, -1};
  int hint_width_offset_ = 0;
  int hint_height_offset_ = 0;
  double surface_aspect_ratio_ = 0.0;  // GDK_HINT_ASPECT; 0 until measured.
  GtkWidget* widget_;
  GdkWindow* gdk_window_;
  TitleBarStyle title_bar_style_;
  Color background_color_;
  double aspect_ratio_ = 0.0;
  // What SetContentSize() asked for while the window was not mapped yet. GDK only
  // learns the new size when the window manager confirms it, which is after anything
  // the caller does next — centring the window, say.
  Size requested_content_size_ = {0, 0};
  // Recorded only: keyboard focus is per window on Linux, see Window::SetNonActivating().
  bool non_activating_ = false;
};

Window::Window() {
  // Check if GTK is available
  GdkDisplay* display = gdk_display_get_default();
  if (!display) {
    std::cerr << "No display available for window creation" << std::endl;
    pimpl_ = std::make_unique<Impl>(nullptr, nullptr);
    return;
  }

  // Create a new GTK toplevel window
  GtkWidget* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  if (!widget) {
    std::cerr << "Failed to create GTK window" << std::endl;
    pimpl_ = std::make_unique<Impl>(nullptr, nullptr);
    return;
  }

  // Realize to ensure GdkWindow exists
  if (!gtk_widget_get_realized(widget)) {
    gtk_widget_realize(widget);
  }

  // Obtain GdkWindow
  GdkWindow* gdk_window = gtk_widget_get_window(widget);
  if (!gdk_window) {
    std::cerr << "Failed to get GdkWindow from GTK widget" << std::endl;
    gtk_widget_destroy(widget);
    pimpl_ = std::make_unique<Impl>(nullptr, nullptr);
    return;
  }

  // Allocate and attach a stable WindowId to the native objects
  WindowId id = IdAllocator::Allocate<Window>();
  if (id != IdAllocator::kInvalidId) {
    g_object_set_data(G_OBJECT(widget), kWindowIdKey,
                      reinterpret_cast<gpointer>(static_cast<uintptr_t>(id)));
    g_object_set_data(G_OBJECT(gdk_window), kWindowIdKey,
                      reinterpret_cast<gpointer>(static_cast<uintptr_t>(id)));
  }

  // Only create the instance, don't show the window
  pimpl_ = std::make_unique<Impl>(widget, gdk_window);
}

Window::Window(void* native_window) {
  // Wrap existing GdkWindow or GtkWidget
  GtkWidget* widget = nullptr;
  GdkWindow* gdk_window = nullptr;

  // Heuristic: if this looks like a GtkWidget*, use it; otherwise treat as GdkWindow*
  // In our codebase, native Linux window handles should be GtkWidget* (GtkWindow)
  if (native_window && GTK_IS_WIDGET(native_window)) {
    widget = static_cast<GtkWidget*>(native_window);
    if (!gtk_widget_get_realized(widget)) {
      gtk_widget_realize(widget);
    }
    gdk_window = gtk_widget_get_window(widget);
  } else if (native_window && GDK_IS_WINDOW(native_window)) {
    // A GdkWindow*: recover the GtkWidget that owns it, so GetNativeObject()
    // reports the same GtkWindow* whichever handle the wrapper was built from.
    gdk_window = static_cast<GdkWindow*>(native_window);
    gpointer user_data = nullptr;
    gdk_window_get_user_data(gdk_window, &user_data);
    if (user_data && GTK_IS_WIDGET(user_data)) {
      widget = static_cast<GtkWidget*>(user_data);
    }
  }

  // Like the other platforms, a wrapped window gets an ID on first sight, stored
  // on the native objects so every later wrapper and WindowManager agree on it.
  gpointer existing_id = nullptr;
  if (gdk_window) {
    existing_id = g_object_get_data(G_OBJECT(gdk_window), kWindowIdKey);
  }
  if (!existing_id && widget) {
    existing_id = g_object_get_data(G_OBJECT(widget), kWindowIdKey);
  }
  if (existing_id) {
    if (gdk_window) {
      g_object_set_data(G_OBJECT(gdk_window), kWindowIdKey, existing_id);
    }
  } else if (gdk_window || widget) {
    WindowId id = IdAllocator::Allocate<Window>();
    if (id != IdAllocator::kInvalidId) {
      gpointer data = reinterpret_cast<gpointer>(static_cast<uintptr_t>(id));
      if (gdk_window) {
        g_object_set_data(G_OBJECT(gdk_window), kWindowIdKey, data);
      }
      if (widget) {
        g_object_set_data(G_OBJECT(widget), kWindowIdKey, data);
      }
    }
  }

  pimpl_ = std::make_unique<Impl>(widget, gdk_window);
}

Window::~Window() {}

WindowId Window::GetId() const {
  // Prefer reading ID stored on the native objects
  if (pimpl_->gdk_window_) {
    gpointer data = g_object_get_data(G_OBJECT(pimpl_->gdk_window_), kWindowIdKey);
    if (data) {
      return static_cast<WindowId>(reinterpret_cast<uintptr_t>(data));
    }
  }
  if (pimpl_->widget_) {
    gpointer data = g_object_get_data(G_OBJECT(pimpl_->widget_), kWindowIdKey);
    if (data) {
      return static_cast<WindowId>(reinterpret_cast<uintptr_t>(data));
    }
  }
  return IdAllocator::kInvalidId;
}

void Window::Focus() {
  if (pimpl_->widget_) {
    gtk_window_present(GTK_WINDOW(pimpl_->widget_));
  } else if (pimpl_->gdk_window_) {
    gdk_window_focus(pimpl_->gdk_window_, GDK_CURRENT_TIME);
  }
}

void Window::Blur() {
  if (pimpl_->gdk_window_) {
    gdk_window_lower(pimpl_->gdk_window_);
  }
}

bool Window::IsFocused() const {
  // Asking the seat's keyboard for the window at its position is not an option: that
  // call is about pointer position and GDK rejects keyboard devices outright, so it
  // only logs an assertion failure and never finds a window. The toplevel's own
  // state works on both X11 and Wayland.
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    return gtk_window_is_active(GTK_WINDOW(pimpl_->widget_));
  }
  if (!pimpl_->gdk_window_)
    return false;
  return gdk_window_get_state(pimpl_->gdk_window_) & GDK_WINDOW_STATE_FOCUSED;
}

// Present through GTK so both its iconify-on-map bookkeeping and the WM's
// activation request agree. A bare GDK deiconify is ignored by Mutter.
static void PresentWindow(GtkWidget* widget) {
  auto* window = GTK_WINDOW(widget);
  gtk_window_deiconify(window);
  guint32 timestamp = gtk_get_current_event_time();
#ifdef GDK_WINDOWING_X11
  auto* surface = gtk_widget_get_window(widget);
  if (timestamp == GDK_CURRENT_TIME && surface && GDK_IS_X11_WINDOW(surface)) {
    // Calls from timers/FFI have no input event. Obtain X server time instead
    // of reusing GTK's possibly stale last-user-interaction timestamp.
    timestamp = gdk_x11_get_server_time(surface);
  }
#endif
  // Wayland activation uses the compositor's input serial/token via GDK;
  // an X timestamp (or a locally fabricated clock value) is not applicable.
  gtk_window_present_with_time(window, timestamp);
}

void Window::Show() {
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    PresentWindow(pimpl_->widget_);
  } else if (pimpl_->widget_) {
    gtk_widget_show(pimpl_->widget_);
  } else if (pimpl_->gdk_window_) {
    gdk_window_show(pimpl_->gdk_window_);
  }
}

void Window::ShowInactive() {
  if (pimpl_->widget_) {
    gtk_widget_show(pimpl_->widget_);
  } else if (pimpl_->gdk_window_) {
    gdk_window_show_unraised(pimpl_->gdk_window_);
  }
}

void Window::Hide() {
  if (pimpl_->widget_) {
    gtk_widget_hide(pimpl_->widget_);
  } else if (pimpl_->gdk_window_) {
    gdk_window_hide(pimpl_->gdk_window_);
  }
}

bool Window::IsVisible() const {
  if (pimpl_->widget_) {
    return gtk_widget_get_visible(pimpl_->widget_);
  }
  if (pimpl_->gdk_window_) {
    return gdk_window_is_visible(pimpl_->gdk_window_);
  }
  return false;
}

void Window::Maximize() {
  if (pimpl_->gdk_window_) {
    AllowProgrammaticWindowControl(pimpl_->widget_, pimpl_->gdk_window_, true);
    gdk_window_maximize(pimpl_->gdk_window_);
  }
}

void Window::Unmaximize() {
  if (pimpl_->gdk_window_) {
    gdk_window_unmaximize(pimpl_->gdk_window_);
  }
}

bool Window::IsMaximized() const {
  if (!pimpl_->gdk_window_)
    return false;
  GdkWindowState state = gdk_window_get_state(pimpl_->gdk_window_);
  return state & GDK_WINDOW_STATE_MAXIMIZED;
}

void Window::Minimize() {
  if (pimpl_->gdk_window_) {
    AllowProgrammaticWindowControl(pimpl_->widget_, pimpl_->gdk_window_, false);
    gdk_window_iconify(pimpl_->gdk_window_);
  }
}

void Window::Restore() {
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    PresentWindow(pimpl_->widget_);
  } else if (pimpl_->gdk_window_) {
    gdk_window_deiconify(pimpl_->gdk_window_);
    gdk_window_focus(pimpl_->gdk_window_, GDK_CURRENT_TIME);
  }
}

bool Window::IsMinimized() const {
  if (!pimpl_->gdk_window_)
    return false;
  GdkWindowState state = gdk_window_get_state(pimpl_->gdk_window_);
  return state & GDK_WINDOW_STATE_ICONIFIED;
}

void Window::SetFullScreen(bool is_full_screen) {
  if (!pimpl_->gdk_window_)
    return;
  if (is_full_screen) {
    gdk_window_fullscreen(pimpl_->gdk_window_);
  } else {
    gdk_window_unfullscreen(pimpl_->gdk_window_);
  }
}

bool Window::IsFullScreen() const {
  if (!pimpl_->gdk_window_)
    return false;
  GdkWindowState state = gdk_window_get_state(pimpl_->gdk_window_);
  return state & GDK_WINDOW_STATE_FULLSCREEN;
}

void Window::SetBounds(Rectangle bounds) {
  const Decorations decorations = GetDecorations(pimpl_->widget_, pimpl_->gdk_window_);
  SetContentBounds({bounds.x + decorations.left, bounds.y + decorations.top,
                    bounds.width - decorations.left - decorations.right,
                    bounds.height - decorations.top - decorations.bottom});
}

Rectangle Window::GetBounds() const {
  const Point position = GetPosition();
  const Size size = GetSize();
  return {position.x, position.y, size.width, size.height};
}

void Window::SetSize(Size size, bool animate) {
  const Decorations decorations = GetDecorations(pimpl_->widget_, pimpl_->gdk_window_);
  SetContentSize({size.width - decorations.left - decorations.right,
                  size.height - decorations.top - decorations.bottom});
}

Size Window::GetSize() const {
  const Decorations decorations = GetDecorations(pimpl_->widget_, pimpl_->gdk_window_);
  const Size content = GetContentSize();
  return {content.width + decorations.left + decorations.right,
          content.height + decorations.top + decorations.bottom};
}

void Window::SetContentSize(Size size) {
  if (pimpl_->widget_ && !gtk_widget_get_mapped(pimpl_->widget_)) {
    pimpl_->requested_content_size_ = size;
  }
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    if (linux_shadow::Get(pimpl_->widget_)) {
      size.width += linux_shadow::Get(pimpl_->widget_)->margin * 2;
      size.height += linux_shadow::Get(pimpl_->widget_)->margin * 2;
    }
    GtkWindow* gtk_window = GTK_WINDOW(pimpl_->widget_);
    if (!gtk_window_get_resizable(gtk_window)) {
      // GTK derives a non-resizable window's fixed geometry from its default
      // size. Updating only the resize request cannot shrink that geometry.
      gtk_window_set_default_size(gtk_window, (gint)size.width, (gint)size.height);
    }
    if (!gtk_widget_get_mapped(pimpl_->widget_)) {
      // Windows are realized as soon as they are created, and GTK then maps them at
      // whatever size the GdkWindow already has: a resize requested in between is
      // forgotten. Set all three, so the size holds whenever it is asked for.
      gtk_window_set_default_size(gtk_window, (gint)size.width, (gint)size.height);
      if (pimpl_->gdk_window_) {
        gdk_window_resize(pimpl_->gdk_window_, (gint)size.width, (gint)size.height);
      }
    }
    gtk_window_resize(gtk_window, (gint)size.width, (gint)size.height);
  } else if (pimpl_->gdk_window_) {
    gdk_window_resize(pimpl_->gdk_window_, (gint)size.width, (gint)size.height);
  }
}

Size Window::GetContentSize() const {
  if (pimpl_->widget_ && !gtk_widget_get_mapped(pimpl_->widget_) &&
      pimpl_->requested_content_size_.width > 0) {
    return pimpl_->requested_content_size_;
  }
  if (!pimpl_->gdk_window_) {
    return {0, 0};
  }
  if (!gdk_window_is_viewable(pimpl_->gdk_window_)) {
    return {static_cast<double>(gdk_window_get_width(pimpl_->gdk_window_)),
            static_cast<double>(gdk_window_get_height(pimpl_->gdk_window_))};
  }
  // Not the GdkWindow's size: with client-side decorations that includes the title bar
  // and the shadow, and would not be what SetContentSize() was given.
  const Layout layout = GetLayout(pimpl_->widget_, pimpl_->gdk_window_);
  return {static_cast<double>(layout.content.width),
          static_cast<double>(layout.content.height)};
}

void Window::SetContentBounds(Rectangle bounds) {
  const Decorations decorations = GetDecorations(pimpl_->widget_, pimpl_->gdk_window_);
  // gtk_window_move() takes the frame's corner, so the content lands where asked.
  SetPosition({bounds.x - decorations.left, bounds.y - decorations.top});
  SetContentSize({bounds.width, bounds.height});
}

Rectangle Window::GetContentBounds() const {
  if (!pimpl_->gdk_window_) {
    return {0, 0, 0, 0};
  }
  if (!gdk_window_is_viewable(pimpl_->gdk_window_)) {
    gint origin_x = 0;
    gint origin_y = 0;
    gdk_window_get_origin(pimpl_->gdk_window_, &origin_x, &origin_y);
    const Size size = GetContentSize();
    return {static_cast<double>(origin_x), static_cast<double>(origin_y), size.width,
            size.height};
  }
  const Layout layout = GetLayout(pimpl_->widget_, pimpl_->gdk_window_);
  return {static_cast<double>(layout.content.x), static_cast<double>(layout.content.y),
          static_cast<double>(layout.content.width),
          static_cast<double>(layout.content.height)};
}

void Window::SetMinimumSize(Size size) {
  pimpl_->minimum_size_ = size;
  pimpl_->RefreshGeometryHints();
  pimpl_->ApplyGeometryHints();
}

Size Window::GetMinimumSize() const {
  return pimpl_->minimum_size_;
}

void Window::SetMaximumSize(Size size) {
  pimpl_->maximum_size_ = size;
  pimpl_->RefreshGeometryHints();
  pimpl_->ApplyGeometryHints();
}

void Window::SetAspectRatio(double aspect_ratio) {
  pimpl_->aspect_ratio_ = aspect_ratio > 0.0 ? aspect_ratio : 0.0;
  pimpl_->surface_aspect_ratio_ = 0.0;  // Derived from the old ratio; re-measure.
  pimpl_->RefreshGeometryHints();
  pimpl_->ApplyGeometryHints();
}

double Window::GetAspectRatio() const {
  return pimpl_->aspect_ratio_;
}

Size Window::GetMaximumSize() const {
  return pimpl_->maximum_size_;
}

void Window::SetResizable(bool is_resizable) {
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    auto* window = GTK_WINDOW(pimpl_->widget_);
    if (!is_resizable && gtk_window_get_resizable(window)) {
      // Preserve the current size rather than reverting to an old default when
      // GTK computes the fixed-size hints. Use GTK's content coordinates so CSD
      // title bars and shadows are accounted for exactly once.
      gint width = 0;
      gint height = 0;
      gtk_window_get_size(window, &width, &height);
      gtk_window_set_default_size(window, width, height);
    }
    gtk_window_set_resizable(window, is_resizable);
  }
}

bool Window::IsResizable() const {
  return pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)
             ? gtk_window_get_resizable(GTK_WINDOW(pimpl_->widget_))
             : true;
}

void Window::SetMovable(bool is_movable) {
  auto* controls = GetWindowControls(pimpl_->widget_, pimpl_->gdk_window_, true);
  if (controls) {
    controls->movable = is_movable;
    ApplyWindowControls(pimpl_->widget_, pimpl_->gdk_window_, *controls);
  }
}

bool Window::IsMovable() const {
  const auto* controls = GetWindowControls(pimpl_->widget_, pimpl_->gdk_window_, false);
  return controls ? controls->movable : true;
}

void Window::SetMinimizable(bool is_minimizable) {
  auto* controls = GetWindowControls(pimpl_->widget_, pimpl_->gdk_window_, true);
  if (controls) {
    controls->minimizable = is_minimizable;
    ApplyWindowControls(pimpl_->widget_, pimpl_->gdk_window_, *controls);
  }
}

bool Window::IsMinimizable() const {
  const auto* controls = GetWindowControls(pimpl_->widget_, pimpl_->gdk_window_, false);
  return controls ? controls->minimizable : true;
}

void Window::SetMaximizable(bool is_maximizable) {
  auto* controls = GetWindowControls(pimpl_->widget_, pimpl_->gdk_window_, true);
  if (controls) {
    controls->maximizable = is_maximizable;
    ApplyWindowControls(pimpl_->widget_, pimpl_->gdk_window_, *controls);
  }
}

bool Window::IsMaximizable() const {
  const auto* controls = GetWindowControls(pimpl_->widget_, pimpl_->gdk_window_, false);
  return controls ? controls->maximizable : true;
}

void Window::SetFullScreenable(bool is_full_screenable) {
  // Provide stub implementation
}

bool Window::IsFullScreenable() const {
  return true;  // Default assumption
}

void Window::SetClosable(bool is_closable) {
  auto* controls = GetWindowControls(pimpl_->widget_, pimpl_->gdk_window_, true);
  if (controls) {
    controls->closable = is_closable;
    if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
      gtk_window_set_deletable(GTK_WINDOW(pimpl_->widget_), is_closable);
    }
    ApplyWindowControls(pimpl_->widget_, pimpl_->gdk_window_, *controls);
  }
}

bool Window::IsClosable() const {
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    return gtk_window_get_deletable(GTK_WINDOW(pimpl_->widget_));
  }
  const auto* controls = GetWindowControls(pimpl_->widget_, pimpl_->gdk_window_, false);
  return controls ? controls->closable : true;
}

void Window::SetWindowControlButtonsVisible(bool is_visible) {
  // TODO: Implement for Linux
  // This would involve manipulating GTK window decorations
}

bool Window::IsWindowControlButtonsVisible() const {
  // TODO: Implement for Linux
  return true;  // Default to visible
}

void Window::SetAlwaysOnTop(bool is_always_on_top) {
  if (pimpl_->gdk_window_) {
    gdk_window_set_keep_above(pimpl_->gdk_window_, is_always_on_top);
  }
}

bool Window::IsAlwaysOnTop() const {
  if (!pimpl_->gdk_window_)
    return false;
  GdkWindowState state = gdk_window_get_state(pimpl_->gdk_window_);
  return state & GDK_WINDOW_STATE_ABOVE;
}

void Window::SetAlwaysOnBottom(bool is_always_on_bottom) {
  // GDK clears _NET_WM_STATE_ABOVE when setting BELOW and vice versa, so the two
  // settings are naturally exclusive here.
  if (pimpl_->gdk_window_) {
    gdk_window_set_keep_below(pimpl_->gdk_window_, is_always_on_bottom);
  }
}

bool Window::IsAlwaysOnBottom() const {
  if (!pimpl_->gdk_window_)
    return false;
  GdkWindowState state = gdk_window_get_state(pimpl_->gdk_window_);
  return state & GDK_WINDOW_STATE_BELOW;
}

// GDK tells a Wayland compositor about the parent when the child is mapped or the
// relationship changes - and only if the parent has a surface by then. A child that
// is mapped before its parent (an embedding framework decides the order) would stay
// without one for good, so the relationship is announced again once the parent is up.
static gboolean OnParentMappedAnnounceChild(GtkWidget* parent, GdkEvent* event, gpointer data) {
  (void)event;
  GtkWidget* child = GTK_WIDGET(data);
  if (GTK_IS_WINDOW(child) && gtk_window_get_transient_for(GTK_WINDOW(child)) == GTK_WINDOW(parent)) {
    gtk_window_set_transient_for(GTK_WINDOW(child), nullptr);
    gtk_window_set_transient_for(GTK_WINDOW(child), GTK_WINDOW(parent));
  }
  g_signal_handlers_disconnect_matched(parent, static_cast<GSignalMatchType>(
                                                   G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA),
                                       0, 0, nullptr,
                                       reinterpret_cast<gpointer>(OnParentMappedAnnounceChild),
                                       child);
  return FALSE;
}

bool Window::SetParentWindow(std::shared_ptr<Window> parent) {
  GtkWidget* widget = static_cast<GtkWidget*>(GetNativeObject());
  if (!widget || !GTK_IS_WINDOW(widget)) {
    return false;
  }
  GtkWindow* parent_window = nullptr;
  if (parent) {
    GtkWidget* parent_widget = static_cast<GtkWidget*>(parent->GetNativeObject());
    if (!parent_widget || !GTK_IS_WINDOW(parent_widget)) {
      return false;
    }
    parent_window = GTK_WINDOW(parent_widget);
    // Neither itself nor one of its own descendants
    for (GtkWindow* ancestor = parent_window; ancestor;
         ancestor = gtk_window_get_transient_for(ancestor)) {
      if (ancestor == GTK_WINDOW(widget)) {
        return false;
      }
    }
  }
  gtk_window_set_transient_for(GTK_WINDOW(widget), parent_window);
  if (parent_window && !gtk_widget_get_mapped(GTK_WIDGET(parent_window))) {
    // Disconnected with the child, should that go away first
    g_signal_connect_object(parent_window, "map-event",
                            G_CALLBACK(OnParentMappedAnnounceChild), widget, G_CONNECT_AFTER);
  }
  return true;
}

std::shared_ptr<Window> Window::GetParentWindow() const {
  GtkWidget* widget = static_cast<GtkWidget*>(GetNativeObject());
  if (!widget || !GTK_IS_WINDOW(widget)) {
    return nullptr;
  }
  GtkWindow* parent_window = gtk_window_get_transient_for(GTK_WINDOW(widget));
  if (!parent_window) {
    return nullptr;
  }
  // The wrapper takes the ID the native window already carries, which is how
  // the registered Window for it, if there is one, is found.
  auto wrapper = std::make_shared<Window>(static_cast<void*>(parent_window));
  auto registered = WindowManager::GetInstance().Get(wrapper->GetId());
  return registered ? registered : wrapper;
}

void Window::SetNonActivating(bool is_non_activating) {
  // Keyboard focus is per window on Linux, so a non-activating window has no
  // observable difference here. Record the flag so IsNonActivating() round-trips.
  pimpl_->non_activating_ = is_non_activating;
}

bool Window::IsNonActivating() const {
  return pimpl_->non_activating_;
}

void Window::SetPosition(Point point) {
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    // gtk_window_move() positions the frame, and remembers the position for a window
    // that is not mapped yet — which gdk_window_move() does not.
    const int margin = linux_shadow::Get(pimpl_->widget_) ? linux_shadow::Get(pimpl_->widget_)->margin : 0;
    gtk_window_move(GTK_WINDOW(pimpl_->widget_), (gint)point.x - margin, (gint)point.y - margin);
  } else if (pimpl_->gdk_window_) {
    gdk_window_move(pimpl_->gdk_window_, (gint)point.x, (gint)point.y);
  }
}

Point Window::GetPosition() const {
  if (!pimpl_->gdk_window_) {
    return {0, 0};
  }
  if (!gdk_window_is_viewable(pimpl_->gdk_window_)) {
    GdkRectangle frame = {};
    gdk_window_get_frame_extents(pimpl_->gdk_window_, &frame);
    return {static_cast<double>(frame.x), static_cast<double>(frame.y)};
  }
  const Layout layout = GetLayout(pimpl_->widget_, pimpl_->gdk_window_);
  return {static_cast<double>(layout.frame.x), static_cast<double>(layout.frame.y)};
}

void Window::Center() {
  if (!pimpl_->gdk_window_)
    return;

  // The size to centre, decorations included: they are part of the window.
  const Size size = GetSize();
  const gint window_width = (gint)size.width;
  const gint window_height = (gint)size.height;

  // Get the screen size
  GdkDisplay* display = gdk_window_get_display(pimpl_->gdk_window_);
  GdkMonitor* monitor = gdk_display_get_primary_monitor(display);
  if (!monitor) {
    // Fallback to first monitor if no primary monitor is found
    monitor = gdk_display_get_monitor(display, 0);
  }

  if (monitor) {
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);

    // Calculate center position
    gint center_x = geometry.x + (geometry.width - window_width) / 2;
    gint center_y = geometry.y + (geometry.height - window_height) / 2;

    // Move the window to center
    SetPosition({static_cast<double>(center_x), static_cast<double>(center_y)});
  }
}

void Window::SetTitle(std::string title) {
  // Prefer setting title via GtkWindow if available
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    gtk_window_set_title(GTK_WINDOW(pimpl_->widget_), title.c_str());
    return;
  }

  // If only GdkWindow is available, try to get associated GtkWindow
  if (pimpl_->gdk_window_) {
    gpointer user_data = nullptr;
    gdk_window_get_user_data(pimpl_->gdk_window_, &user_data);
    if (user_data && GTK_IS_WINDOW(user_data)) {
      gtk_window_set_title(GTK_WINDOW(user_data), title.c_str());
      return;
    }

    // Fallback: set title via GDK for toplevel windows
    gdk_window_set_title(pimpl_->gdk_window_, title.c_str());
  }
}

std::string Window::GetTitle() const {
  // Prefer reading title via GtkWindow if available
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    const gchar* t = gtk_window_get_title(GTK_WINDOW(pimpl_->widget_));
    return t ? std::string(t) : std::string();
  }

  // If only GdkWindow is available, try to get associated GtkWindow
  if (pimpl_->gdk_window_) {
    gpointer user_data = nullptr;
    gdk_window_get_user_data(pimpl_->gdk_window_, &user_data);
    if (user_data && GTK_IS_WINDOW(user_data)) {
      const gchar* t = gtk_window_get_title(GTK_WINDOW(user_data));
      return t ? std::string(t) : std::string();
    }
  }

  // No reliable way to get title directly from GdkWindow
  return std::string();
}

void Window::SetTitleBarStyle(TitleBarStyle style) {
  const bool has_shadow = HasShadow();
  pimpl_->title_bar_style_ = style;
  // Wrapping a Flutter controller creates a new Window each time. Keep the
  // title-bar policy on the native object so later wrappers can apply shapes.
  GObject* object = pimpl_->gdk_window_ ? G_OBJECT(pimpl_->gdk_window_)
                                     : (pimpl_->widget_ ? G_OBJECT(pimpl_->widget_) : nullptr);
  if (object) {
    g_object_set_data(object, kTitleBarStyleKey,
                      GINT_TO_POINTER(static_cast<int>(style) + 1));
  }

  if (!pimpl_->widget_ || !GTK_IS_WINDOW(pimpl_->widget_))
    return;

  GtkWindow* gtk_window = GTK_WINDOW(pimpl_->widget_);
  bool show_decorations = (style == TitleBarStyle::Normal);
  if (show_decorations) linux_shadow::Remove(pimpl_->widget_);

  // Try to find and toggle header bar visibility
  GtkWidget* header_bar = FindHeaderBar(pimpl_->widget_);
  if (header_bar) {
    gtk_widget_set_visible(header_bar, show_decorations);
  } else {
    // If no header bar found, toggle window decorations
    const gchar* title = gtk_window_get_title(gtk_window);
    if (title != nullptr) {
      gtk_window_set_decorated(gtk_window, show_decorations);
    }
  }

  // When restoring to normal, ensure decorations are shown
  if (show_decorations) {
    gtk_window_set_decorated(gtk_window, TRUE);
  }
  SetHasShadow(has_shadow);
}

TitleBarStyle Window::GetTitleBarStyle() const {
  GObject* object = pimpl_->gdk_window_ ? G_OBJECT(pimpl_->gdk_window_)
                                     : (pimpl_->widget_ ? G_OBJECT(pimpl_->widget_) : nullptr);
  if (object) {
    gpointer stored = g_object_get_data(object, kTitleBarStyleKey);
    if (stored) return static_cast<TitleBarStyle>(GPOINTER_TO_INT(stored) - 1);
  }
  return pimpl_->title_bar_style_;
}

// A GTK header bar is a sibling above the content, not an overlay over it; taking it into
// the content area would mean rebuilding a widget tree the toolkit owns.
bool Window::SetContentUnderTitleBar(bool is_content_under_title_bar) {
  return false;
}

bool Window::IsContentUnderTitleBar() const {
  return false;
}

bool Window::IsContentUnderTitleBarSupported() {
  return false;
}

// The shadow of a window with client-side decorations - every toplevel on Wayland, and
// windows with a header bar on X11 - is drawn by GTK itself, from the CSS of the window's
// "decoration" node. That node cannot be styled through the window's own style context,
// so the window gets a style class, and one rule for the whole screen takes the shadow
// (and the hairline border that is part of it) away from windows carrying it. The state
// lives on the widget, so every wrapper of the window agrees. A window the window
// manager decorates has no such node: its shadow is not the application's to remove.
static const char* kNoShadowStyleClass = "nativeapi-no-shadow";
static const char* kPendingNoShadowKey = "nativeapi-pending-no-shadow";

static void EnsureNoShadowRule(GtkWidget* widget) {
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  GtkCssProvider* provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider,
                                  "window.nativeapi-no-shadow decoration,"
                                  "window.nativeapi-no-shadow decoration:backdrop {"
                                  "  box-shadow: none; border: none; }",
                                  -1, nullptr);
  gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(widget),
                                            GTK_STYLE_PROVIDER(provider),
                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

// Only ever on a mapped window. The shadow is also a margin of the surface, and GTK
// does not survive that margin changing between realizing a window and mapping it: it
// asks for a size with the new margin and allocates with the old one, which leaves the
// content too small for good. On a mapped window the change is taken in - the content
// size is asked for again, so that it is the surface that shrinks or grows.
// GTK marks the windows it decorates itself with the "csd" style class.
static bool DrawsOwnShadow(GtkWidget* widget) {
  return gtk_style_context_has_class(gtk_widget_get_style_context(widget), "csd");
}

static void ApplyShadowClass(GtkWidget* widget, bool has_shadow) {
  GtkStyleContext* context = gtk_widget_get_style_context(widget);
  if (!has_shadow && !DrawsOwnShadow(widget)) {
    return;  // the window manager's shadow: HasShadow() goes on saying true
  }
  if (has_shadow == !gtk_style_context_has_class(context, kNoShadowStyleClass)) {
    return;
  }
  if (has_shadow) {
    gtk_style_context_remove_class(context, kNoShadowStyleClass);
  } else {
    EnsureNoShadowRule(widget);
    gtk_style_context_add_class(context, kNoShadowStyleClass);
  }
}

// What SetHasShadow() asked for while the window was not mapped: 1 for no shadow, 2 for
// a shadow. Also covers a window that is hidden at the moment, which is in the same
// state as one that was never shown.
static gboolean OnMappedApplyShadow(GtkWidget* widget, GdkEvent* event, gpointer data) {
  (void)event;
  (void)data;
  const gint pending = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), kPendingNoShadowKey));
  g_object_set_data(G_OBJECT(widget), kPendingNoShadowKey, nullptr);
  if (pending != 0) {
    ApplyShadowClass(widget, pending == 2);
  }
  g_signal_handlers_disconnect_matched(widget, G_SIGNAL_MATCH_FUNC, 0, 0, nullptr,
                                       reinterpret_cast<gpointer>(OnMappedApplyShadow), nullptr);
  return FALSE;
}

void Window::SetHasShadow(bool has_shadow) {
  GtkWidget* widget = pimpl_->widget_;
  if (!widget || !GTK_IS_WINDOW(widget)) {
    return;
  }
  GObject* object = G_OBJECT(widget);
  if (GetTitleBarStyle() == TitleBarStyle::Hidden) {
    auto* shadow = linux_shadow::Ensure(widget);
    shadow->enabled = has_shadow;
    RefreshShadowInput(widget);
    // Like the system-shadow path below, do not change CSD extents between
    // realize and map. GTK otherwise allocates content with stale extents.
    if (gtk_widget_get_mapped(widget)) {
      ApplyShadowClass(widget, false);
    } else {
      if (!g_object_get_data(object, kPendingNoShadowKey)) {
        g_signal_connect(widget, "map-event", G_CALLBACK(OnMappedApplyShadow), nullptr);
      }
      g_object_set_data(object, kPendingNoShadowKey, GINT_TO_POINTER(1));
    }
    gtk_widget_queue_draw(widget);
    return;
  }
  if (gtk_widget_get_mapped(widget)) {
    g_object_set_data(object, kPendingNoShadowKey, nullptr);
    ApplyShadowClass(widget, has_shadow);
    return;
  }
  if (!has_shadow && gtk_widget_get_realized(widget) && !DrawsOwnShadow(widget)) {
    return;  // see ApplyShadowClass()
  }
  if (!g_object_get_data(object, kPendingNoShadowKey)) {
    g_signal_connect(widget, "map-event", G_CALLBACK(OnMappedApplyShadow), nullptr);
  }
  g_object_set_data(object, kPendingNoShadowKey, GINT_TO_POINTER(has_shadow ? 2 : 1));
}

bool Window::SetCustomShadow(std::shared_ptr<WindowShadow> shadow) {
  auto* widget = pimpl_->widget_;
  if (!widget || !GTK_IS_WINDOW(widget) ||
      (shadow && GetTitleBarStyle() != TitleBarStyle::Hidden)) return false;
  const bool enabled = HasShadow();
  g_object_set_data_full(G_OBJECT(widget), linux_shadow::kConfig,
                        shadow ? new WindowShadow(*shadow) : nullptr,
                        [](gpointer p) { delete static_cast<WindowShadow*>(p); });
  if (GetTitleBarStyle() == TitleBarStyle::Hidden)
    linux_shadow::Configure(widget, shadow ? std::make_shared<WindowShadow>(*shadow) : nullptr);
  SetHasShadow(enabled);
  return true;
}
std::shared_ptr<WindowShadow> Window::GetCustomShadow() const {
  const auto* options = pimpl_->widget_ ? static_cast<WindowShadow*>(
      g_object_get_data(G_OBJECT(pimpl_->widget_), linux_shadow::kConfig)) : nullptr;
  return options ? std::make_shared<WindowShadow>(*options) : nullptr;
}

bool Window::HasShadow() const {
  GtkWidget* widget = pimpl_->widget_;
  if (auto* shadow = linux_shadow::Get(widget)) return shadow->enabled;
  if (!widget || !GTK_IS_WINDOW(widget)) {
    return true;
  }
  const gint pending = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), kPendingNoShadowKey));
  if (pending != 0) {
    return pending == 2;
  }
  return !gtk_style_context_has_class(gtk_widget_get_style_context(widget), kNoShadowStyleClass);
}

void Window::SetOpacity(float opacity) {
  if (pimpl_->gdk_window_) {
    gdk_window_set_opacity(pimpl_->gdk_window_, opacity);
  }
}

float Window::GetOpacity() const {
  // GDK doesn't provide a direct way to get opacity
  return 1.0f;  // Default assumption
}

// Blur behind a window is the compositor's to offer on Linux, and there is no common way
// to ask for it: KWin has a protocol of its own, Mutter has nothing.
bool Window::SetVisualEffect(VisualEffect effect) {
  return effect == VisualEffect::None;
}

VisualEffect Window::GetVisualEffect() const {
  return VisualEffect::None;
}

bool Window::IsVisualEffectSupported(VisualEffect effect) {
  return effect == VisualEffect::None;
}

void Window::SetBackgroundColor(const Color& color) {
  if (!pimpl_->widget_)
    return;

  // Store the color
  pimpl_->background_color_ = color;

  // Create CSS provider for background color
  GtkCssProvider* provider = gtk_css_provider_new();
  
  // Format CSS string with RGBA color
  gchar* css = g_strdup_printf(
    "window { background-color: rgba(%d, %d, %d, %.2f); }",
    color.r, color.g, color.b, color.a / 255.0);
  
  gtk_css_provider_load_from_data(provider, css, -1, nullptr);
  g_free(css);
  
  // Apply CSS to the widget
  GtkStyleContext* context = gtk_widget_get_style_context(pimpl_->widget_);
  gtk_style_context_add_provider(context,
                                 GTK_STYLE_PROVIDER(provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  
  g_object_unref(provider);

  // The content may paint a backing of its own over the window's background. A Flutter
  // view does - opaque black - and has a setter for it, which is looked up at run time:
  // core does not link against Flutter.
  using SetViewBackgroundFn = void (*)(gpointer view, const GdkRGBA* color);
  static const auto set_view_background =
      reinterpret_cast<SetViewBackgroundFn>(dlsym(RTLD_DEFAULT, "fl_view_set_background_color"));
  if (set_view_background) {
    const GdkRGBA rgba = {color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0};
    std::function<void(GtkWidget*)> visit = [&](GtkWidget* widget) {
      if (g_strcmp0(G_OBJECT_TYPE_NAME(widget), "FlView") == 0) {
        set_view_background(widget, &rgba);
        return;
      }
      if (GTK_IS_CONTAINER(widget)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* l = children; l != nullptr; l = l->next) {
          visit(GTK_WIDGET(l->data));
        }
        g_list_free(children);
      }
    };
    visit(pimpl_->widget_);
  }
  gtk_widget_queue_draw(pimpl_->widget_);
}

Color Window::GetBackgroundColor() const {
  if (!pimpl_->widget_)
    return Color::White;
  
  // Return the stored background color
  // Since we set it via CSS, we track it ourselves to avoid using deprecated APIs
  return pimpl_->background_color_;
}

void Window::SetVisibleOnAllWorkspaces(bool is_visible_on_all_workspaces) {
  if (pimpl_->gdk_window_) {
    gdk_window_stick(pimpl_->gdk_window_);
  }
}

bool Window::IsVisibleOnAllWorkspaces() const {
  if (!pimpl_->gdk_window_)
    return false;
  GdkWindowState state = gdk_window_get_state(pimpl_->gdk_window_);
  return state & GDK_WINDOW_STATE_STICKY;
}

void Window::SetVisibleInTaskbar(bool is_visible_in_taskbar) {
  if (pimpl_->widget_ && GTK_IS_WINDOW(pimpl_->widget_)) {
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(pimpl_->widget_), !is_visible_in_taskbar);
  }
}

bool Window::IsVisibleInTaskbar() const {
  if (!pimpl_->widget_ || !GTK_IS_WINDOW(pimpl_->widget_))
    return true;
  return !gtk_window_get_skip_taskbar_hint(GTK_WINDOW(pimpl_->widget_));
}

void Window::SetIgnoreMouseEvents(bool is_ignore_mouse_events) {
  // This would involve setting input shapes or event masks
  // Provide stub implementation
}

bool Window::IsIgnoreMouseEvents() const {
  return false;  // Default assumption
}

void Window::SetFocusable(bool is_focusable) {
  // This would typically be set via window hints
  // Provide stub implementation
}

bool Window::IsFocusable() const {
  return true;  // Default assumption
}

void Window::StartDragging() {
  if (!pimpl_->gdk_window_) {
    return;
  }

  GdkDisplay* display = gdk_window_get_display(pimpl_->gdk_window_);
  GdkSeat* seat = display ? gdk_display_get_default_seat(display) : nullptr;
  GdkDevice* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
  if (!pointer) {
    return;
  }

  gint root_x = 0, root_y = 0;
  gdk_device_get_position(pointer, nullptr, &root_x, &root_y);
  // The window manager moves the window from here on, so this works on Wayland too,
  // where the position above is meaningless and ignored — what matters is the
  // timestamp of the mouse-down we are called from, which is what the compositor
  // matches against the press it delivered.
  gdk_window_begin_move_drag_for_device(pimpl_->gdk_window_, pointer, GDK_BUTTON_PRIMARY,
                                        root_x, root_y, gtk_get_current_event_time());
}

void Window::StartResizing(ResizeEdge edge) {
  if (!pimpl_->gdk_window_) {
    return;
  }

  GdkWindowEdge gdk_edge;
  switch (edge) {
    case ResizeEdge::Top:
      gdk_edge = GDK_WINDOW_EDGE_NORTH;
      break;
    case ResizeEdge::Left:
      gdk_edge = GDK_WINDOW_EDGE_WEST;
      break;
    case ResizeEdge::Right:
      gdk_edge = GDK_WINDOW_EDGE_EAST;
      break;
    case ResizeEdge::Bottom:
      gdk_edge = GDK_WINDOW_EDGE_SOUTH;
      break;
    case ResizeEdge::TopLeft:
      gdk_edge = GDK_WINDOW_EDGE_NORTH_WEST;
      break;
    case ResizeEdge::TopRight:
      gdk_edge = GDK_WINDOW_EDGE_NORTH_EAST;
      break;
    case ResizeEdge::BottomLeft:
      gdk_edge = GDK_WINDOW_EDGE_SOUTH_WEST;
      break;
    case ResizeEdge::BottomRight:
    default:
      gdk_edge = GDK_WINDOW_EDGE_SOUTH_EAST;
      break;
  }

  GdkDisplay* display = gdk_window_get_display(pimpl_->gdk_window_);
  GdkSeat* seat = display ? gdk_display_get_default_seat(display) : nullptr;
  GdkDevice* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
  if (!pointer) {
    return;
  }

  gint root_x = 0, root_y = 0;
  gdk_device_get_position(pointer, nullptr, &root_x, &root_y);
  // gtk_get_current_event_time() yields the timestamp of the mouse-down we are
  // called from, which window managers require to accept the drag request.
  gdk_window_begin_resize_drag_for_device(pimpl_->gdk_window_, gdk_edge, pointer,
                                          GDK_BUTTON_PRIMARY, root_x, root_y,
                                          gtk_get_current_event_time());
}

void* Window::GetNativeObjectInternal() const {
  // Return the GtkWidget* (GtkWindow) as the native handle on Linux
  return pimpl_ ? static_cast<void*>(pimpl_->widget_ ? pimpl_->widget_ : nullptr) : nullptr;
}

bool Window::IsShapeSupported() {
  GdkDisplay* display = gdk_display_get_default();
  return display && gdk_display_supports_shapes(display) &&
         gdk_display_supports_input_shapes(display);
}

// Both visible and input polygons use the same pixel-centre, even-odd rasterization.
static cairo_region_t* RasterizeShape(GtkWidget* widget, GdkWindow* window,
                                      const WindowShape& polygon) {
  const WindowShape* shape = &polygon;
  // Rasterize at pixel centres using the even-odd rule. GDK region coordinates
  // are logical pixels, including on HiDPI displays. Bound work by the surface.
  cairo_region_t* region = cairo_region_create();
  const int width = gdk_window_get_width(window);
  const int height = gdk_window_get_height(window);
  gint origin_x = 0, origin_y = 0;
  gdk_window_get_origin(window, &origin_x, &origin_y);
  const auto layout = GetLayout(widget, window);
  int offset_x = layout.content.width > 0 ? layout.content.x - origin_x : 0;
  int offset_y = layout.content.height > 0 ? layout.content.y - origin_y : 0;
  // GetLayout deliberately normalizes Wayland frame coordinates to zero, hiding
  // the CSD shadow margin. Input regions instead need surface-local coordinates.
  if (widget && GTK_IS_WINDOW(widget)) {
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(widget));
    if (child && gtk_widget_get_mapped(child)) {
      gtk_widget_translate_coordinates(child, widget, 0, 0, &offset_x, &offset_y);
    }
#ifdef GDK_WINDOWING_WAYLAND
    else if (GDK_IS_WAYLAND_DISPLAY(gdk_window_get_display(window))) {
      gint content_width = 0, content_height = 0;
      gtk_window_get_size(GTK_WINDOW(widget), &content_width, &content_height);
      offset_x = std::max(0, (width - content_width) / 2);
      offset_y = std::max(0, (height - content_height) / 2);
    }
#endif
  }
  std::vector<double> crossings;
  for (int y = 0; y < height; ++y) {
    crossings.clear();
    const double scan = y + 0.5 - offset_y;
    for (size_t i = 0, j = shape->GetPointCount() - 1; i < shape->GetPointCount(); j = i++) {
      const auto a = shape->GetPointAt(j), b = shape->GetPointAt(i);
      if ((a.y > scan) != (b.y > scan)) {
        crossings.push_back(offset_x + a.x + (scan - a.y) * (b.x - a.x) / (b.y - a.y));
      }
    }
    std::sort(crossings.begin(), crossings.end());
    for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
      const int left = std::max(0, static_cast<int>(std::ceil(crossings[i] - 0.5)));
      const int right = std::min(width, static_cast<int>(std::ceil(crossings[i + 1] - 0.5)));
      if (right > left) {
        cairo_rectangle_int_t rect = {left, y, right - left, 1};
        cairo_region_union_rectangle(region, &rect);
      }
    }
  }
  return region;
}

static void ApplyInputRegion(GtkWidget* widget, GdkWindow* window, cairo_region_t* region) {
  // GTK intersects its CSD input region with the explicit widget region. Register
  // ours with GTK as well as GDK so a later allocation cannot overwrite it.
  if (widget) {
    gtk_widget_input_shape_combine_region(widget, region);
  } else {
    gdk_window_input_shape_combine_region(window, region, 0, 0);
  }
  g_object_set_data(G_OBJECT(window), kInputShapeKey, region ? GINT_TO_POINTER(1) : nullptr);
  gdk_window_invalidate_rect(window, nullptr, TRUE);
}

static void RefreshShadowInput(GtkWidget* widget) {
  auto* shadow = linux_shadow::Get(widget);
  auto* window = gtk_widget_get_window(widget);
  auto* child = gtk_bin_get_child(GTK_BIN(widget));
  if (!shadow || !window || !child || !gtk_widget_get_mapped(child)) return;
  const bool explicit_polygon = shadow->polygon.GetPointCount() > 0;
  WindowShape rectangle;
  const int width = gtk_widget_get_allocated_width(child);
  const int height = gtk_widget_get_allocated_height(child);
  if (!explicit_polygon) {
    rectangle.AddPoint({0, 0}); rectangle.AddPoint({double(width), 0});
    rectangle.AddPoint({double(width), double(height)}); rectangle.AddPoint({0, double(height)});
  }
  auto* region = RasterizeShape(widget, window, explicit_polygon ? shadow->polygon : rectangle);
  ApplyInputRegion(widget, window, region);
  cairo_region_destroy(region);
  g_object_set_data(G_OBJECT(window), kInputShapeKey, explicit_polygon ? GINT_TO_POINTER(1) : nullptr);
}

bool Window::SetShape(std::shared_ptr<WindowShape> shape) {
  GdkWindow* window = pimpl_->gdk_window_;
  if (!window || gdk_window_is_destroyed(window) || !IsShapeSupported()) return false;
  if (!shape) {
    gdk_window_shape_combine_region(window, nullptr, 0, 0);
    ApplyInputRegion(pimpl_->widget_, window, nullptr);
    gdk_window_invalidate_rect(window, nullptr, TRUE);
    return true;
  }
  if (shape->GetPointCount() < 3 || GetTitleBarStyle() != TitleBarStyle::Hidden) return false;
  cairo_region_t* region = RasterizeShape(pimpl_->widget_, window, *shape);
  const bool ok = cairo_region_status(region) == CAIRO_STATUS_SUCCESS;
  if (ok) {
    gdk_window_shape_combine_region(window, region, 0, 0);
    ApplyInputRegion(pimpl_->widget_, window, region);
    // GDK submits the bounding shape during its next paint update. Shrinking
    // a region exposes no new pixels, so it may otherwise schedule no paint
    // and leave the X server using the previous visual contour.
    gdk_window_invalidate_rect(window, nullptr, TRUE);
  }
  cairo_region_destroy(region);
  return ok;
}

bool Window::IsInputShapeSupported() {
  GdkDisplay* display = gdk_display_get_default();
  return display && gdk_display_supports_input_shapes(display);
}

bool Window::SetInputShape(std::shared_ptr<WindowShape> shape) {
  GdkWindow* window = pimpl_->gdk_window_;
  if (!window || gdk_window_is_destroyed(window) || !IsInputShapeSupported()) return false;
  if (!shape) {
    linux_shadow::SetPolygon(pimpl_->widget_, nullptr);
    if (linux_shadow::Get(pimpl_->widget_)) RefreshShadowInput(pimpl_->widget_);
    else ApplyInputRegion(pimpl_->widget_, window, nullptr);
    return true;
  }
  if (shape->GetPointCount() < 3 || GetTitleBarStyle() != TitleBarStyle::Hidden) return false;
  cairo_region_t* region = RasterizeShape(pimpl_->widget_, window, *shape);
  const bool ok = cairo_region_status(region) == CAIRO_STATUS_SUCCESS;
  if (ok) {
    ApplyInputRegion(pimpl_->widget_, window, region);
    linux_shadow::SetPolygon(pimpl_->widget_, shape);
  }
  cairo_region_destroy(region);
  return ok;
}

bool Window::IsInputShaped() const {
  return pimpl_->gdk_window_ && !gdk_window_is_destroyed(pimpl_->gdk_window_) &&
         g_object_get_data(G_OBJECT(pimpl_->gdk_window_), kInputShapeKey) != nullptr;
}

bool Window::IsShaped() const {
  return pimpl_->gdk_window_ && !gdk_window_is_destroyed(pimpl_->gdk_window_) &&
         gdk_window_is_shaped(pimpl_->gdk_window_);
}

}  // namespace nativeapi

namespace nativeapi {
bool Window::SetTitleBarColors(const Color&, const Color&) { return false; }
bool Window::ResetTitleBarColors() { return false; }
}
