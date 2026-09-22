// Real desktop integration test; no synthetic mouse or keyboard input.
#include "nativeapi.h"

#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
void Check(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  failures += !ok;
}
void Settle() {
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
  while (std::chrono::steady_clock::now() < end) {
    while (gtk_events_pending()) gtk_main_iteration();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
}  // namespace
int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) return 77;
  if (gdk_display_get_n_monitors(gdk_display_get_default()) == 0) {
    std::cerr << "SKIP: a desktop with an active monitor is required\n";
    return 77;
  }
  auto& manager = nativeapi::WindowManager::GetInstance();
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widget), "nativeapi restore regression");
  gtk_container_add(GTK_CONTAINER(widget), gtk_drawing_area_new());
  nativeapi::Window window(widget);
  window.SetContentSize({600, 400});
  int minimized = 0, restored = 0;
  const auto minimize_listener = manager.AddListener<nativeapi::WindowMinimizedEvent>([&](const auto& event) {
    if (event.GetWindowId() == window.GetId()) ++minimized;
  });
  const auto restore_listener = manager.AddListener<nativeapi::WindowRestoredEvent>([&](const auto& event) {
    if (event.GetWindowId() == window.GetId()) ++restored;
  });
  gtk_widget_show_all(widget);
  window.Show();
  Settle();
  const bool x11 = GDK_IS_X11_WINDOW(gtk_widget_get_window(widget));
  Check(window.IsVisible() && window.IsFocused(), "initial Show presents window");
  Check(minimized == 0 && restored == 0, "initial show emits no minimize/restore transition");
  for (int mode = 0; mode < 3; ++mode) {
    std::cout << "CASE " << mode << std::endl;
    // GTK iconify is what its title-bar button does, including iconify-on-map
    // bookkeeping. Test that path as well as the nativeapi request.
    if (mode == 0) window.Minimize();
    else gtk_window_iconify(GTK_WINDOW(widget));
    Settle();
    if (x11) {
      Check(window.IsMinimized(), "WM confirmed minimization");
      Check(minimized == mode + 1, "one observed minimize event");
    } else {
      Check(!window.IsFocused(), "minimize removes activation");
    }
    if (mode == 2) window.Hide();
    if (mode == 0) window.Restore();
    else window.Show();
    Settle();
    Check(gtk_widget_get_mapped(widget), "restored window mapped");
    if (x11 || window.IsFocused()) {
      Check(window.IsFocused(), "restored window active");
    } else {
      // This input-free test has no Wayland user activation token. Mutter may
      // reject the request; do not count a mapped-but-minimized window as restored.
      std::cout << "SKIP restoration: Wayland activation denied without user token" << std::endl;
    }
    if (x11) {
      Check(!window.IsMinimized(), "WM confirmed restoration");
      Check(restored == mode + 1, "one observed restore event");
      Check(minimized == mode + 1, "restoring emits no spurious minimize event");
    }
    const int before = minimized;
    window.Show();
    Settle();
    Check(minimized == before, "repeated Show does not re-minimize");
  }
  if (!x11) {
    Check(minimized == 0 && restored == 0, "Wayland does not fabricate unsupported iconify events");
  }
  manager.RemoveListener(minimize_listener);
  manager.RemoveListener(restore_listener);
  gtk_widget_destroy(widget);
  Settle();
  return failures ? 1 : 0;
}
