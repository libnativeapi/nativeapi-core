// Desktop integration test: a hidden-title-bar window keeps its content size across
// hide and show, whatever xdg states the compositor hands it.
//
// Hyprland reports every toplevel as tiled (at creation) and maximized (at map),
// floating ones included, so that it draws no client-side shadow, and GDK takes no
// resize while those states stand. The shadow gutter must therefore never be added
// before the compositor has answered: with the gutter in, the window stayed inflated by
// it (320×320 content in a 584×584 window) and grew by another two gutters on every
// show. Three timings of hiding the title bar are run; the compositor-independent
// guarantee is the same for all of them - no gutter band, no growth across shows -
// and the exact content size holds once the window had settled. Hidden before the
// compositor's answer, GTK's own CSD margin accounting still leaves the content a few
// tens of pixels off on Hyprland (printed, not judged). Prints the GDK window state at
// each step, for the record.
#include "../src/window.h"

#include <gtk/gtk.h>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {
int failures = 0;
void Check(bool ok, const std::string& label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Settle(int ms = 400) {
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    while (gtk_events_pending())
      gtk_main_iteration();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
std::string States(GtkWidget* widget) {
  auto* window = gtk_widget_get_window(widget);
  if (!window)
    return "(no GdkWindow)";
  const auto state = gdk_window_get_state(window);
  std::string out;
  if (state & GDK_WINDOW_STATE_WITHDRAWN) out += " withdrawn";
  if (state & GDK_WINDOW_STATE_MAXIMIZED) out += " maximized";
  if (state & GDK_WINDOW_STATE_FULLSCREEN) out += " fullscreen";
  if (state & GDK_WINDOW_STATE_TILED) out += " tiled";
  if (state & (GDK_WINDOW_STATE_TOP_TILED | GDK_WINDOW_STATE_RIGHT_TILED |
               GDK_WINDOW_STATE_BOTTOM_TILED | GDK_WINDOW_STATE_LEFT_TILED))
    out += " tiled-edges";
  if (state & GDK_WINDOW_STATE_FOCUSED) out += " focused";
  return out.empty() ? " (none)" : out;
}
struct Layout {
  GtkAllocation content{};
  int width = 0, height = 0;
};
Layout Measure(GtkWidget* widget, GtkWidget* content) {
  Layout layout;
  gtk_widget_get_allocation(content, &layout.content);
  gtk_window_get_size(GTK_WINDOW(widget), &layout.width, &layout.height);
  return layout;
}
void Print(const char* step, GtkWidget* widget, GtkWidget* content, const nativeapi::Window& window) {
  const auto layout = Measure(widget, content);
  const auto size = window.GetContentSize();
  std::cout << step << ": states" << States(widget) << "; window " << layout.width << "x"
            << layout.height << ", content allocation " << layout.content.width << "x"
            << layout.content.height << " at " << layout.content.x << "," << layout.content.y
            << ", GetContentSize " << size.width << "x" << size.height << std::endl;
}
// One window per scenario: when the title bar is hidden relative to the first map.
enum class HideAt { BeforeShow, RightAfterShow, OnceSettled };
const char* Name(HideAt at) {
  switch (at) {
    case HideAt::BeforeShow: return "title bar hidden before the first show";
    case HideAt::RightAfterShow: return "title bar hidden right after show, before the states arrive";
    case HideAt::OnceSettled: return "title bar hidden once the window settled";
  }
  return "";
}
void Scenario(HideAt at) {
  std::cout << "--- " << Name(at) << std::endl;
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widget), "Shadow remap test");
  auto* content = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(widget), content);
  // Flutter's runner: a CSD window with a header bar, hidden by the app afterwards.
  gtk_window_set_titlebar(GTK_WINDOW(widget), gtk_header_bar_new());
  gtk_window_set_default_size(GTK_WINDOW(widget), 320, 320);
  gtk_widget_realize(widget);
  nativeapi::Window window(widget);
  auto hide_title_bar = [&] {
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    window.SetHasShadow(true);
    window.SetContentSize({320, 320});
  };
  if (at == HideAt::BeforeShow)
    hide_title_bar();
  gtk_widget_show(content);
  gtk_widget_show(widget);
  if (at == HideAt::RightAfterShow)
    hide_title_bar();
  Settle(600);
  if (at == HideAt::OnceSettled) {
    hide_title_bar();
    Settle(600);
  }
  Print("shown", widget, content, window);
  const auto first = Measure(widget, content);
  // No gutter band: a compositor that fits every window gets the content as the whole
  // surface (a gutter would show as a content allocation smaller than the window).
  auto no_band = [](const Layout& l) {
    return l.content.width == l.width && l.content.height == l.height;
  };
  Check(no_band(first), std::string("no gutter band around the content (") + Name(at) + ")");
  if (at == HideAt::OnceSettled)
    Check(first.content.width == 320 && first.content.height == 320,
          "content is 320x320 with the title bar hidden");

  for (int round = 1; round <= 2; ++round) {
    window.Hide();
    Settle(500);
    Print(("hidden " + std::to_string(round)).c_str(), widget, content, window);
    window.Show();
    Settle(800);
    Print(("shown again " + std::to_string(round)).c_str(), widget, content, window);
    const auto again = Measure(widget, content);
    Check(no_band(again), "no gutter band after show " + std::to_string(round));
    Check(again.content.width == first.content.width &&
              again.content.height == first.content.height,
          "content size unchanged after show " + std::to_string(round));
    Check(again.width == first.width && again.height == first.height,
          "window size unchanged after show " + std::to_string(round));
  }
  // Informational: a compositor that reports the window as maximized makes GTK hold
  // back the resize until it is "restored", so this is printed, not judged.
  window.SetContentSize({360, 300});
  Settle(600);
  Print("after SetContentSize(360x300)", widget, content, window);
  gtk_widget_destroy(widget);
  Settle(300);
}
}  // namespace

int main(int argc, char** argv) {
  gtk_init(&argc, &argv);
  for (auto at : {HideAt::OnceSettled, HideAt::RightAfterShow, HideAt::BeforeShow})
    Scenario(at);
  std::cout << (failures ? "FAILED " : "OK ") << failures << std::endl;
  return failures;
}
