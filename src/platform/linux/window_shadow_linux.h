#pragma once

#include <gtk/gtk.h>
#include <memory>

#include "../window_shadow_blur.h"

namespace nativeapi {
static void RefreshShadowInput(GtkWidget* widget);
}

namespace nativeapi::linux_shadow {
constexpr char kConfig[] = "NativeAPICustomShadow";
constexpr char kState[] = "NativeAPICoreShadow";

// Reserve the complete supported blur/offset envelope once. Wayland cannot
// compensate for changing surface gutters with gtk_window_move(), and doing so
// also races asynchronous GTK allocations on X11. Raster only the actual shadow.
inline int ReservedMargin() {
  WindowShadow maximum;
  maximum.SetBlurRadius(64);
  maximum.SetOffset({64, 64});
  return window_shadow::Margin(maximum);
}

// States in which the window manager fits the whole surface to the space it gives the
// window - a tiling window manager's tiles, maximized, full screen. The gutter is then
// no shadow, only a band of empty window around the content, so it is taken away
// while the window is in one of them.
constexpr int kFittedStates =
    GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN | GDK_WINDOW_STATE_TILED |
    GDK_WINDOW_STATE_TOP_TILED | GDK_WINDOW_STATE_RIGHT_TILED |
    GDK_WINDOW_STATE_BOTTOM_TILED | GDK_WINDOW_STATE_LEFT_TILED;

struct State {
  GtkWidget* target = nullptr;
  GtkWidget* overlay = nullptr;
  guint original_border = 0;
  int margin = ReservedMargin();
  std::shared_ptr<WindowShadow> custom;
  bool enabled = true;
  bool fitted = false;
  bool dirty = true;
  int width = 0, height = 0;
  WindowShape polygon;
  cairo_surface_t* image = nullptr;
  ~State() {
    if (image)
      cairo_surface_destroy(image);
  }
};
inline State* Get(GtkWidget* widget) {
  return widget ? static_cast<State*>(g_object_get_data(G_OBJECT(widget), kState)) : nullptr;
}
// The gutter the surface has around the content right now.
inline int Gutter(GtkWidget* widget) {
  auto* state = Get(widget);
  if (!state)
    return 0;
  return state->fitted ? static_cast<int>(state->original_border) : state->margin;
}
inline bool IsFitted(GtkWidget* widget) {
  auto* window = gtk_widget_get_window(widget);
  return window && (gdk_window_get_state(window) & kFittedStates);
}
inline void Path(cairo_t* cr, State* state, int width, int height) {
  if (!state->polygon.GetPointCount()) {
    cairo_rectangle(cr, 0, 0, width, height);
    return;
  }
  for (size_t i = 0; i < state->polygon.GetPointCount(); ++i) {
    auto point = state->polygon.GetPointAt(i);
    if (i)
      cairo_line_to(cr, point.x, point.y);
    else
      cairo_move_to(cr, point.x, point.y);
  }
  cairo_close_path(cr);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
}
inline gboolean Paint(GtkWidget* widget, cairo_t* cr, gpointer data, bool inside) {
  auto* state = static_cast<State*>(data);
  if (!state->enabled || state->fitted)
    return FALSE;
  GtkWidget* child = gtk_bin_get_child(GTK_BIN(widget));
  if (!child)
    return FALSE;
  int x = 0, y = 0;
  gtk_widget_translate_coordinates(child, widget, 0, 0, &x, &y);
  const int width = gtk_widget_get_allocated_width(child);
  const int height = gtk_widget_get_allocated_height(child);
  const auto options = state->custom ? *state->custom : WindowShadow{};
  const int kMargin = window_shadow::Margin(options);
  const int w = width + kMargin * 2, h = height + kMargin * 2;
  if (w <= 0 || h <= 0 || static_cast<int64_t>(w) * h > 16000000)
    return FALSE;
  if (state->dirty || width != state->width || height != state->height) {
    state->dirty = false;
    state->width = width;
    state->height = height;
    if (state->image)
      cairo_surface_destroy(state->image);
    state->image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(state->image) != CAIRO_STATUS_SUCCESS) {
      cairo_surface_destroy(state->image);
      state->image = nullptr;
      state->dirty = true;
      return FALSE;
    }
    auto pixels = window_shadow::Render(width, height, state->polygon, options);
    cairo_surface_flush(state->image);
    if (pixels.size() != static_cast<size_t>(w) * h)
      return FALSE;
    std::copy(pixels.begin(), pixels.end(),
              reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(state->image)));
    cairo_surface_mark_dirty(state->image);
  }
  cairo_save(cr);
  // GTK invokes this after content painting. Only draw outside the content path,
  // so an opaque or translucent application never gets darkened by its shadow.
  if (!inside) cairo_translate(cr, x, y);
  cairo_new_path(cr);
  cairo_rectangle(cr, -kMargin, -kMargin, w, h);
  if (!inside && state->overlay)
    cairo_rectangle(cr, 0, 0, width, height);
  else
    Path(cr, state, width, height);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_clip(cr);
  cairo_set_source_surface(cr, state->image, -kMargin, -kMargin);
  cairo_paint(cr);
  cairo_restore(cr);
  return FALSE;
}
inline gboolean Draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
  return Paint(widget, cr, data, false);
}
inline gboolean DrawOverlay(GtkWidget*, cairo_t* cr, gpointer data) {
  auto* state = static_cast<State*>(data);
  return Paint(state->target, cr, data, true);
}
// The gutter is shadow, not window. Tell the window manager, so that it places,
// snaps and tiles the content rather than the whole surface (_GTK_FRAME_EXTENTS on
// X11, the xdg_surface window geometry on Wayland). GTK declares its own
// decoration's extents while allocating the window; this runs after it and
// declares everything around the content, GTK's part included.
inline void DeclareGutter(GtkWidget* widget) {
  auto* window = gtk_widget_get_window(widget);
  auto* child = gtk_bin_get_child(GTK_BIN(widget));
  int x = 0, y = 0;
  if (!window || !child || !gtk_widget_get_visible(child) ||
      !gtk_widget_translate_coordinates(child, widget, 0, 0, &x, &y))
    return;
  const int right = gtk_widget_get_allocated_width(widget) - x -
                    gtk_widget_get_allocated_width(child);
  const int bottom = gtk_widget_get_allocated_height(widget) - y -
                     gtk_widget_get_allocated_height(child);
  if (x < 0 || y < 0 || right < 0 || bottom < 0)
    return;
  gdk_window_set_shadow_width(window, x, right, y, bottom);
}
inline void Allocated(GtkWidget* widget, GtkAllocation*, gpointer) {
  RefreshShadowInput(widget);
  DeclareGutter(widget);
}

// GtkWindow paints its background under the whole allocation, gutter included,
// which would hide the shadow behind an opaque band. A window that can be
// translucent gets this class, and one rule for the screen clears its background;
// the content paints its own.
constexpr char kGutterStyleClass[] = "nativeapi-shadow-gutter";
inline bool CanBeTranslucent(GtkWidget* widget) {
  auto* screen = gtk_widget_get_screen(widget);
  return gdk_screen_is_composited(screen) &&
         gtk_widget_get_visual(widget) == gdk_screen_get_rgba_visual(screen);
}
inline void ClearGutterBackground(GtkWidget* widget) {
  static bool installed = false;
  if (!installed) {
    installed = true;
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
                                    "window.nativeapi-shadow-gutter,"
                                    "window.nativeapi-shadow-gutter:backdrop {"
                                    "  background: none; }",
                                    -1, nullptr);
    gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(widget),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
  }
  gtk_style_context_add_class(gtk_widget_get_style_context(widget), kGutterStyleClass);
}
// The window manager has already sized the surface for the new state; changing the
// border only moves the content edge to the surface edge, or back in.
inline gboolean StateChanged(GtkWidget* widget, GdkEventWindowState* event, gpointer data) {
  auto* state = static_cast<State*>(data);
  const bool fitted = (event->new_window_state & kFittedStates) != 0;
  if (fitted != state->fitted) {
    state->fitted = fitted;
    gtk_container_set_border_width(GTK_CONTAINER(widget),
                                   fitted ? state->original_border : state->margin);
    gtk_widget_queue_draw(widget);
  }
  return FALSE;
}
inline void Remove(GtkWidget* widget) {
  auto* state = Get(widget);
  if (!state)
    return;
  const auto border = state->original_border;
  auto* child = gtk_bin_get_child(GTK_BIN(widget));
  const int width = child ? gtk_widget_get_allocated_width(child) : 0;
  const int height = child ? gtk_widget_get_allocated_height(child) : 0;
  g_signal_handlers_disconnect_by_data(widget, state);
  gtk_style_context_remove_class(gtk_widget_get_style_context(widget), kGutterStyleClass);
  if (state->overlay && child == state->overlay) {
    auto* content = gtk_bin_get_child(GTK_BIN(state->overlay));
    g_object_ref(content);
    gtk_container_remove(GTK_CONTAINER(state->overlay), content);
    gtk_container_remove(GTK_CONTAINER(widget), state->overlay);
    state->overlay = nullptr;
    gtk_container_add(GTK_CONTAINER(widget), content);
    g_object_unref(content);
  }
  gtk_container_set_border_width(GTK_CONTAINER(widget), border);
  if (width > 0 && height > 0)
    gtk_window_resize(GTK_WINDOW(widget), width + border * 2, height + border * 2);
  g_object_set_data(G_OBJECT(widget), kState, nullptr);
  gtk_widget_queue_draw(widget);
}
inline State* Ensure(GtkWidget* widget) {
  if (auto* state = Get(widget))
    return state;
  if (!widget || !GTK_IS_WINDOW(widget))
    return nullptr;
  auto* state = new State;
  state->target = widget;
  // Parent-window drawing is occluded by native GL child windows. A pass-through
  // GTK overlay paints the part of the core shadow inside the content rectangle;
  // the parent paints only the outer gutter, without double compositing.
  // That part exists only for a polygon shape, and moving the content into the
  // overlay unrealizes and realizes it again, which a realized host widget may
  // not survive: a Flutter FlView then never renders again - black on Hyprland,
  // and on GNOME its window is never shown at all. Such content stays where it
  // is; the parent then paints the whole shadow outside the content.
  auto* content = gtk_bin_get_child(GTK_BIN(widget));
  if (content && !gtk_widget_get_realized(content)) {
    g_object_ref(content);
    gtk_container_remove(GTK_CONTAINER(widget), content);
    state->overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(state->overlay), content);
    g_object_unref(content);
    auto* paint = gtk_drawing_area_new();
    gtk_widget_set_has_window(paint, FALSE);
    gtk_widget_set_hexpand(paint, TRUE);
    gtk_widget_set_vexpand(paint, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(state->overlay), paint);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(state->overlay), paint, TRUE);
    g_signal_connect(paint, "draw", G_CALLBACK(DrawOverlay), state);
    gtk_container_add(GTK_CONTAINER(widget), state->overlay);
    gtk_widget_show(paint);
    gtk_widget_show(state->overlay);
  }
  if (auto* config = static_cast<WindowShadow*>(g_object_get_data(G_OBJECT(widget), kConfig))) {
    state->custom = std::make_shared<WindowShadow>(*config);
  }
  state->original_border = gtk_container_get_border_width(GTK_CONTAINER(widget));
  state->fitted = IsFitted(widget);
  g_object_set_data_full(G_OBJECT(widget), kState, state,
                         [](gpointer p) { delete static_cast<State*>(p); });
  g_signal_connect_after(widget, "draw", G_CALLBACK(Draw), state);
  g_signal_connect_after(widget, "size-allocate", G_CALLBACK(Allocated), state);
  g_signal_connect(widget, "window-state-event", G_CALLBACK(StateChanged), state);
  if (CanBeTranslucent(widget))
    ClearGutterBackground(widget);
  if (state->fitted)
    return state;
  // The gutter belongs to GTK, not the application's render tree. Keeping it
  // reserved while disabled avoids geometry changes when toggling the shadow.
  int width = 0, height = 0;
  int x = 0, y = 0;
  gtk_window_get_position(GTK_WINDOW(widget), &x, &y);
  gtk_window_get_size(GTK_WINDOW(widget), &width, &height);
  gtk_container_set_border_width(GTK_CONTAINER(widget), state->margin);
  const int extra = state->margin - state->original_border;
  gtk_window_resize(GTK_WINDOW(widget), width + extra * 2, height + extra * 2);
  gtk_window_move(GTK_WINDOW(widget), x - extra, y - extra);
  return state;
}
inline void Configure(GtkWidget* widget, std::shared_ptr<WindowShadow> custom) {
  auto* state = Ensure(widget);
  if (!state) return;
  state->custom = std::move(custom);
  state->dirty = true;
  gtk_widget_queue_draw(widget);
}
inline void SetPolygon(GtkWidget* widget, const std::shared_ptr<WindowShape>& polygon) {
  auto* state = Get(widget);
  if (!state)
    return;
  state->polygon.Clear();
  if (polygon)
    for (size_t i = 0; i < polygon->GetPointCount(); ++i)
      state->polygon.AddPoint(polygon->GetPointAt(i));
  state->dirty = true;
  gtk_widget_queue_draw(widget);
}
}  // namespace nativeapi::linux_shadow
