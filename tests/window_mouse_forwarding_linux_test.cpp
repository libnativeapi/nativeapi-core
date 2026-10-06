// Real GDK/GTK event delivery while the X server hits the window underneath.
// Pointer movement is confined to an explicitly private Xvfb display.
#include "nativeapi.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
struct Events { int moves = 0, enters = 0, leaves = 0, buttons = 0; double x = 0, y = 0; };
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Settle() {
  const auto end = g_get_monotonic_time() + 120000;
  while (g_get_monotonic_time() < end) {
    while (gtk_events_pending()) gtk_main_iteration();
    g_usleep(1000);
  }
}
GtkWidget* Area(Events* events) {
  auto* area = gtk_drawing_area_new();
  gtk_widget_add_events(area, GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK |
                       GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK);
  g_signal_connect(area, "motion-notify-event", G_CALLBACK(+[](GtkWidget*, GdkEventMotion* e, gpointer data) -> gboolean {
    auto* events = static_cast<Events*>(data); ++events->moves; events->x = e->x; events->y = e->y;
    return TRUE;
  }), events);
  g_signal_connect(area, "enter-notify-event", G_CALLBACK(+[](GtkWidget*, GdkEventCrossing*, gpointer data) -> gboolean {
    ++static_cast<Events*>(data)->enters; return TRUE;
  }), events);
  g_signal_connect(area, "leave-notify-event", G_CALLBACK(+[](GtkWidget*, GdkEventCrossing*, gpointer data) -> gboolean {
    ++static_cast<Events*>(data)->leaves; return TRUE;
  }), events);
  g_signal_connect(area, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer data) -> gboolean {
    ++static_cast<Events*>(data)->buttons; return TRUE;
  }), events);
  return area;
}
void Move(GtkWidget* content, int x, int y) {
  auto* top = gtk_widget_get_toplevel(content);
  auto* surface = gtk_widget_get_window(top);
  auto* display = GDK_WINDOW_XDISPLAY(surface);
  const int scale = gdk_window_get_scale_factor(surface);
  int origin_x = 0, origin_y = 0; ::Window child = 0;
  XTranslateCoordinates(display, GDK_WINDOW_XID(surface), DefaultRootWindow(display),
                        0, 0, &origin_x, &origin_y, &child);
  gint local_x = 0, local_y = 0;
  gtk_widget_translate_coordinates(content, top, x, y, &local_x, &local_y);
  XWarpPointer(display, 0, DefaultRootWindow(display), 0, 0, 0, 0,
               origin_x + local_x * scale, origin_y + local_y * scale);
  XSync(display, False); Settle();
}
}
int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--wayland") {
    const char* private_wayland = std::getenv("NATIVEAPI_TEST_PRIVATE_WAYLAND");
    if (!private_wayland || std::string(private_wayland) != "1" || !gtk_init_check(&argc, &argv)) return 77;
    nativeapi::Application::GetInstance();
    auto* host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_add(GTK_CONTAINER(host), gtk_label_new("private Wayland input policy"));
    nativeapi::Window window(host);
    gtk_widget_show_all(host); window.ShowInactive(); Settle();
    const auto handle = native_window_create_with_native_window(host);
    Check(!nativeapi::Window::IsMouseMoveForwardingSupported() &&
          !native_window_is_mouse_move_forwarding_supported(), "Wayland forwarding unsupported");
    Check(window.SetIgnoreMouseEvents(true) && window.IsIgnoreMouseEvents(), "Wayland input pass-through");
    Check(!window.SetIgnoreMouseEvents(true, true) && window.IsIgnoreMouseEvents() &&
          !window.IsMouseMoveForwardingEnabled(), "unsupported request preserves enabled pass-through");
    Check(native_window_set_ignore_mouse_events(handle, false, true) && !window.IsIgnoreMouseEvents(),
          "disabling pass-through ignores forward flag on Wayland");
    Check(!native_window_set_ignore_mouse_events(handle, true, true) && !window.IsIgnoreMouseEvents(),
          "unsupported C ABI request preserves disabled pass-through");
    gtk_widget_destroy(host); Settle();
    Check(!window.SetIgnoreMouseEvents(true) && !window.IsMouseMoveForwardingEnabled(), "destroyed Wayland policy rejected");
    native_window_free(handle);
    std::cout << (failures ? "FAILED" : "ALL PASS") << std::endl;
    return failures ? 1 : 0;
  }
  const char* isolated = std::getenv("NATIVEAPI_TEST_PRIVATE_X11");
  if (!isolated || std::string(isolated) != "1" || !gtk_init_check(&argc, &argv)) return 77;
  nativeapi::Application::GetInstance();
  auto* below = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_decorated(GTK_WINDOW(below), FALSE);
  gtk_window_move(GTK_WINDOW(below), 80, 90);
  gtk_window_set_default_size(GTK_WINDOW(below), 240, 120);
  gtk_container_add(GTK_CONTAINER(below), gtk_label_new("underlying window"));
  gtk_widget_show_all(below); Settle();
  auto* host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(host), 240, 120);
  Events a, b;
  auto* first = Area(&a); auto* second = Area(&b);
  auto* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(box), first, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(box), second, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(host), box);
  nativeapi::Window window(host);
  window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden); window.SetHasShadow(false);
  window.SetPosition({80, 90});
  gtk_widget_show_all(host); window.ShowInactive(); Settle();
  Move(first, -50, -50);
  Check(nativeapi::Window::IsMouseMoveForwardingSupported(), "X11 movement-forwarding capability");
  Check(window.SetIgnoreMouseEvents(true, true) && window.IsMouseMoveForwardingEnabled(), "enable forwarding");
  Move(first, 25, 35);
  Check(a.moves > 0 && a.enters > 0 && b.moves == 0, "GTK child receives forwarded movement and enter");
  Check(std::abs(a.x - 25) < 0.1 && std::abs(a.y - 35) < 0.1, "child-local logical coordinates");
  auto* surface = gtk_widget_get_window(host); auto* display = GDK_WINDOW_XDISPLAY(surface);
  ::Window root = 0, hit = 0; int rx = 0, ry = 0, wx = 0, wy = 0; unsigned int mask = 0;
  XQueryPointer(display, DefaultRootWindow(display), &root, &hit, &rx, &ry, &wx, &wy, &mask);
  Check(hit == GDK_WINDOW_XID(gtk_widget_get_window(below)), "server pointer hit goes to window underneath");
  const int moves = a.moves; Settle(); Check(a.moves == moves, "stationary pointer emits no duplicate movement");
  Move(second, 25, 45);
  Check(a.leaves > 0 && b.enters > 0 && b.moves > 0, "nested target transition forwards exit and enter");
  Check(std::abs(b.x - 25) < 0.1 && std::abs(b.y - 45) < 0.1, "second child-local coordinates");
  Move(second, 250, 45); Check(b.leaves > 0, "leaving overlay forwards exit");
  Check(a.buttons == 0 && b.buttons == 0, "no synthetic button events");
  const auto handle = native_window_create_with_native_window(host);
  Check(native_window_is_mouse_move_forwarding_enabled(handle), "policy shared with C ABI wrapper");
  Check(native_window_set_ignore_mouse_events(handle, true, false) && !window.IsMouseMoveForwardingEnabled(),
        "disable forwarding while retaining pass-through");
  const int previous = a.moves; Move(first, 35, 45);
  Check(a.moves == previous && window.IsIgnoreMouseEvents(), "plain pass-through does not forward");
  Check(native_window_set_ignore_mouse_events(handle, false, true) && !window.IsMouseMoveForwardingEnabled(),
        "disable pass-through ignores forward flag");
  Check(window.SetIgnoreMouseEvents(true, true), "re-enable forwarding");
  native_window_free(handle); Move(first, 55, 65);
  Check(a.moves > previous && window.IsMouseMoveForwardingEnabled(), "sampler survives wrapper release");
  gtk_widget_destroy(host); Settle();
  Check(!window.IsIgnoreMouseEvents() && !window.IsMouseMoveForwardingEnabled(), "destroy stops sampler");
  gtk_widget_destroy(below); Settle();
  std::cout << (failures ? "FAILED" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
}
