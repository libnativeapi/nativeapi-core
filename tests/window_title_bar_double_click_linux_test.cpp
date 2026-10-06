// Requires a window manager on a private Xvfb server. Sends no pointer input.
#include "nativeapi.h"
#include <gtk/gtk.h>
#include <cstdlib>
#include <iostream>
#include <string>
static int failures;
static void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
static void Settle() {
  const auto end = g_get_monotonic_time() + 400000;
  while (g_get_monotonic_time() < end) {
    while (gtk_events_pending()) gtk_main_iteration();
    g_usleep(1000);
  }
}
int main(int argc, char** argv) {
  const char* isolated = std::getenv("NATIVEAPI_TEST_PRIVATE_X11");
  if (!isolated || std::string(isolated) != "1" || !gtk_init_check(&argc, &argv)) return 77;
  nativeapi::Application::GetInstance();
  GtkWidget* host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(host), 400, 240);
  gtk_container_add(GTK_CONTAINER(host), gtk_label_new("double-click action test"));
  nativeapi::Window window(host);
  window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
  gtk_widget_show_all(host);
  window.ShowInactive(); Settle();
  Check(window.PerformTitleBarDoubleClick(), "hidden title bar requests maximize");
  Settle(); Check(window.IsMaximized(), "window manager applied maximize");
  const auto handle = native_window_create_with_native_window(host);
  Check(native_window_perform_title_bar_double_click(handle), "C ABI requests unmaximize");
  Settle(); Check(!window.IsMaximized(), "window manager applied unmaximize");
  window.SetMaximizable(false);
  Check(!window.PerformTitleBarDoubleClick(), "disabled maximize rejects gesture");
  Settle(); Check(!window.IsMaximized(), "disabled gesture does not maximize");
  window.SetMaximizable(true);
  window.SetFullScreen(true); Settle();
  Check(window.IsFullScreen() && !window.PerformTitleBarDoubleClick(), "full-screen window rejects gesture");
  window.SetFullScreen(false); Settle();
  window.Minimize(); Settle();
  Check(window.IsMinimized() && !window.PerformTitleBarDoubleClick(), "minimized window rejects gesture");
  Check(!native_window_perform_title_bar_double_click(0), "invalid handle rejected");
  gtk_widget_destroy(host);
  Check(!window.PerformTitleBarDoubleClick(), "destroyed native window rejected");
  native_window_free(handle);
  return failures ? 1 : 0;
}
