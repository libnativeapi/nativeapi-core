// Desktop integration test: native properties, CSD controls and X11 WM hints.
#include "../src/window.h"

#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Settle() {
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
  while (std::chrono::steady_clock::now() < end) {
    while (gtk_events_pending())
      gtk_main_iteration();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
GtkWidget* Header(GtkWidget* widget) {
  if (GTK_IS_HEADER_BAR(widget))
    return widget;
  GtkWidget* found = nullptr;
  if (GTK_IS_CONTAINER(widget)) {
    gtk_container_forall(
        GTK_CONTAINER(widget),
        +[](GtkWidget* child, gpointer data) {
          auto** result = static_cast<GtkWidget**>(data);
          if (!*result)
            *result = Header(child);
        },
        &found);
  }
  return found;
}
void CheckFunctions(GtkWidget* widget, unsigned long expected) {
  auto* surface = gtk_widget_get_window(widget);
  if (!GDK_IS_X11_WINDOW(surface))
    return;
  auto* display = GDK_WINDOW_XDISPLAY(surface);
  Atom property = XInternAtom(display, "_MOTIF_WM_HINTS", False);
  Atom type = None;
  int format = 0;
  unsigned long count = 0, remaining = 0;
  unsigned char* bytes = nullptr;
  const int status =
      XGetWindowProperty(display, GDK_WINDOW_XID(surface), property, 0, 5, False, AnyPropertyType,
                         &type, &format, &count, &remaining, &bytes);
  auto* hints = reinterpret_cast<unsigned long*>(bytes);
  Check(status == Success && format == 32 && count >= 2 && (hints[0] & 1) && hints[1] == expected,
        "combined Motif function hints");
  if (bytes)
    XFree(bytes);
}
void Run(bool explicit_header) {
  std::cout << "CASE explicit_header=" << explicit_header << std::endl;
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widget), "nativeapi control regression");
  gtk_container_add(GTK_CONTAINER(widget), gtk_drawing_area_new());
  if (explicit_header) {
    auto* header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header), "menu:minimize,maximize,close");
    gtk_widget_show(header);
    gtk_window_set_titlebar(GTK_WINDOW(widget), header);
  }
  {
    nativeapi::Window window(widget);
    window.SetContentSize({600, 400});
    Check(window.IsClosable() && window.IsMovable() && window.IsMinimizable() &&
              window.IsMaximizable(),
          "default capabilities");
    window.SetMovable(false);
    window.SetMinimizable(false);
    window.SetMaximizable(false);
    window.SetClosable(false);
    gtk_widget_show_all(widget);
    Settle();
    Check(!window.IsClosable() && !gtk_window_get_deletable(GTK_WINDOW(widget)),
          "close control disabled");
    Check(!window.IsMovable() && !window.IsMinimizable() && !window.IsMaximizable(),
          "requested capabilities disabled");
    CheckFunctions(widget, GDK_FUNC_RESIZE);
    auto* header = Header(widget);
    if (header) {
      auto* layout = gtk_header_bar_get_decoration_layout(GTK_HEADER_BAR(header));
      Check(layout && !strstr(layout, "minimize") && !strstr(layout, "maximize"),
            "CSD min/max buttons removed");
    }
    nativeapi::Window other(widget);
    Check(!other.IsMovable() && !other.IsMinimizable() && !other.IsMaximizable() &&
              !other.IsClosable(),
          "wrappers share native policy");
    window.SetClosable(true);
    Settle();
    CheckFunctions(widget, GDK_FUNC_RESIZE | GDK_FUNC_CLOSE);
    window.SetResizable(false);
    Settle();
    CheckFunctions(widget, GDK_FUNC_CLOSE);
    window.SetResizable(true);
    window.SetMaximizable(true);
    Settle();
    CheckFunctions(widget, GDK_FUNC_RESIZE | GDK_FUNC_MAXIMIZE | GDK_FUNC_CLOSE);
    if (explicit_header) {
      Check(g_strcmp0(gtk_header_bar_get_decoration_layout(GTK_HEADER_BAR(header)),
                      "menu:maximize,close") == 0,
            "partial CSD layout restored");
    }
    window.SetMinimizable(true);
    window.SetMovable(true);
    Settle();
    CheckFunctions(widget, GDK_FUNC_RESIZE | GDK_FUNC_MOVE | GDK_FUNC_MINIMIZE | GDK_FUNC_MAXIMIZE |
                               GDK_FUNC_CLOSE);
    if (header) {
      Check(g_strcmp0(gtk_header_bar_get_decoration_layout(GTK_HEADER_BAR(header)),
                      explicit_header ? "menu:minimize,maximize,close" : nullptr) == 0,
            "original CSD layout restored");
    }
    Check(other.IsMovable() && other.IsMinimizable() && other.IsMaximizable() && other.IsClosable(),
          "other wrapper sees restored policy");
    // GTK setters also rewrite WM hints: ensure our other policies survive.
    window.SetMovable(false);
    gtk_window_set_deletable(GTK_WINDOW(widget), FALSE);
    Settle();
    CheckFunctions(widget, GDK_FUNC_RESIZE | GDK_FUNC_MINIMIZE | GDK_FUNC_MAXIMIZE);
    Check(!window.IsClosable(), "getter observes external GTK changes");
    gtk_widget_hide(widget);
    gtk_widget_show_all(widget);
    Settle();
    CheckFunctions(widget, GDK_FUNC_RESIZE | GDK_FUNC_MINIMIZE | GDK_FUNC_MAXIMIZE);
    window.SetMinimizable(false);
    window.SetMaximizable(false);
    Settle();
    window.Maximize();
    for (int i = 0; i < 5; ++i)
      Settle();
    Check(window.IsMaximized(), "programmatic maximize remains available");
    window.Unmaximize();
    Settle();
    CheckFunctions(widget, GDK_FUNC_RESIZE);
    window.Minimize();
    for (int i = 0; i < 5; ++i)
      Settle();
    if (GDK_IS_X11_WINDOW(gtk_widget_get_window(widget)))
      Check(window.IsMinimized(), "programmatic minimize remains available");
    else
      std::cout << "SKIP minimize state: Wayland does not report iconification" << std::endl;
    // Present the window again (also supplies activation on compositors that
    // ignore a bare deiconify request).
    window.Focus();
    for (int i = 0; i < 5; ++i) Settle();
    if (GDK_IS_X11_WINDOW(gtk_widget_get_window(widget)))
      Check(!window.IsMinimized(), "present restores minimized window");
    CheckFunctions(widget, GDK_FUNC_RESIZE);
    window.SetContentSize({650, 430});
    Settle();
    auto size = window.GetContentSize();
    Check(size.width == 650 && size.height == 430, "programmatic resizing remains available");
  }
  {
    nativeapi::Window other(widget);
    Check(!other.IsMovable() && !other.IsMinimizable() && !other.IsMaximizable(),
          "policy survives wrapper destruction");
  }
  // Disabling close is not a programmatic destruction veto.
  bool destroyed = false;
  g_signal_connect(widget, "destroy",
                   G_CALLBACK(+[](GtkWidget*, gpointer data) { *static_cast<bool*>(data) = true; }),
                   &destroyed);
  gtk_widget_destroy(widget);
  Check(destroyed, "programmatic destruction remains available");
  Settle();
}
}  // namespace
int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv))
    return 77;
  Run(false);
  Run(true);
  return failures ? 1 : 0;
}
