// Native GDK X11 protocol, in a private Xvfb session only. No input sent.
// --no-hint checks a WM without support. --wayland checks rejection without a gesture.
#include "nativeapi.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
int failures = 0;
void Check(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  failures += !ok;
}
void Settle() {
  const auto end = g_get_monotonic_time() + 300000;
  while (g_get_monotonic_time() < end) {
    while (gtk_events_pending()) gtk_main_iteration();
    g_usleep(1000);
  }
}
}  // namespace

int main(int argc, char** argv) {
  const bool wayland = argc > 1 && std::string(argv[1]) == "--wayland";
  const bool supported = !(argc > 1 && std::string(argv[1]) == "--no-hint");
  Display* observer = nullptr;
  ::Window root = 0, wm = 0;
  Atom menu_atom = 0;
  if (!wayland) {
    const char* isolated = std::getenv("NATIVEAPI_TEST_PRIVATE_X11");
    if (!isolated || std::string(isolated) != "1") return 77;
    observer = XOpenDisplay(nullptr);
    if (!observer) return 77;
    root = DefaultRootWindow(observer);
    wm = XCreateSimpleWindow(observer, root, 0, 0, 1, 1, 0, 0, 0);
    const Atom wm_check = XInternAtom(observer, "_NET_SUPPORTING_WM_CHECK", False);
    XChangeProperty(observer, root, wm_check, XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&wm), 1);
    XChangeProperty(observer, wm, wm_check, XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&wm), 1);
    menu_atom = XInternAtom(observer, "_GTK_SHOW_WINDOW_MENU", False);
    const Atom supported_atom = XInternAtom(observer, "_NET_SUPPORTED", False);
    XChangeProperty(observer, root, supported_atom, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&menu_atom), supported ? 1 : 0);
    XSelectInput(observer, root, SubstructureNotifyMask);
    XSync(observer, False);
  }
  if (!gtk_init_check(&argc, &argv)) return 77;
  nativeapi::Application::GetInstance();
  // Install CSD and content before realizing the window; GTK can otherwise
  // recreate its surface when a title bar is first installed.
  GtkWidget* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(content_box), gtk_label_new("system menu coordinate test"), TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(widget), content_box);
  gtk_window_set_titlebar(GTK_WINDOW(widget), gtk_header_bar_new());
  nativeapi::Window window(widget);
  Check(!window.ShowSystemMenu({10, 20}), "unmapped window rejects menu");
  window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
  window.SetHasShadow(false);
  window.SetPosition({80, 90});
  gtk_widget_show_all(widget);
  window.ShowInactive();
  Settle();
  Check(!window.ShowSystemMenu({std::numeric_limits<double>::quiet_NaN(), 0}) &&
        !window.ShowSystemMenu({1e100, 0}), "invalid position rejected");
  const auto handle = native_window_create_with_native_window(widget);
  Check(!native_window_show_system_menu(0, {10, 20}), "invalid C handle rejected");
  if (wayland) {
    Check(nativeapi::Window::IsSystemMenuSupported() && native_window_is_system_menu_supported(), "Wayland request capability");
    Check(!window.ShowSystemMenu({10, 20}) && !native_window_show_system_menu(handle, {10, 20}),
          "Wayland request without an owned active press rejected");
  } else {
    Check(nativeapi::Window::IsSystemMenuSupported() == supported &&
          native_window_is_system_menu_supported() == supported, "capability follows WM hint");
    for (int phase = 0; phase < (supported ? 3 : 1); ++phase) {
      if (phase == 1) {
        window.SetTitleBarStyle(nativeapi::TitleBarStyle::Normal);
        gtk_widget_show_all(widget);
        Settle();
      } else if (phase == 2) {
        window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
        window.SetHasShadow(true);
        Settle();
      }
      for (bool capi : {false, true}) {
      const nativeapi::Point position = {30, 40};
      const bool accepted = capi ? native_window_show_system_menu(handle, {30, 40}) : window.ShowSystemMenu(position);
      Check(accepted == supported, "request result follows WM support");
      if (supported) {
        GdkWindow* surface = gtk_widget_get_window(widget);
        const int scale = gdk_window_get_scale_factor(surface);
        int root_x = 0, root_y = 0;
        ::Window child_root = 0;
        XTranslateCoordinates(observer, gdk_x11_window_get_xid(surface), root, 0, 0,
                               &root_x, &root_y, &child_root);
        gint content_x = 0, content_y = 0;
        GtkWidget* content = gtk_bin_get_child(GTK_BIN(widget));
        if (content) {
          Check(gtk_widget_translate_coordinates(content, widget, 0, 0, &content_x, &content_y),
                "native GTK content origin measured independently");
        }
        XFlush(gdk_x11_display_get_xdisplay(gdk_window_get_display(surface)));
        XEvent event;
        bool found = false;
        const auto end = g_get_monotonic_time() + 1000000;
        while (g_get_monotonic_time() < end && !found) {
          while (XCheckTypedEvent(observer, ClientMessage, &event)) {
            if (event.xclient.message_type != menu_atom) continue;
            found = true;
            Check(event.xclient.window == gdk_x11_window_get_xid(surface) && event.xclient.format == 32,
                  "request names target native window");
            Check(event.xclient.data.l[0] > 0, "request identifies real pointer device");
            Check(event.xclient.data.l[1] == root_x + std::lround((content_x + position.x) * scale) &&
                  event.xclient.data.l[2] == root_y + std::lround((content_y + position.y) * scale),
                  "native request has content-relative root coordinates and scale");
          }
          g_usleep(1000);
        }
        Check(found, "native WM client message observed");
      }
      }
    }
  }
  gtk_widget_destroy(widget);
  Settle();
  Check(!window.ShowSystemMenu({10, 20}) && !native_window_show_system_menu(handle, {10, 20}),
        "destroyed native window rejects menu through live wrappers");
  native_window_free(handle);
  if (observer) { XDestroyWindow(observer, wm); XCloseDisplay(observer); }
  std::cout << (failures ? "FAILURES" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
}
