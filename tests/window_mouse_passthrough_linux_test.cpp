// Checks server-side input regions in a private Xvfb session. No input sent.
#include "nativeapi.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/shape.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Settle() {
  const auto end = g_get_monotonic_time() + 100000;
  while (g_get_monotonic_time() < end) {
    while (gtk_events_pending()) gtk_main_iteration();
    g_usleep(1000);
  }
}
std::vector<XRectangle> Input(GtkWidget* widget) {
  auto* surface = gtk_widget_get_window(widget);
  auto* display = GDK_WINDOW_XDISPLAY(surface);
  XSync(display, False);
  int count = 0, order = 0;
  auto* rectangles = XShapeGetRectangles(display, GDK_WINDOW_XID(surface), ShapeInput,
                                        &count, &order);
  std::vector<XRectangle> result;
  if (rectangles) {
    result.assign(rectangles, rectangles + count);
    XFree(rectangles);
  }
  return result;
}
bool Same(const std::vector<XRectangle>& a, const std::vector<XRectangle>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].x != b[i].x || a[i].y != b[i].y ||
        a[i].width != b[i].width || a[i].height != b[i].height) return false;
  return true;
}
std::shared_ptr<nativeapi::WindowShape> Rectangle(int size) {
  auto shape = std::make_shared<nativeapi::WindowShape>();
  shape->AddPoint({0, 0}); shape->AddPoint({double(size), 0});
  shape->AddPoint({double(size), double(size)}); shape->AddPoint({0, double(size)});
  return shape;
}
}

int main(int argc, char** argv) {
  const char* private_x11 = std::getenv("NATIVEAPI_TEST_PRIVATE_X11");
  if (!private_x11 || std::string(private_x11) != "1" || !gtk_init_check(&argc, &argv)) return 77;
  nativeapi::Application::GetInstance();
  for (bool shadow : {false, true}) {
    auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    auto* content = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(widget), content);
    gtk_window_set_titlebar(GTK_WINDOW(widget), gtk_header_bar_new());
    gtk_window_set_default_size(GTK_WINDOW(widget), 180, 140);
    nativeapi::Window window(widget);
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    window.SetHasShadow(shadow);
    window.SetIgnoreMouseEvents(true);
    gtk_widget_show_all(widget);
    window.ShowInactive();
    Settle();
    Check(window.IsIgnoreMouseEvents() && Input(widget).empty(), "ignore before first map");
    window.SetIgnoreMouseEvents(false);
    Settle();
    const auto initial = Input(widget);
    Check(!initial.empty() && !window.IsIgnoreMouseEvents(), "normal input region");
    const auto handle = native_window_create_with_native_window(widget);
    window.SetIgnoreMouseEvents(true);
    Settle();
    Check(window.IsIgnoreMouseEvents() && native_window_is_ignore_mouse_events(handle),
          "ignore state shared by native wrappers");
    Check(Input(widget).empty(), "server input region empty");
    window.SetContentSize({230, 160});
    Settle();
    Check(Input(widget).empty(), "resize and shadow allocation cannot restore input");
    window.Hide(); window.ShowInactive(); Settle();
    Check(Input(widget).empty(), "remap preserves pass-through");
    native_window_set_ignore_mouse_events(handle, false, false);
    Settle();
    Check(!window.IsIgnoreMouseEvents() && !Input(widget).empty(), "C ABI restores input");
    Check(window.SetInputShape(Rectangle(70)), "set polygon input shape");
    Settle();
    const auto shaped = Input(widget);
    Check(!shaped.empty() && window.IsInputShaped(), "polygon active");
    window.SetIgnoreMouseEvents(true); Settle();
    Check(Input(widget).empty(), "polygon also passes through");
    window.SetIgnoreMouseEvents(false); Settle();
    Check(Same(shaped, Input(widget)), "original polygon restored exactly");
    window.SetIgnoreMouseEvents(true);
    Check(window.SetInputShape(Rectangle(45)), "change polygon while ignored");
    Settle();
    Check(Input(widget).empty(), "shape change cannot re-enable input");
    window.SetIgnoreMouseEvents(false); Settle();
    Check(!Same(shaped, Input(widget)) && !Input(widget).empty(), "latest polygon restored");
    window.SetIgnoreMouseEvents(true);
    Check(window.SetInputShape(nullptr), "clear polygon while ignored");
    Settle();
    Check(Input(widget).empty(), "clearing polygon keeps input ignored");
    window.SetIgnoreMouseEvents(false); Settle();
    Check(!window.IsInputShaped() && !Input(widget).empty(), "clear restores default input");
    gtk_widget_destroy(widget); Settle();
    Check(!window.IsIgnoreMouseEvents() && !native_window_is_ignore_mouse_events(handle),
          "destroyed wrappers reject state");
    window.SetIgnoreMouseEvents(true);
    native_window_free(handle);
  }
  Check(!native_window_is_ignore_mouse_events(0), "invalid C handle");
  std::cout << (failures ? "FAILED" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
}
