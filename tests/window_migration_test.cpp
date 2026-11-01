// Native regressions for window_manager's migrated platform capabilities.
// Sends no input; Windows docking changes the desktop work area temporarily.
#include "nativeapi.h"
#include "../src/capi/window_c.h"
static_assert(NATIVE_WINDOW_EVENT_TYPE_MAXIMIZED == 3);
static_assert(NATIVE_WINDOW_EVENT_TYPE_OCCLUSION_CHANGED == 13);
static_assert(NATIVE_WINDOW_EVENT_TYPE_DOCKED == 14);
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <thread>
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
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Pump(int milliseconds = 100) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  do {
#ifdef __APPLE__
    while (NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
        untilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]
        inMode:NSDefaultRunLoopMode dequeue:YES]) [NSApp sendEvent:event];
#elif defined(_WIN32)
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
#else
    while (gtk_events_pending()) gtk_main_iteration();
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < end);
}
bool Near(double a, double b) { return std::abs(a - b) < 2; }
}  // namespace

int main(int argc, char** argv) {
#ifdef __APPLE__
  [NSApplication sharedApplication];
#elif !defined(_WIN32)
  if (!gtk_init_check(&argc, &argv)) return 77;
#endif
  Application::GetInstance();
  Window window;
  Window alias(window.GetNativeObject());
  Check(window.GetId() == alias.GetId(), "wrappers share a native identity");
  window.SetBounds({100, 120, 500, 360});
  window.ShowInactive();
  Pump(250);
#ifdef __APPLE__
  auto* native = (__bridge NSWindow*)window.GetNativeObject();
  native.collectionBehavior |= NSWindowCollectionBehaviorManaged |
      NSWindowCollectionBehaviorMoveToActiveSpace | NSWindowCollectionBehaviorFullScreenPrimary;
  const auto unrelated = native.collectionBehavior & ~(
      NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary);
  Check(window.SetVisibleOnFullScreen(true) && alias.IsVisibleOnFullScreen(),
        "full-screen visibility is shared across wrappers");
  window.SetVisibleOnAllWorkspaces(true);
  Check(!(native.collectionBehavior & NSWindowCollectionBehaviorMoveToActiveSpace) &&
        !(native.collectionBehavior & NSWindowCollectionBehaviorFullScreenPrimary),
        "enabling visibility policies displaces conflicting AppKit flags");
  Check(alias.IsVisibleOnAllWorkspaces() && alias.IsVisibleOnFullScreen(),
        "enabling all spaces preserves full-screen auxiliary");
  window.SetVisibleOnAllWorkspaces(false);
  Check(!alias.IsVisibleOnAllWorkspaces() && alias.IsVisibleOnFullScreen(),
        "disabling all spaces preserves full-screen auxiliary");
  Check(window.SetVisibleOnFullScreen(false) &&
        (native.collectionBehavior & ~(NSWindowCollectionBehaviorCanJoinAllSpaces |
         NSWindowCollectionBehaviorFullScreenAuxiliary)) == unrelated,
        "visibility changes preserve unrelated collection behavior");
  const auto original = window.GetBounds();
  Check(!window.AnimateBounds({0, 0, -10, 300}) && Near(window.GetBounds().x, original.x),
        "invalid animation leaves the window unchanged");
  Check(window.AnimateBounds({130, 160, 560, 400}), "native frame animation submitted");
  Pump(1000);
  const auto result = alias.GetBounds();
  Check(Near(result.x, 130) && Near(result.y, 160) && Near(result.width, 560) &&
        Near(result.height, 400), "native frame animation reaches target bounds");
  int restored = 0;
  auto listener = window.AddListener<WindowRestoredEvent>([&](const auto&) { ++restored; });
  window.RemoveListener(listener);
  window.Maximize();
  Pump(700);
  listener = window.AddListener<WindowRestoredEvent>([&](const auto&) { ++restored; });
  window.Unmaximize();
  Pump(700);
  Check(restored == 1, "re-subscription seeds the live zoom state before unmaximizing");
  window.RemoveListener(listener);
  [native close];
#elif defined(_WIN32)
  HWND hwnd = static_cast<HWND>(window.GetNativeObject());
  const auto original = window.GetBounds();
  Check(window.SetMaximizedVertically(true) && alias.IsMaximizedVertically() &&
        !window.IsMaximized() && Near(window.GetBounds().width, original.width),
        "vertical maximize is shared and preserves width");
  Check(alias.SetMaximizedVertically(false) && !window.IsMaximizedVertically() &&
        Near(window.GetBounds().height, original.height), "vertical restore uses original bounds");
  if (Window::IsDockingSupported()) {
    MONITORINFO monitor = {sizeof(monitor)};
    const HMONITOR screen = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(screen, &monitor);
    const RECT work = monitor.rcWork;
    int docked = 0, undocked = 0;
    window.AddListener<WindowDockedEvent>([&](const auto& event) {
      if (event.GetSide() != WindowDockSide::None) ++docked;
    });
    alias.AddListener<WindowUndockedEvent>([&](const auto&) { ++undocked; });
    Check(window.Dock(WindowDockSide::Left, 180) && alias.GetDockSide() == WindowDockSide::Left,
          "left appbar reservation shared across wrappers");
    Pump();
    GetMonitorInfoW(screen, &monitor);
    Check(monitor.rcWork.left > work.left && docked == 1,
          "left appbar reserves actual desktop space and emits one event");
    Check(!window.Dock(WindowDockSide::Right, std::numeric_limits<double>::quiet_NaN()) &&
          alias.GetDockSide() == WindowDockSide::Left, "invalid docking does not change reservation");
    Check(alias.Dock(WindowDockSide::Right, 200), "reservation switches edges");
    Pump();
    GetMonitorInfoW(screen, &monitor);
    Check(monitor.rcWork.left == work.left && monitor.rcWork.right < work.right && docked == 2,
          "switching docking releases previous edge");
    Check(alias.Undock() && window.GetDockSide() == WindowDockSide::None && undocked == 1,
          "undocking is shared and emits one event");
    Pump();
    GetMonitorInfoW(screen, &monitor);
    Check(EqualRect(&work, &monitor.rcWork) && Near(window.GetBounds().width, original.width),
          "undocking restores desktop work area and window bounds");
    auto worker = std::async(std::launch::async, [&] {
      Window other(hwnd);
      return other.Dock(WindowDockSide::Left, 180);
    });
    for (int i = 0; i < 70 && worker.wait_for(std::chrono::milliseconds(0)) !=
        std::future_status::ready; ++i) Pump(100);
    Check(worker.get(), "worker-thread docking executes on window owner thread");
    DestroyWindow(hwnd);
    Pump();
    GetMonitorInfoW(screen, &monitor);
    Check(EqualRect(&work, &monitor.rcWork), "native destruction releases desktop reservation");
  } else {
    std::cout << "SKIP appbar tests: no interactive shell" << std::endl;
    DestroyWindow(hwnd);
  }
#else
  if (Window::IsKeyboardGrabSupported()) {
    Window other;
    other.ShowInactive();
    Pump();
    Check(window.GrabKeyboard() && alias.IsKeyboardGrabbed(), "X11 keyboard grab shared across wrappers");
    Check(window.GrabKeyboard(), "repeated grab succeeds without replacing ownership");
    Check(!other.GrabKeyboard(), "another window cannot steal the keyboard grab");
    Check(other.UngrabKeyboard() && window.IsKeyboardGrabbed(), "another window cannot release the grab");
    Check(alias.UngrabKeyboard() && !window.IsKeyboardGrabbed(), "alias releases the keyboard grab");
    Check(window.GrabKeyboard(), "keyboard can be grabbed again");
    window.Hide();
    Pump();
    Check(!alias.IsKeyboardGrabbed(), "hiding releases the keyboard grab");
    window.ShowInactive();
    Pump();
    Check(other.GrabKeyboard(), "another window can acquire after the owner is hidden");
    gtk_widget_destroy(GTK_WIDGET(other.GetNativeObject()));
    Pump();
    Check(window.GrabKeyboard(), "native destruction releases the previous owner's grab");
    auto* seat = gdk_display_get_default_seat(gdk_display_get_default());
    Window external;
    external.ShowInactive();
    Pump();
    const auto status = gdk_seat_grab(seat, gtk_widget_get_window(GTK_WIDGET(external.GetNativeObject())),
        GDK_SEAT_CAPABILITY_KEYBOARD, FALSE, nullptr, nullptr, nullptr, nullptr);
    Pump();
    Check(status == GDK_GRAB_SUCCESS && !window.IsKeyboardGrabbed(),
          "a native grab-broken event clears the previous owner");
    window.UngrabKeyboard();
    Check(gdk_display_device_is_grabbed(gdk_display_get_default(), gdk_seat_get_keyboard(seat)),
          "releasing a broken grab does not release an external native grab");
    gdk_seat_ungrab(seat);
    gtk_widget_destroy(GTK_WIDGET(external.GetNativeObject()));
  } else {
    Check(!window.GrabKeyboard() && !window.UngrabKeyboard() && !window.IsKeyboardGrabbed(),
          "Wayland refuses unsupported keyboard grabs");
  }
  gtk_widget_destroy(GTK_WIDGET(window.GetNativeObject()));
#endif
  return failures ? 1 : 0;
}
