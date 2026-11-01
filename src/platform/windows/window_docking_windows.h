#pragma once

// Private native-window state shared by every Window wrapper. Included only by
// window_windows.cpp; the lifetime subclass below handles shell notifications.
#include <shellapi.h>
#include "../../window_dock_dispatch.h"
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <memory>
#include "ui_thread_windows.h"

namespace nativeapi::detail {

// Flutter may call from an isolate thread. Keep native reservation state and
// callbacks on the HWND's owner, including getters, and recheck native identity.
template <typename Result, typename Work>
Result OnWindowThread(HWND hwnd, WindowId id, Result fallback, Work work) {
  struct Task {
    std::mutex mutex;
    std::condition_variable ready;
    bool started = false;
    bool cancelled = false;
    bool done = false;
    Result result;
    explicit Task(Result initial) : result(initial) {}
  };
  const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
  if (!thread) return fallback;
  if (thread == GetCurrentThreadId()) return work();
  auto state = std::make_shared<Task>(fallback);
  if (!PostToThread(thread, [hwnd, id, state, work] {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->cancelled) return;
      state->started = true;
    }
    Result result = state->result;
    try {
      if (::IsWindow(hwnd)) {
        Window window(hwnd);
        if (window.GetId() == id) result = work();
      }
    } catch (...) {
    }
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->result = result;
      state->done = true;
    }
    state->ready.notify_one();
  })) return fallback;
  std::unique_lock<std::mutex> lock(state->mutex);
  if (!state->ready.wait_for(lock, std::chrono::seconds(5), [&] { return state->done; })) {
    if (!state->started) {
      state->cancelled = true;
      return fallback;
    }
    state->ready.wait(lock, [&] { return state->done; });
  }
  return state->result;
}

inline constexpr wchar_t kDockProperty[] = L"NativeAPIDesktopDock";
inline constexpr wchar_t kVerticalMaximizeProperty[] = L"NativeAPIVerticalMaximize";

struct DesktopDock {
  WindowDockSide side = WindowDockSide::None;
  double width = 0;
  RECT original = {};
  bool positioning = false;
};
struct VerticalMaximize {
  RECT original;
  RECT applied;
};

inline UINT DockMessage() {
  static const UINT message = RegisterWindowMessageW(L"NativeAPIDesktopDockCallback");
  return message;
}
inline UINT ShellCreatedMessage() {
  static const UINT message = RegisterWindowMessageW(L"TaskbarCreated");
  return message;
}
inline std::shared_ptr<DesktopDock> DockFor(HWND hwnd) {
  auto* holder = static_cast<std::shared_ptr<DesktopDock>*>(GetPropW(hwnd, kDockProperty));
  return holder ? *holder : nullptr;
}
inline bool StoreDock(HWND hwnd, const std::shared_ptr<DesktopDock>& state) {
  auto* holder = new std::shared_ptr<DesktopDock>(state);
  if (SetPropW(hwnd, kDockProperty, holder)) return true;
  delete holder;
  return false;
}
inline std::shared_ptr<DesktopDock> TakeDock(HWND hwnd) {
  auto* holder = static_cast<std::shared_ptr<DesktopDock>*>(RemovePropW(hwnd, kDockProperty));
  auto result = holder ? *holder : nullptr;
  delete holder;
  return result;
}
inline APPBARDATA DockData(HWND hwnd) {
  APPBARDATA data = {};
  data.cbSize = sizeof(data);
  data.hWnd = hwnd;
  data.uCallbackMessage = DockMessage();
  return data;
}

inline bool PositionDock(HWND hwnd, DesktopDock& dock) {
  if (dock.positioning) return true;
  MONITORINFO monitor = {sizeof(monitor)};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
  const double physical_width = dock.width * GetDpiForWindow(hwnd) / 96.0;
  if (!std::isfinite(physical_width) || physical_width < 1 ||
      physical_width >= monitor.rcMonitor.right - monitor.rcMonitor.left) return false;
  const LONG width = static_cast<LONG>(std::round(physical_width));
  auto data = DockData(hwnd);
  data.uEdge = dock.side == WindowDockSide::Left ? ABE_LEFT : ABE_RIGHT;
  data.rc = monitor.rcMonitor;
  dock.positioning = true;
  SHAppBarMessage(ABM_QUERYPOS, &data);
  if (data.uEdge == ABE_LEFT) data.rc.right = data.rc.left + width;
  else data.rc.left = data.rc.right - width;
  SHAppBarMessage(ABM_SETPOS, &data);
  const bool success = SetWindowPos(hwnd, nullptr, data.rc.left, data.rc.top,
      data.rc.right - data.rc.left, data.rc.bottom - data.rc.top,
      SWP_NOACTIVATE | SWP_NOZORDER) != FALSE;
  dock.positioning = false;
  RECT actual = {};
  return success && ::IsWindow(hwnd) && DockFor(hwnd).get() == &dock &&
      GetWindowRect(hwnd, &actual) && EqualRect(&actual, &data.rc);
}

inline void RemoveDockOnDestroy(HWND hwnd) {
  auto dock = TakeDock(hwnd);
  if (dock) {
    auto data = DockData(hwnd);
    SHAppBarMessage(ABM_REMOVE, &data);
  }
  delete static_cast<VerticalMaximize*>(RemovePropW(hwnd, kVerticalMaximizeProperty));
}

inline void HandleDockMessage(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  auto dock = DockFor(hwnd);
  if (!dock) return;
  if (message == DockMessage()) {
    if (wp == ABN_POSCHANGED) PositionDock(hwnd, *dock);
    if (wp == ABN_FULLSCREENAPP) {
      // Do not cover another application's full-screen window.
      SetWindowPos(hwnd, lp ? HWND_BOTTOM :
          (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST ? HWND_TOPMOST : HWND_TOP),
          0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  } else if (message == ShellCreatedMessage()) {
    auto data = DockData(hwnd);
    if (SHAppBarMessage(ABM_NEW, &data)) PositionDock(hwnd, *dock);
    else {
      // The replacement shell rejected registration: stop reporting a reservation.
      const RECT original = dock->original;
      TakeDock(hwnd);
      SetWindowPos(hwnd, nullptr, original.left, original.top,
          original.right - original.left, original.bottom - original.top,
          SWP_NOACTIVATE | SWP_NOZORDER);
      Window window(hwnd);
      WindowDockDispatch::Emit(WindowUndockedEvent(window.GetId()));
    }
  } else if (message == WM_ACTIVATE || message == WM_WINDOWPOSCHANGED) {
    auto data = DockData(hwnd);
    SHAppBarMessage(message == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &data);
  } else if (message == WM_DISPLAYCHANGE || message == WM_DPICHANGED) {
    PositionDock(hwnd, *dock);
  }
}

}  // namespace nativeapi::detail
