// Native caption policy regression. Creates only this process's windows;
// no synthetic input. Run with a desktop/window manager (77 means unavailable).
#include "nativeapi.h"
#include "../src/capi/window_c.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <future>
#include <thread>
#include <vector>
#ifdef __APPLE__
#import <AppKit/AppKit.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <gtk/gtk.h>
#endif
using namespace nativeapi;
namespace {
int failures = 0;
void Check(bool condition, const char* message) {
  std::cout << (condition ? "PASS " : "FAIL ") << message << std::endl;
  failures += !condition;
}
void Pump(int ms = 150) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  do {
#ifdef __APPLE__
    while (auto* event = [NSApp nextEventMatchingMask:NSEventMaskAny
        untilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]
        inMode:NSDefaultRunLoopMode dequeue:YES]) [NSApp sendEvent:event];
#elif defined(_WIN32)
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
#else
    while (gtk_events_pending()) gtk_main_iteration();
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < end);
}
}
int main(int argc, char** argv) {
#ifdef __APPLE__
  [NSApplication sharedApplication];
#elif !defined(_WIN32)
  if (!gtk_init_check(&argc, &argv)) return 77;
#endif
  Application::GetInstance();
#if !defined(__APPLE__) && !defined(_WIN32)
  auto* host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  auto* header = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header), ":minimize,maximize,close");
  gtk_widget_show(header);
  gtk_window_set_titlebar(GTK_WINDOW(host), header);
  auto* content_header = gtk_header_bar_new();
  gtk_widget_show(content_header);
  gtk_container_add(GTK_CONTAINER(host), content_header);
  Window window(host);
#else
  Window window;
#endif
  window.SetTitle("nativeapi caption controls regression");
  window.SetBounds({100, 100, 600, 400});
  window.ShowInactive();
  Window alias(window.GetNativeObject());
  auto ffi_window = native_window_create_with_native_window(window.GetNativeObject());
  Check(native_window_get_id(ffi_window) == window.GetId(), "C ABI alias retains native identity");
  std::vector<WindowProperty> changes;
  window.AddListener<WindowPropertyChangedEvent>([&](const auto& event) {
    changes.push_back(event.GetProperty());
  });
  Pump(400);
  const bool minimize = window.IsMinimizable();
  const bool maximize = window.IsMaximizable();
  const bool close = window.IsClosable();
  Check(alias.IsWindowControlButtonsVisible(), "native button group starts visible");
  changes.clear();
  native_window_set_window_control_buttons_visible(ffi_window, false);
  Pump();
  Check(!alias.IsWindowControlButtonsVisible() &&
        !native_window_is_window_control_buttons_visible(ffi_window),
        "C ABI hiding and state are shared by native window aliases");
  Check(alias.IsMinimizable() == minimize && alias.IsMaximizable() == maximize &&
        alias.IsClosable() == close, "group hiding preserves independent caption policies");
  Check(std::count(changes.begin(), changes.end(), WindowProperty::WindowControlButtonsVisible) == 1 &&
        std::count(changes.begin(), changes.end(), WindowProperty::Minimizable) == 0 &&
        std::count(changes.begin(), changes.end(), WindowProperty::Maximizable) == 0 &&
        std::count(changes.begin(), changes.end(), WindowProperty::Closable) == 0,
        "hiding emits one group change without changing independent policies");
#if defined(_WIN32)
  const auto hwnd = static_cast<HWND>(window.GetNativeObject());
  Check(!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_SYSMENU) &&
        (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION,
        "Win32 removes caption buttons while retaining the title bar");
#elif !defined(__APPLE__)
  Check(!gtk_header_bar_get_show_close_button(GTK_HEADER_BAR(header)),
        "GTK's native standard decoration group is hidden");
  Check(gtk_widget_get_visible(content_header), "application content headers remain visible");
#endif
#if defined(_WIN32)
  auto worker = std::async(std::launch::async, [&] {
    alias.SetWindowControlButtonsVisible(true);
    return alias.IsWindowControlButtonsVisible();
  });
  for (int i = 0; i < 70 && worker.wait_for(std::chrono::milliseconds(0)) !=
      std::future_status::ready; ++i) Pump(100);
  Check(worker.get(), "worker calls apply and read controls on their native owner thread");
  window.SetWindowControlButtonsVisible(false);
#endif
  alias.SetMinimizable(false);
  alias.SetMaximizable(false);
  alias.SetClosable(false);
  native_window_set_window_control_buttons_visible(ffi_window, true);
  Pump();
  Check(!window.IsMinimizable() && !window.IsMaximizable() && !window.IsClosable(),
        "showing the group does not re-enable independently disabled actions");
  window.SetClosable(true);
  window.SetMinimizable(true);
  window.SetMaximizable(true);
  Pump();
  Check(alias.IsWindowControlButtonsVisible(), "re-enabling policies restores native buttons");
#if defined(_WIN32)
  window.SetClosable(false);
  const UINT close_state = GetMenuState(GetSystemMenu(hwnd, FALSE), SC_CLOSE, MF_BYCOMMAND);
  Check(!alias.IsClosable() && window.IsWindowControlButtonsVisible() &&
        window.IsMinimizable() && window.IsMaximizable() &&
        close_state != static_cast<UINT>(-1) && (close_state & (MF_DISABLED | MF_GRAYED)),
        "only the native close command is disabled, retaining minimize and maximize");
  window.SetClosable(true);
#endif
  window.SetTitleBarStyle(TitleBarStyle::Hidden);
  window.SetWindowControlButtonsVisible(false);
  Pump();
  Check(!alias.IsWindowControlButtonsVisible(), "a hidden title bar has no native buttons");
#if defined(_WIN32)
  const auto custom_style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  Check((custom_style & (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) ==
        (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
        "custom chrome retains native system-menu and snap styles");
#elif !defined(__APPLE__)
  Check(!gtk_widget_get_visible(header) && gtk_widget_get_visible(content_header),
        "title-bar style changes affect only the window's own header");
#endif
  window.SetTitleBarStyle(TitleBarStyle::Normal);
  Pump();
  Check(alias.IsWindowControlButtonsVisible(), "normal title-bar style resets group visibility");
#if !defined(__APPLE__)
  window.SetClosable(false);
  changes.clear();
  window.SetFullScreen(true);
  for (int i = 0; i < 50 && !window.IsFullScreen(); ++i) Pump(100);
  Check(window.IsFullScreen() && !window.IsWindowControlButtonsVisible(),
        "fullscreen has no normal caption buttons");
  Check(std::count(changes.begin(), changes.end(), WindowProperty::WindowControlButtonsVisible) == 1,
        "native fullscreen emits one caption-group visibility change");
  alias.SetWindowControlButtonsVisible(false);
  alias.SetMinimizable(false);
  alias.SetMaximizable(false);
  window.SetFullScreen(false);
  for (int i = 0; i < 50 && window.IsFullScreen(); ++i) Pump(100);
  Check(!window.IsFullScreen() && !window.IsWindowControlButtonsVisible() &&
        !window.IsMinimizable() && !window.IsMaximizable() && !window.IsClosable(),
        "fullscreen restores updated visibility and independent policies");
#endif
  int closes = 0;
  window.AddListener<WindowCloseRequestedEvent>([&](const auto& event) {
    ++closes;
    event.GetRequest()->Cancel();
  });
  Pump();
  window.SetClosable(false);
  window.Close();
  for (int i = 0; i < 50 && !closes; ++i) Pump(50);
  Check(closes == 1, "programmatic close bypasses disabled native close controls");
  native_window_free(ffi_window);
#ifdef __APPLE__
  [(__bridge NSWindow*)window.GetNativeObject() close];
#elif defined(_WIN32)
  DestroyWindow(hwnd);
#else
  gtk_widget_destroy(host);
#endif
  return failures ? 1 : 0;
}
