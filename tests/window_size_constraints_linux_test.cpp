// Desktop integration test: build the window_size_constraints_linux_test target
// and run it under a window manager with GDK_BACKEND=x11 or GDK_BACKEND=wayland.
// Uses real GTK windows and native geometry; sends no synthetic input.
#include "../src/window.h"

#include <X11/Xutil.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
void Check(bool condition, const char* label) {
  std::cout << (condition ? "PASS " : "FAIL ") << label << std::endl;
  failures += !condition;
}
void Settle() {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
  while (std::chrono::steady_clock::now() < end) {
    while (gtk_events_pending())
      gtk_main_iteration();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
void CheckSize(nativeapi::Window& window, double width, double height, const char* label) {
  auto size = window.GetSize();
  std::cout << "frame " << size.width << " x " << size.height << std::endl;
  Check(std::abs(size.width - width) <= 2 && std::abs(size.height - height) <= 2, label);
}
void CheckHints(GtkWidget* widget, long expected) {
  auto* surface = gtk_widget_get_window(widget);
  if (!GDK_IS_X11_WINDOW(surface))
    return;
  XSizeHints hints = {};
  long supplied = 0;
  const auto ok =
      XGetWMNormalHints(GDK_WINDOW_XDISPLAY(surface), GDK_WINDOW_XID(surface), &hints, &supplied);
  Check(ok && (hints.flags & (PMinSize | PMaxSize | PAspect)) == expected,
        "combined native hint flags");
}
void Run(bool csd, bool with_content = true) {
  std::cout << (csd ? "CSD" : "default decorations") << std::endl;
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widget), "nativeapi size constraint regression");
  if (with_content)
    gtk_container_add(GTK_CONTAINER(widget), gtk_drawing_area_new());
  if (csd) {
    auto* titlebar = gtk_header_bar_new();
    gtk_widget_show(titlebar);
    gtk_window_set_titlebar(GTK_WINDOW(widget), titlebar);
  }
  {
    nativeapi::Window window(widget);
    Check(window.GetMinimumSize().width == 0 && window.GetMaximumSize().width == -1,
          "default constraints");
    window.SetMinimumSize({400, 300});
    window.SetMaximumSize({800, 600});
    Check(window.GetMinimumSize().height == 300 && window.GetMaximumSize().height == 600,
          "constraint getters");
    window.SetContentSize(with_content ? nativeapi::Size{100, 100} : nativeapi::Size{1000, 900});
    gtk_widget_show_all(widget);
    Settle();
    auto initial = window.GetSize();
    Check(initial.width >= 400 && initial.height >= 300 && initial.width <= 800 &&
              initial.height <= 600,
          "constraints set before mapping");
    window.SetSize({1000, 900}, false);
    Settle();
    CheckSize(window, 800, 600, "maximum enforced");
    window.SetAspectRatio(1.5);
    Settle();
    CheckHints(widget, PMinSize | PMaxSize | PAspect);
    window.SetMinimumSize({420, 320});
    window.SetMaximumSize({820, 620});
    Settle();
    CheckHints(widget, PMinSize | PMaxSize | PAspect);
    window.SetAspectRatio(0);
    Settle();
    CheckHints(widget, PMinSize | PMaxSize);
    window.SetSize({100, 100}, false);
    Settle();
    CheckSize(window, 420, 320, "clearing aspect preserves minimum");
    window.SetSize({1000, 900}, false);
    Settle();
    CheckSize(window, 820, 620, "clearing aspect preserves maximum");
    // Some WMs disable maximization with a finite maximum. Exercise a real
    // maximize/restore cycle with the minimum, then install the maximum while
    // maximized so both hints must survive restoration.
    window.SetMaximumSize({-1, -1});
    window.Maximize();
    Settle();
    Check(window.IsMaximized(), "window maximized");
    window.SetMaximumSize({820, 620});
    window.Unmaximize();
    Settle();
    Check(!window.IsMaximized(), "window restored");
    window.SetSize({100, 100}, false);
    Settle();
    CheckSize(window, 420, 320, "minimum survives restore");
    window.SetSize({1000, 900}, false);
    Settle();
    CheckSize(window, 820, 620, "maximum survives restore");
    window.SetMaximumSize({700, -1});
    window.SetSize({1000, 750}, false);
    Settle();
    CheckSize(window, 700, 750, "independent unbounded height");
    window.SetMinimumSize({0, 0});
    window.SetMaximumSize({-1, -1});
    Settle();
    CheckHints(widget, PMinSize);  // GTK supplies the widget's natural minimum.
    window.SetSize({900, 700}, false);
    Settle();
    CheckSize(window, 900, 700, "constraints removed");
  }  // Disconnect callbacks while the native widget is still alive.
  gtk_window_resize(GTK_WINDOW(widget), 500, 400);
  Settle();
  {  // Also exercise native destruction before wrapper destruction.
    nativeapi::Window window(widget);
    window.SetMinimumSize({300, 200});
    gtk_widget_destroy(widget);
  }
  Settle();
}
}  // namespace

int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv))
    return 77;
  Run(false);
  Run(true);
  Run(false, false);  // Empty windows, as created by nativeapi itself.
  return failures ? 1 : 0;
}
