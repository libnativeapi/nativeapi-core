// Desktop integration test: SetAspectRatio on GTK windows, with server-side and
// client-side decorations. Run under a window manager with GDK_BACKEND=x11 or
// GDK_BACKEND=wayland. Real windows, no synthetic input.
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
constexpr double kRatio = 16.0 / 9.0;

void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Settle(int ms = 300) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    while (gtk_events_pending())
      gtk_main_iteration();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
nativeapi::Size Content(nativeapi::Window& window, const char* what) {
  const auto size = window.GetContentSize();
  std::cout << what << ": content " << size.width << 'x' << size.height << " ratio "
            << size.width / size.height << std::endl;
  return size;
}
bool HasRatio(nativeapi::Size size, double tolerance_px) {
  return std::abs(size.width - size.height * kRatio) <= tolerance_px;
}
// X11 only: the aspect hint must describe the surface at its current size, so
// that the content (surface minus header bar and shadow) has the API ratio.
void CheckHint(GtkWidget* widget) {
  auto* surface = gtk_widget_get_window(widget);
  if (!GDK_IS_X11_WINDOW(surface))
    return;
  XSizeHints hints = {};
  long supplied = 0;
  const bool ok =
      XGetWMNormalHints(GDK_WINDOW_XDISPLAY(surface), GDK_WINDOW_XID(surface), &hints, &supplied) &&
      (hints.flags & PAspect) && hints.min_aspect.y > 0;
  const double hint = ok ? static_cast<double>(hints.min_aspect.x) / hints.min_aspect.y : 0;
  const double surface_ratio =
      static_cast<double>(gdk_window_get_width(surface)) / gdk_window_get_height(surface);
  std::cout << "hint " << hint << " surface " << surface_ratio << std::endl;
  Check(ok && std::abs(hint - surface_ratio) < 0.01, "aspect hint matches the surface");
}

void Run(bool csd, bool with_content) {
  std::cout << "CASE csd=" << csd << " content=" << with_content << std::endl;
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widget), "nativeapi aspect ratio regression");
  if (with_content)
    gtk_container_add(GTK_CONTAINER(widget), gtk_drawing_area_new());
  if (csd) {
    auto* titlebar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(titlebar), TRUE);
    gtk_widget_show(titlebar);
    gtk_window_set_titlebar(GTK_WINDOW(widget), titlebar);
  }
  {
    nativeapi::Window window(widget);
    window.SetContentSize({640, 360});
    gtk_widget_show_all(widget);
    Settle();
    window.SetAspectRatio(kRatio);
    Settle();
    Check(HasRatio(Content(window, "after SetAspectRatio"), 1), "current 16:9 size kept");
    CheckHint(widget);

    // A user resize proposes sizes step by step, and the hint follows each
    // allocation. Emulate a bottom-edge drag: the height grows, width follows.
    for (int step = 1; step <= 15; ++step) {
      window.SetContentSize({2000, 360.0 + step * 12});
      Settle(120);
    }
    Settle();
    auto size = Content(window, "after incremental growth");
    Check(size.height > 400 && HasRatio(size, 3), "content keeps the ratio while resized");
    CheckHint(widget);

    // Programmatic sizes: GTK constrains gtk_window_resize with the same
    // hints, so Linux cannot hold an off-ratio size while a ratio is set.
    window.SetContentSize({700, 300});
    Settle();
    Content(window, "SetContentSize(700x300) with ratio (informational)");

    window.SetAspectRatio(0);
    Settle();
    window.SetContentSize({700, 300});
    Settle();
    size = Content(window, "after clearing");
    Check(std::abs(size.width - 700) <= 2 && std::abs(size.height - 300) <= 2,
          "clearing removes the constraint");
  }
  gtk_widget_destroy(widget);
  Settle();
}
}  // namespace

int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv))
    return 77;
  if (gdk_display_get_n_monitors(gdk_display_get_default()) == 0) {
    std::cerr << "SKIP: a desktop with an active monitor is required\n";
    return 77;
  }
  Run(false, true);
  Run(true, true);
  Run(false, false);  // Empty windows, as created by nativeapi itself.
  return failures ? 1 : 0;
}
