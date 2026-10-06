// WindowPropertyChangedEvent (#72): setters on any wrapper and direct native
// changes, once per changed property, on the Window and on WindowManager.
// No input is sent. On Linux the window is shown, because the window
// manager only confirms the above/below state of a mapped window; run there
// on a private display (Xvfb + a window manager).
#include "nativeapi.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>
#include <iostream>
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
void Check(bool ok, const std::string& label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void PumpOnce() {
#if !defined(__APPLE__) && !defined(_WIN32)
  while (gtk_events_pending()) gtk_main_iteration();
#elif defined(_WIN32)
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
#endif
}
// Pumps until |done| holds or the window manager had |ms| to answer, then a
// little longer so a duplicate event would also arrive.
void Pump(const std::function<bool()>& done = [] { return true; }, int ms = 2000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  do {
    PumpOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (!done() && std::chrono::steady_clock::now() < end);
  for (int i = 0; i < 10; ++i) {
    PumpOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
int Count(const std::vector<WindowProperty>& events, WindowProperty property) {
  return static_cast<int>(std::count(events.begin(), events.end(), property));
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__APPLE__) && !defined(_WIN32)
  if (!gtk_init_check(&argc, &argv)) return 77;
#endif
  Application::GetInstance();
  Window window;
  window.SetTitle("before");
  const WindowId id = window.GetId();

  std::vector<WindowProperty> on_window, on_manager;
  window.AddListener<WindowPropertyChangedEvent>([&](const WindowPropertyChangedEvent& e) {
    on_window.push_back(e.GetProperty());
  });
  WindowManager::GetInstance().AddListener<WindowPropertyChangedEvent>(
      [&](const WindowPropertyChangedEvent& e) {
        if (e.GetWindowId() == id) on_manager.push_back(e.GetProperty());
      });
#if !defined(__APPLE__) && !defined(_WIN32)
  window.Show();
#endif
  Pump([] { return false; }, 300);  // Linux subscribes on the next main loop turn.

  // GTK reports keep-above only once the window manager confirms it, which
  // not every one does (Openbox on Xvfb does not); check with a plain window.
  bool keep_above_confirmed = true;
#if !defined(__APPLE__) && !defined(_WIN32)
  {
    GtkWidget* plain = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_show(plain);
    Pump([] { return false; }, 300);
    gtk_window_set_keep_above(GTK_WINDOW(plain), TRUE);
    Pump([&] { return !!(gdk_window_get_state(gtk_widget_get_window(plain)) & GDK_WINDOW_STATE_ABOVE); });
    keep_above_confirmed = gdk_window_get_state(gtk_widget_get_window(plain)) & GDK_WINDOW_STATE_ABOVE;
    gtk_widget_destroy(plain);
    if (!keep_above_confirmed)
      std::cout << "SKIP always-on-top: the window manager does not confirm keep-above" << std::endl;
  }
#endif

  // Runs |change| and checks |property| was reported exactly once, on both.
  auto expect = [&](const char* label, WindowProperty property, const std::function<void()>& change) {
    on_window.clear();
    on_manager.clear();
    change();
    Pump([&] { return Count(on_window, property) > 0 && Count(on_manager, property) > 0; });
    const bool ok = Count(on_window, property) == 1 && Count(on_manager, property) == 1 &&
                    on_window == on_manager;
    Check(ok, label);
    if (!ok) {
      std::cout << "  window:";
      for (auto p : on_window) std::cout << ' ' << static_cast<int>(p);
      std::cout << "  manager:";
      for (auto p : on_manager) std::cout << ' ' << static_cast<int>(p);
      std::cout << std::endl;
    }
  };
  auto expect_none = [&](const char* label, const std::function<void()>& change) {
    on_window.clear();
    on_manager.clear();
    change();
    Pump([] { return false; }, 200);
    Check(on_window.empty() && on_manager.empty(), label);
  };

  expect("SetTitle reports Title", WindowProperty::Title, [&] { window.SetTitle("after"); });
  Check(window.GetTitle() == "after", "the getter already returns the new title");
  expect_none("setting the same title reports nothing", [&] { window.SetTitle("after"); });
  expect("SetClosable reports Closable", WindowProperty::Closable, [&] { window.SetClosable(false); });
  expect("SetMinimizable reports Minimizable", WindowProperty::Minimizable,
         [&] { window.SetMinimizable(false); });
  expect("SetResizable reports Resizable", WindowProperty::Resizable,
         [&] { window.SetResizable(false); });
  if (keep_above_confirmed) {
    expect("SetAlwaysOnTop reports AlwaysOnTop", WindowProperty::AlwaysOnTop,
           [&] { window.SetAlwaysOnTop(true); });
    expect("SetAlwaysOnTop(false) reports AlwaysOnTop", WindowProperty::AlwaysOnTop,
           [&] { window.SetAlwaysOnTop(false); });
  }
  expect("SetTitleBarStyle reports TitleBarStyle", WindowProperty::TitleBarStyle,
         [&] { window.SetTitleBarStyle(TitleBarStyle::Hidden); });
  expect("SetTitleBarStyle back reports TitleBarStyle", WindowProperty::TitleBarStyle,
         [&] { window.SetTitleBarStyle(TitleBarStyle::Normal); });
  expect_none("SetClosable to its value reports nothing", [&] { window.SetClosable(false); });

  Window other(window.GetNativeObject());
  expect("a setter on another wrapper is reported once", WindowProperty::Closable,
         [&] { other.SetClosable(true); });

  // Changes made to the native window directly, bypassing nativeapi.
  expect("a direct native title change is reported", WindowProperty::Title, [&] {
#ifdef __APPLE__
    static_cast<NSWindow*>(window.GetNativeObject()).title = @"native";
#elif defined(_WIN32)
    SetWindowTextW(static_cast<HWND>(window.GetNativeObject()), L"native");
#else
    gtk_window_set_title(GTK_WINDOW(window.GetNativeObject()), "native");
#endif
  });
  Check(window.GetTitle() == "native", "the getter returns the native title");
  expect("a direct native resizable change is reported", WindowProperty::Resizable, [&] {
#ifdef __APPLE__
    auto* native = static_cast<NSWindow*>(window.GetNativeObject());
    native.styleMask |= NSWindowStyleMaskResizable;
#elif defined(_WIN32)
    auto hwnd = static_cast<HWND>(window.GetNativeObject());
    SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_THICKFRAME);
#else
    gtk_window_set_resizable(GTK_WINDOW(window.GetNativeObject()), TRUE);
#endif
  });
  Check(window.IsResizable(), "the getter returns the native resizable state");
  if (keep_above_confirmed) expect("a direct native always-on-top change is reported", WindowProperty::AlwaysOnTop, [&] {
#ifdef __APPLE__
    static_cast<NSWindow*>(window.GetNativeObject()).level = NSFloatingWindowLevel;
#elif defined(_WIN32)
    SetWindowPos(static_cast<HWND>(window.GetNativeObject()), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    gtk_window_set_keep_above(GTK_WINDOW(window.GetNativeObject()), TRUE);
#endif
  });

  std::cout << (failures ? "FAILED" : "OK") << std::endl;
  return failures ? 1 : 0;
}
