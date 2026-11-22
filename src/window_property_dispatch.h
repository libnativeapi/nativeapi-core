#pragma once

// Private producer of WindowPropertyChangedEvent. Does not enter API_HEADERS.
//
// The last reported value of every property is kept per native window. A
// refresh re-reads them through the Window getters and emits one event per
// property that differs, so a change is reported once however it was noticed:
// by the setter that made it or by the platform's own change notification.
#include "window.h"

namespace nativeapi::detail {

struct WindowPropertyDispatch {
  // Re-reads |window|'s properties and emits for every changed one. The first
  // reading of a native window only records the values. Ignored while a
  // WindowPropertyScope is open on this thread: that scope refreshes at its end.
  static void Refresh(const Window& window);
  // A rendered SDK frame can change only the observed title-button layout.
  // Avoid polling every unrelated window property once per frame.
  static void RefreshControlButtons(const Window& window);
  // A Z-order change on Windows can change only AlwaysOnTop.
  // Avoid polling title, menu and style properties on every WM_WINDOWPOSCHANGED.
  static void RefreshAlwaysOnTop(const Window& window);
  // Seed the first usable rendered control layout, which may become available
  // after native window attachment but before observation starts.
  static void PrimeControlButtons(const Window& window);
  // Re-reads Window::GetOcclusionState() and emits WindowOcclusionChangedEvent
  // when it differs from the last reading; the first reading only records it.
  static void RefreshOcclusion(const Window& window);
  // Drops the readings of a native window that is gone.
  static void Forget(WindowId id);
};

// Opened by a setter around its native change: records the readings before
// the change if there are none yet, holds back refreshes from the change
// notifications it triggers, and refreshes once at the end.
class WindowPropertyScope {
 public:
  explicit WindowPropertyScope(const Window& window);
  ~WindowPropertyScope();
  WindowPropertyScope(const WindowPropertyScope&) = delete;
  WindowPropertyScope& operator=(const WindowPropertyScope&) = delete;

 private:
  const Window& window_;
};

}  // namespace nativeapi::detail
