// Desktop integration test; sends no synthetic input.
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
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Settle() {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(450);
  while (std::chrono::steady_clock::now() < end) {
    while (gtk_events_pending())
      gtk_main_iteration();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
void CheckSize(nativeapi::Size actual, nativeapi::Size expected, const char* label) {
  std::cout << "size " << actual.width << 'x' << actual.height << std::endl;
  Check(std::abs(actual.width - expected.width) <= 2 &&
            std::abs(actual.height - expected.height) <= 2,
        label);
}
void CheckFixed(GtkWidget* widget) {
  Check(!gtk_window_get_resizable(GTK_WINDOW(widget)), "GTK disables interactive resizing");
  auto* surface = gtk_widget_get_window(widget);
  if (!GDK_IS_X11_WINDOW(surface))
    return;
  XSizeHints hints = {};
  long supplied = 0;
  Check(
      XGetWMNormalHints(GDK_WINDOW_XDISPLAY(surface), GDK_WINDOW_XID(surface), &hints, &supplied) &&
          (hints.flags & PMinSize) && (hints.flags & PMaxSize) &&
          hints.min_width == hints.max_width && hints.min_height == hints.max_height,
      "WM receives fixed-size hints");
}
void Run(bool csd, bool content, bool before_map) {
  std::cout << "CASE csd=" << csd << " content=" << content << " before_map=" << before_map
            << std::endl;
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widget), "nativeapi resizable regression");
  if (content)
    gtk_container_add(GTK_CONTAINER(widget), gtk_drawing_area_new());
  if (csd) {
    auto* titlebar = gtk_header_bar_new();
    gtk_widget_show(titlebar);
    gtk_window_set_titlebar(GTK_WINDOW(widget), titlebar);
  }
  {
    nativeapi::Window window(widget);
    Check(window.IsResizable(), "initially resizable");
    window.SetContentSize({560, 360});
    if (before_map) {
      window.SetResizable(false);
      window.SetContentSize({620, 420});
    }
    gtk_widget_show_all(widget);
    Settle();
    CheckSize(window.GetContentSize(),
              before_map ? nativeapi::Size{620, 420} : nativeapi::Size{560, 360},
              "initial content size");
    const auto initial = window.GetSize();
    window.SetResizable(false);
    Settle();
    Check(!window.IsResizable(), "resizable getter disabled");
    CheckSize(window.GetSize(), initial, "disabling preserves size");
    CheckFixed(widget);
    window.SetContentSize({740, 500});
    Settle();
    CheckSize(window.GetContentSize(), {740, 500}, "programmatic content growth");
    CheckFixed(widget);
    window.SetContentSize({450, 280});
    Settle();
    CheckSize(window.GetContentSize(), {450, 280}, "programmatic content shrink");
    window.SetSize({680, 480}, false);
    Settle();
    CheckSize(window.GetSize(), {680, 480}, "programmatic frame growth");
    window.SetSize({500, 350}, false);
    Settle();
    CheckSize(window.GetSize(), {500, 350}, "programmatic frame shrink");
    window.SetMinimumSize({400, 300});
    window.SetMaximumSize({850, 650});
    Settle();
    CheckFixed(widget);
    window.SetContentSize({550, 370});
    Settle();
    CheckSize(window.GetContentSize(), {550, 370}, "programmatic resize with constraints");
    CheckFixed(widget);
    const auto fixed = window.GetSize();
    window.SetResizable(true);
    Settle();
    Check(window.IsResizable() && gtk_window_get_resizable(GTK_WINDOW(widget)),
          "interactive resizing reenabled");
    CheckSize(window.GetSize(), fixed, "reenabling preserves size");
    window.SetSize({100, 100}, false);
    Settle();
    CheckSize(window.GetSize(), {400, 300}, "minimum survives toggle");
    window.SetSize({1000, 900}, false);
    Settle();
    CheckSize(window.GetSize(), {850, 650}, "maximum survives toggle");
    // Read native state even when the wrapped application's code changes it.
    gtk_window_set_resizable(GTK_WINDOW(widget), FALSE);
    Check(!window.IsResizable(), "getter observes native property");
  }
  gtk_widget_destroy(widget);
  Settle();
}
}  // namespace
int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv))
    return 77;
  Run(false, true, false);
  Run(true, true, true);
  Run(false, false, true);
  return failures ? 1 : 0;
}
