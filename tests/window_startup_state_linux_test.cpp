// State requests before first map. The minimize cases are checked against
// Wayland protocol output by core_window_startup_state_test_linux.py because
// Wayland does not report an observed minimized state back to GTK clients.
#include "nativeapi.h"
#include <gtk/gtk.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
using namespace nativeapi;
static void Pump(int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  do {
    while (gtk_events_pending()) gtk_main_iteration();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < end);
}
int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) return 77;
  Application::GetInstance();
  const std::string mode = argc > 1 ? argv[1] : "fullscreen";
  if (mode != "normal" && mode != "fullscreen" && mode != "cancel-fullscreen" &&
      mode != "minimize" && mode != "cancel-minimize" && mode != "show-minimize" &&
      mode != "fullscreen-then-maximize") return 2;
  Window window;
  window.SetTitle("nativeapi startup state regression");
  window.SetSize({540, 380}, false);
  Window alias(window.GetNativeObject());
  if (mode.find("fullscreen") != std::string::npos) {
    window.SetFullScreen(true);
    if (mode == "cancel-fullscreen") alias.SetFullScreen(false);
  } else if (mode.find("minimize") != std::string::npos) {
    window.Minimize();
    if (mode == "cancel-minimize") alias.Restore();
    if (mode == "show-minimize") alias.Show();
  }
  window.ShowInactive();
  auto* widget = GTK_WIDGET(window.GetNativeObject());
  auto* surface = gtk_widget_get_window(widget);
  GdkRectangle monitor;
  gdk_monitor_get_geometry(gdk_display_get_monitor_at_window(gdk_window_get_display(surface), surface),
                           &monitor);
  if (mode == "fullscreen")
    std::cout << "MONITOR " << monitor.width << " " << monitor.height << std::endl;
  bool ok = false;
  for (int i = 0; i < 50; ++i) {
    Pump(100);
    const auto size = window.GetSize();
    if (mode == "fullscreen" || mode == "fullscreen-then-maximize") {
      ok = gtk_widget_get_mapped(widget) && window.IsFullScreen() &&
           std::abs(size.width - monitor.width) < 2 && std::abs(size.height - monitor.height) < 2;
    } else if (mode == "normal" || mode == "cancel-fullscreen") {
      ok = gtk_widget_get_mapped(widget) && !window.IsFullScreen() &&
           std::abs(size.width - 540) < 2;
    } else {
      // Let the compositor receive the initial map/configure/minimize sequence.
      ok = gtk_widget_get_mapped(widget);
      if (ok) Pump(300);
    }
    if (ok) break;
  }
  if (ok && mode == "fullscreen-then-maximize") {
    alias.SetFullScreen(false);
    alias.Maximize();
    ok = false;
    for (int i = 0; i < 50; ++i) {
      Pump(100);
      if (!window.IsFullScreen() && window.IsMaximized()) {
        ok = true;
        break;
      }
    }
    Pump(400);
    ok = ok && !window.IsFullScreen() && window.IsMaximized();
  }
  if (ok && mode == "fullscreen") {
    alias.SetFullScreen(false);
    ok = false;
    for (int i = 0; i < 50; ++i) {
      Pump(100);
      if (!window.IsFullScreen() && std::abs(window.GetSize().width - 540) < 2) {
        ok = true;
        break;
      }
    }
  }
  if (ok && mode == "fullscreen") {
    Pump(400);
    ok = !window.IsFullScreen() && !window.IsMaximized() &&
         std::abs(window.GetSize().width - 540) < 2;
  }
  if (!ok) {
    const auto size = window.GetSize();
    std::cout << "mapped=" << gtk_widget_get_mapped(widget)
              << " fullscreen=" << window.IsFullScreen()
              << " size=" << size.width << "x" << size.height
              << " monitor=" << monitor.width << "x" << monitor.height << std::endl;
  }
  std::cout << (ok ? "PASS " : "FAIL ") << "startup " << mode << std::endl;
  gtk_widget_destroy(widget);
  Pump(50);
  return ok ? 0 : 1;
}
