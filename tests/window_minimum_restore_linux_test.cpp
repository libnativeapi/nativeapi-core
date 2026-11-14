// #35: hidden-title-bar windows must retain their normal size across maximize,
// even with a minimum size and GTK's changing CSD shadow extents. No input.
#include "../src/window.h"
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
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < until) {
    while (gtk_events_pending()) gtk_main_iteration();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
void Size(nativeapi::Window& window, double w, double h, const char* label) {
  const auto size = window.GetSize();
  std::cout << "SIZE " << label << " " << size.width << "x" << size.height << std::endl;
  Check(std::abs(size.width - w) <= 2 && std::abs(size.height - h) <= 2, label);
}
void Run(bool header, bool minimum) {
  std::cout << "CASE header=" << header << " minimum=" << minimum << std::endl;
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widget), "NativeAPI minimum restore regression");
  auto* child = gtk_drawing_area_new();
  gtk_widget_set_size_request(child, 240, 160);
  gtk_container_add(GTK_CONTAINER(widget), child);
  if (header) gtk_window_set_titlebar(GTK_WINDOW(widget), gtk_header_bar_new());
  gtk_window_set_default_size(GTK_WINDOW(widget), 800, 600);
  gtk_widget_realize(widget);
  nativeapi::Window window(widget);
  window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
  if (minimum) window.SetMinimumSize({800, 600});
  window.SetSize({800, 600}, false);
  gtk_widget_show(child);
  window.Show();
  Settle();
  window.SetSize({800, 600}, false);
  Settle();
  Size(window, 800, 600, "initial size");
  for (int cycle = 0; cycle < 3; ++cycle) {
    window.Maximize();
    Settle();
    const auto maximized = window.GetSize();
    std::cout << "SIZE maximized " << maximized.width << "x" << maximized.height << std::endl;
    Check(window.IsMaximized(), "maximize acknowledged");
    window.Unmaximize();
    Settle();
    Check(!window.IsMaximized(), "restore acknowledged");
    Size(window, 800, 600, "restored size");
  }
  if (minimum) {
    window.SetSize({300, 200}, false);
    Settle();
    Size(window, 800, 600, "minimum still enforced");
    window.SetMaximumSize({900, 700});
    window.SetSize({1100, 850}, false);
    Settle();
    Size(window, 900, 700, "maximum still enforced");
    window.SetMinimumSize({0, 0});
    int width, height;
    gtk_widget_get_size_request(child, &width, &height);
    Check(width == 240 && height == 160, "clearing preserves the host's size request");
    window.SetSize({500, 400}, false);
    Settle();
    Size(window, 500, 400, "minimum can be cleared");
  }
  gtk_widget_destroy(widget);
  Settle();
}
}  // namespace
int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) return 77;
  for (bool header : {false, true})
    for (bool minimum : {false, true}) Run(header, minimum);
  std::cout << "RESULT failures=" << failures << std::endl;
  return failures ? 1 : 0;
}
