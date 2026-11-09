// Real GTK allocations and the production gutter helper. Only the GDK write is
// observed; the wrapper forwards every declaration to the real backend.
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#include <iostream>
#include <vector>

namespace {
struct Declaration {
  GdkWindow* window;
  int left, right, top, bottom;
};
std::vector<Declaration> declarations;
void DeclareShadowWidth(GdkWindow* window, int left, int right, int top, int bottom) {
  declarations.push_back({window, left, right, top, bottom});
  gdk_window_set_shadow_width(window, left, right, top, bottom);
}
}

#define gdk_window_set_shadow_width DeclareShadowWidth
#include "../src/platform/linux/window_shadow_linux.h"
#undef gdk_window_set_shadow_width

// Input-region synchronization is independent of gutter declarations. Keep the
// renderer/helper test independent of the full Window implementation.
namespace nativeapi {
static void RefreshShadowInput(GtkWidget*) {}
static void ClearShadowInput(GtkWidget*) {}
}

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
  std::cout << (ok ? "PASS " : "FAIL ") << message << std::endl;
  failures += !ok;
}
// Always return at the deadline, including when a regression keeps GTK pending.
void Pump(int milliseconds) {
  const auto end = g_get_monotonic_time() + milliseconds * 1000;
  while (g_get_monotonic_time() < end) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(1000);
  }
}
GtkWidget* MakeWindow(GtkWidget** content) {
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widget), "NativeAPI shadow gutter regression");
  gtk_window_set_decorated(GTK_WINDOW(widget), FALSE);
  gtk_window_set_default_size(GTK_WINDOW(widget), 480, 360);
  *content = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(widget), *content);
  return widget;
}
bool SkipUnsupportedCsd(GtkWidget* widget, const char* test_name) {
  if (gdk_screen_is_composited(gtk_widget_get_screen(widget)) &&
      gtk_style_context_has_class(gtk_widget_get_style_context(widget), "csd"))
    return false;
  std::cout << "SKIP " << test_name
            << ": compositor-supported CSD is unavailable" << std::endl;
  gtk_widget_destroy(widget);
  return true;
}
void AllocateContent(GtkWidget* widget, GtkWidget* content,
                     int left, int right, int top, int bottom) {
  GtkAllocation allocation{left, top,
      gtk_widget_get_allocated_width(widget) - left - right,
      gtk_widget_get_allocated_height(widget) - top - bottom};
  gtk_widget_size_allocate(content, &allocation);
}
bool LastEquals(int left, int right, int top, int bottom) {
  if (declarations.empty()) return false;
  const auto& last = declarations.back();
  return last.left == left && last.right == right &&
         last.top == top && last.bottom == bottom;
}
void RepeatedDeclarations() {
  GtkWidget* content = nullptr;
  auto* widget = MakeWindow(&content);
  gtk_widget_show_all(widget);
  Pump(200);
  Check(!gtk_style_context_has_class(gtk_widget_get_style_context(widget), "csd"),
        "fallback fixture has no client-side decoration");
  nativeapi::linux_shadow::Ensure(widget);

  AllocateContent(widget, content, 11, 17, 23, 29);
  declarations.clear();
  for (int i = 0; i < 12; ++i)
    nativeapi::linux_shadow::DeclareGutter(widget);
  Check(declarations.size() == 1, "unchanged allocations declare shadow widths once");
  Check(LastEquals(11, 17, 23, 29), "asymmetric shadow extents reach real GDK unchanged");

  // Every component must participate in the equality check.
  for (const auto& extents : {Declaration{nullptr, 13, 17, 23, 29},
                              Declaration{nullptr, 13, 19, 23, 29},
                              Declaration{nullptr, 13, 19, 31, 29},
                              Declaration{nullptr, 13, 19, 31, 37}}) {
    AllocateContent(widget, content, extents.left, extents.right,
                    extents.top, extents.bottom);
    const auto before = declarations.size();
    nativeapi::linux_shadow::DeclareGutter(widget);
    nativeapi::linux_shadow::DeclareGutter(widget);
    Check(declarations.size() == before + 1 &&
              LastEquals(extents.left, extents.right, extents.top, extents.bottom),
          "changed extent is declared once, then becomes idempotent");
  }

  // A compositor/display scale change can invalidate native geometry without
  // changing the logical extents. Exercise the real GObject notification hook.
  g_object_notify(G_OBJECT(widget), "scale-factor");
  const auto before_scale = declarations.size();
  nativeapi::linux_shadow::DeclareGutter(widget);
  nativeapi::linux_shadow::DeclareGutter(widget);
  Check(declarations.size() == before_scale + 1 && LastEquals(13, 19, 31, 37),
        "scale notification re-declares unchanged logical extents once");

  const auto valid = declarations.size();
  for (const auto& extents : {Declaration{nullptr, -1, 19, 31, 37},
                              Declaration{nullptr, 13, -1, 31, 37},
                              Declaration{nullptr, 13, 19, -1, 37},
                              Declaration{nullptr, 13, 19, 31, -1}}) {
    AllocateContent(widget, content, extents.left, extents.right,
                    extents.top, extents.bottom);
    nativeapi::linux_shadow::DeclareGutter(widget);
  }
  Check(declarations.size() == valid, "negative extents are not sent to GDK");

  gtk_widget_hide(content);
  const auto hidden = declarations.size();
  nativeapi::linux_shadow::DeclareGutter(widget);
  Check(declarations.size() == hidden, "hidden content does not declare a gutter");
  gtk_widget_show(content);
  AllocateContent(widget, content, 13, 19, 31, 37);
  nativeapi::linux_shadow::DeclareGutter(widget);
  Check(declarations.size() == hidden, "skipping hidden content preserves last declaration");

  // Keep the old GdkWindow alive, so the replacement cannot reuse its address.
  auto* old_window = gtk_widget_get_window(widget);
  g_object_ref(old_window);
  gtk_widget_hide(widget);
  gtk_widget_unrealize(widget);
  const auto unrealized = declarations.size();
  nativeapi::linux_shadow::DeclareGutter(widget);
  Check(declarations.size() == unrealized, "unrealized window does not declare a gutter");
  gtk_widget_realize(widget);
  gtk_widget_realize(content);
  Check(gtk_widget_get_window(widget) != old_window, "realize creates a new GdkWindow");
  AllocateContent(widget, content, 13, 19, 31, 37);
  const auto recreated = declarations.size();
  nativeapi::linux_shadow::DeclareGutter(widget);
  nativeapi::linux_shadow::DeclareGutter(widget);
  Check(declarations.size() == recreated + 1 && LastEquals(13, 19, 31, 37),
        "new GdkWindow receives identical extents once");
  g_object_unref(old_window);
  gtk_widget_destroy(widget);
}
void AllocationQuiescence() {
  const auto before_csd = declarations.size();
  GtkWidget* content = nullptr;
  auto* widget = MakeWindow(&content);
  // Match a realized CSD host with a hidden header, as used by Flutter.
  gtk_window_set_decorated(GTK_WINDOW(widget), TRUE);
  auto* header = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(widget), header);
  gtk_widget_realize(widget);
  gtk_widget_hide(header);
  gtk_widget_realize(content);
  if (SkipUnsupportedCsd(widget, "AllocationQuiescence"))
    return;
  nativeapi::linux_shadow::Ensure(widget);
  // The application requests its content dimensions after installing the
  // hidden-title-bar shadow policy, before mapping the native host.
  gtk_window_resize(GTK_WINDOW(widget), 480, 360);
  int allocations = 0;
  g_signal_connect(widget, "size-allocate", G_CALLBACK(+[](GtkWidget*, GtkAllocation*, gpointer data) {
    ++*static_cast<int*>(data);
  }), &allocations);
  gtk_widget_show(content);
  gtk_widget_show(widget);
  Pump(900);
  Check(declarations.size() == before_csd,
        "GTK owns CSD shadow geometry without custom GDK writes");
  const auto initial_allocations = allocations;
  const auto initial_declarations = declarations.size();
  Pump(300);
  std::cout << "idle: allocations=" << allocations - initial_allocations
            << " declarations=" << declarations.size() - initial_declarations << std::endl;
  Check(allocations == initial_allocations, "static mapped window stops allocating");
  Check(declarations.size() == initial_declarations, "static mapped window stops declaring shadow widths");

  const auto initial_width = gtk_widget_get_allocated_width(content);
  const auto initial_height = gtk_widget_get_allocated_height(content);
  std::cout << "initial mapped CSD content=" << initial_width << "x" << initial_height
            << " border=" << gtk_container_get_border_width(GTK_CONTAINER(widget))
            << std::endl;
  bool wayland = false;
#ifdef GDK_WINDOWING_WAYLAND
  wayland = GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget));
#endif
  if (wayland)
    Check(initial_width == 480 && initial_height == 360,
          "Wayland first map preserves requested content dimensions");
  else
    std::cout << "INFO X11 initial CSD size is informational; exact sizing is checked after a settled resize"
              << std::endl;
  Check(nativeapi::linux_shadow::Gutter(widget) == 0,
        "GTK content dimensions need no additional CSD gutter subtraction");

  // X11 already has different pre-map CSD/default-size accounting. Both
  // backends must honor an explicit content request once the window is settled.
  gtk_window_resize(GTK_WINDOW(widget), 480, 360);
  Pump(500);
  const auto width = gtk_widget_get_allocated_width(content);
  const auto height = gtk_widget_get_allocated_height(content);
  std::cout << "settled requested CSD content=" << width << "x" << height << std::endl;
  Check(width == 480 && height == 360,
        "settled content resize produces the exact requested dimensions");
  gtk_widget_hide(widget);
  Pump(50);
  gtk_widget_show(widget);
  Pump(600);
  const auto remapped_allocations = allocations;
  const auto remapped_declarations = declarations.size();
  Pump(300);
  Check(allocations == remapped_allocations && declarations.size() == remapped_declarations,
        "remapped window returns to an idle layout");
  Check(gtk_widget_get_allocated_width(content) == width &&
            gtk_widget_get_allocated_height(content) == height,
        "hide/show preserves content geometry");

  // GTK retains the CSD class across direct decoration toggles. It must
  // continue owning the geometry even while decorations are temporarily off.
  for (bool decorated : {false, true}) {
    gtk_window_set_decorated(GTK_WINDOW(widget), decorated);
    Pump(500);
    const auto toggled_allocations = allocations;
    const auto toggled_declarations = declarations.size();
    Pump(300);
    Check(allocations == toggled_allocations &&
              declarations.size() == toggled_declarations &&
              declarations.size() == before_csd,
          "direct decoration toggle settles without custom CSD declarations");
  }
  Check(gtk_widget_get_allocated_width(content) == width &&
            gtk_widget_get_allocated_height(content) == height,
        "restoring CSD decorations restores original content geometry");

  nativeapi::linux_shadow::Remove(widget);
  gtk_widget_show(header);
  Pump(500);
  const auto restored_allocations = allocations;
  Pump(300);
  Check(allocations == restored_allocations,
        "restoring system decorations returns to an idle layout");
  Check(!nativeapi::linux_shadow::Get(widget) &&
            !gtk_style_context_has_class(gtk_widget_get_style_context(widget),
                                        nativeapi::linux_shadow::kGutterStyleClass) &&
            !gtk_style_context_has_class(gtk_widget_get_style_context(widget),
                                        "nativeapi-shadow-reserved"),
        "restoring system decorations removes core shadow state and style");
  gtk_widget_destroy(widget);
}
void MappedCsdContent() {
  GtkWidget* content = nullptr;
  auto* widget = MakeWindow(&content);
  gtk_window_set_decorated(GTK_WINDOW(widget), TRUE);
  gtk_window_set_titlebar(GTK_WINDOW(widget), gtk_header_bar_new());
  gtk_widget_show(content);
  gtk_widget_show(widget);
  Pump(500);
  if (SkipUnsupportedCsd(widget, "MappedCsdContent"))
    return;
  gtk_window_resize(GTK_WINDOW(widget), 480, 360);
  Pump(500);
  Check(gtk_style_context_has_class(gtk_widget_get_style_context(widget), "csd") &&
            gtk_widget_get_allocated_width(content) == 480 &&
            gtk_widget_get_allocated_height(content) == 360,
        "mapped CSD fixture starts with exact content dimensions");
  nativeapi::linux_shadow::Ensure(widget);
  Pump(600);  // Include the deferred gutter decision, without another resize.
  std::cout << "mapped CSD after installing gutter=" << gtk_widget_get_allocated_width(content)
            << "x" << gtk_widget_get_allocated_height(content) << std::endl;
  Check(gtk_widget_get_allocated_width(content) == 480 &&
            gtk_widget_get_allocated_height(content) == 360,
        "installing a gutter on mapped CSD preserves content dimensions without a resize request");
  gtk_widget_destroy(widget);
}
}

int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) return 77;
  RepeatedDeclarations();
  AllocationQuiescence();
  MappedCsdContent();
  std::cout << (failures ? "FAILED " : "ALL PASS ") << failures << std::endl;
  return failures ? 1 : 0;
}
