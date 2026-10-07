// WindowOcclusionChangedEvent / GetOcclusionState() (#74) on a real desktop:
// a window is shown, covered by another of this process, uncovered,
// minimized, restored and hidden. Windows are shown without activation and
// no input is sent. Not in CTest: it puts windows on screen. Exit 77 = skip.
//
// Linux does not know about covering windows: a shown window is Unknown
// there, and the coverage checks are skipped.
#include "nativeapi.h"
#include <chrono>
#include <functional>
#include <iostream>
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
void Check(bool ok, const std::string& label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void PumpOnce() {
#ifdef __APPLE__
  while (NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES])
    [NSApp sendEvent:event];
#elif defined(_WIN32)
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
#else
  while (gtk_events_pending()) gtk_main_iteration();
#endif
}
// The system can take seconds to recompute occlusion (macOS after a window
// is ordered out), so the wait is generous.
bool WaitFor(const std::function<bool()>& done, int ms = 6000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (!done() && std::chrono::steady_clock::now() < end) {
    PumpOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
}
const char* Name(WindowOcclusionState state) {
  switch (state) {
    case WindowOcclusionState::Visible: return "Visible";
    case WindowOcclusionState::Occluded: return "Occluded";
    default: return "Unknown";
  }
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__APPLE__) && !defined(_WIN32)
  if (!gtk_init_check(&argc, &argv)) return 77;
#endif
#ifdef _WIN32
  SetProcessDPIAware();
#endif
  Application::GetInstance();
#ifdef __APPLE__
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  [NSApp finishLaunching];
#endif
  Check(Window::IsOcclusionStateSupported(), "occlusion state is supported");

  Window window;
  window.SetTitle("nativeapi occlusion target");
  window.SetBounds({120, 140, 320, 220});
  const WindowId id = window.GetId();
  Check(window.GetOcclusionState() == WindowOcclusionState::Occluded,
        "a window that was never shown is occluded");

  std::vector<WindowOcclusionState> on_window, on_manager;
  window.AddListener<WindowOcclusionChangedEvent>(
      [&](const WindowOcclusionChangedEvent& e) { on_window.push_back(e.GetOcclusionState()); });
  WindowManager::GetInstance().AddListener<WindowOcclusionChangedEvent>(
      [&](const WindowOcclusionChangedEvent& e) {
        if (e.GetWindowId() == id) on_manager.push_back(e.GetOcclusionState());
      });
  WaitFor([] { return false; }, 200);

  // |change|, then the state and the last event on both must be |expected|.
  auto expect = [&](const char* label, WindowOcclusionState expected,
                    const std::function<void()>& change) {
    on_window.clear();
    on_manager.clear();
    change();
    WaitFor([&] {
      return !on_window.empty() && on_window.back() == expected && !on_manager.empty() &&
             on_manager.back() == expected;
    });
    const bool ok = window.GetOcclusionState() == expected && !on_window.empty() &&
                    on_window.back() == expected && on_window == on_manager;
    Check(ok, label);
    if (!ok)
      std::cout << "  state " << Name(window.GetOcclusionState()) << ", " << on_window.size()
                << " window / " << on_manager.size() << " manager events" << std::endl;
  };

#if defined(__APPLE__) || defined(_WIN32)
  constexpr bool coverage = true;
  const WindowOcclusionState shown = WindowOcclusionState::Visible;
#else
  constexpr bool coverage = false;
  const WindowOcclusionState shown = WindowOcclusionState::Unknown;
#endif
  expect(coverage ? "showing reports Visible" : "showing reports Unknown", shown,
         [&] { window.ShowInactive(); });

  Window cover;
  cover.SetTitle("nativeapi occlusion cover");
  cover.SetBounds({60, 80, 460, 360});
  if (coverage) {
    expect("a window on top reports Occluded", WindowOcclusionState::Occluded,
           [&] { cover.ShowInactive(); });
    expect("moving the cover away reports Visible", WindowOcclusionState::Visible,
           [&] { cover.SetBounds({700, 80, 460, 360}); });
    expect("moving it back reports Occluded", WindowOcclusionState::Occluded,
           [&] { cover.SetBounds({60, 80, 460, 360}); });
    expect("hiding the cover reports Visible", WindowOcclusionState::Visible, [&] { cover.Hide(); });
  } else {
    std::cout << "SKIP coverage: the platform does not report covering windows" << std::endl;
  }
  window.Minimize();
  if (WaitFor([&] { return window.IsMinimized(); })) {
    window.Restore();
    WaitFor([&] { return !window.IsMinimized(); });
    WaitFor([] { return false; }, 300);
    expect("minimizing reports Occluded", WindowOcclusionState::Occluded, [&] { window.Minimize(); });
    expect("restoring reports the window again", shown, [&] { window.Restore(); });
  } else {
    // Openbox on Xvfb, for one, does not iconify.
    std::cout << "SKIP minimize: the window manager did not minimize the window" << std::endl;
  }
  expect("hiding reports Occluded", WindowOcclusionState::Occluded, [&] { window.Hide(); });

  std::cout << (failures ? "FAILED" : "OK") << std::endl;
  return failures ? 1 : 0;
}
