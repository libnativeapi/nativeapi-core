// Verifies Issue #30 fixes on Windows:
// 1. Rapid SetWindowPos left-edge bursts coalesce WindowMovedEvent /
//    WindowResizedEvent instead of emitting synchronously on every step, and
//    deliver the exact final geometry once the burst settles.
// 2. Inside WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE, per-step WM_WINDOWPOSCHANGED
//    does not emit synchronously, and WM_EXITSIZEMOVE flushes the final
//    geometry immediately.
// 3. With zero listeners, WindowManager::HasAnyListeners() is false and
//    re-adding a listener delivers isolated changes immediately without stale
//    events.
// 4. WindowMessageDispatcher's message-filtered RegisterHandler only invokes
//    the handler for subscribed messages.

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/platform/windows/window_message_dispatcher.h"
#include "../src/window.h"
#include "../src/window_manager.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}

void PumpFor(DWORD ms) {
  const ULONGLONG deadline = GetTickCount64() + ms;
  MSG msg = {};
  while (true) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (GetTickCount64() >= deadline) {
      break;
    }
    Sleep(5);
  }
}

void PumpPending() {
  MSG msg = {};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

LRESULT CALLBACK TestWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int main() {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = TestWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"NativeAPIIssue30TestWindow";
  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Issue30 Test", WS_OVERLAPPEDWINDOW, 100, 100,
                              400, 300, nullptr, nullptr, wc.hInstance, nullptr);
  Check(hwnd != nullptr, "create test window");
  if (!hwnd) {
    return 1;
  }

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  PumpFor(30);

  auto& manager = nativeapi::WindowManager::GetInstance();
  nativeapi::Window window(hwnd);
  const auto id = window.GetId();
  Check(id != nativeapi::IdAllocator::kInvalidId, "window has valid id");
  Check(!manager.HasAnyListeners(), "zero listeners initially");

  // 1. Message-filtered WindowMessageDispatcher test.
  {
    auto& dispatcher = nativeapi::WindowMessageDispatcher::GetInstance();
    std::atomic<int> minmax_calls{0};
    std::atomic<int> other_calls{0};
    const int hid = dispatcher.RegisterHandler(
        hwnd, {WM_GETMINMAXINFO},
        [&](HWND, UINT msg, WPARAM, LPARAM) -> std::optional<LRESULT> {
          if (msg == WM_GETMINMAXINFO) {
            ++minmax_calls;
          } else {
            ++other_calls;
          }
          return std::nullopt;
        });
    Check(hid > 0, "register filtered handler");

    for (int i = 0; i < 50; ++i) {
      SendMessageW(hwnd, WM_MOUSEMOVE, 0, 0);
      SendMessageW(hwnd, WM_USER + 7, 0, 0);
    }
    Check(other_calls.load() == 0, "filtered handler never called for unrelated messages");
    Check(minmax_calls.load() == 0, "filtered handler not called before WM_GETMINMAXINFO");

    MINMAXINFO mmi = {};
    SendMessageW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi));
    Check(minmax_calls.load() == 1, "filtered handler called for WM_GETMINMAXINFO");
    Check(other_calls.load() == 0, "filtered handler still 0 for unrelated messages");

    Check(dispatcher.UnregisterHandler(hid), "unregister filtered handler");
    SendMessageW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi));
    Check(minmax_calls.load() == 1, "unregistered handler no longer called");
  }

  // 2. Zero-listener fast path: moves while unsubscribed should not emit stale
  //    events when a listener is later attached.
  SetWindowPos(hwnd, nullptr, 140, 120, 420, 320, SWP_NOZORDER | SWP_NOACTIVATE);
  PumpFor(20);
  Check(!manager.HasAnyListeners(), "still zero listeners after unsubscribed move/resize");

  int moved_count = 0;
  int resized_count = 0;
  nativeapi::Point last_pos = {};
  nativeapi::Size last_size = {};

  const size_t moved_lid = manager.AddListener<nativeapi::WindowMovedEvent>([&](const auto& e) {
    if (e.GetWindowId() == id) {
      ++moved_count;
      last_pos = e.GetNewPosition();
    }
  });
  const size_t resized_lid =
      manager.AddListener<nativeapi::WindowResizedEvent>([&](const auto& e) {
        if (e.GetWindowId() == id) {
          ++resized_count;
          last_size = e.GetNewSize();
        }
      });
  Check(manager.HasAnyListeners(), "HasAnyListeners true after AddListener");

  // 3. Isolated single resize (without moving) emits immediately with no stale move.
  SetWindowPos(hwnd, nullptr, 0, 0, 460, 320, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  PumpPending();
  Check(resized_count == 1, "isolated resize emits immediately");
  Check(moved_count == 0, "isolated resize does not emit stale move from pre-listener change");

  // 4. Rapid left-edge SetWindowPos burst (60 steps) must coalesce.
  moved_count = 0;
  resized_count = 0;
  for (int i = 0; i < 60; ++i) {
    const int delta = (i % 2 == 0) ? 1 : 0;
    SetWindowPos(hwnd, nullptr, 140 + delta, 120, 460 - delta, 320,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  const int sync_moved = moved_count;
  const int sync_resized = resized_count;
  Check(sync_moved < 10, "rapid burst coalesces synchronous WindowMovedEvent");
  Check(sync_resized < 10, "rapid burst coalesces synchronous WindowResizedEvent");

  // Wait for coalesce timer to flush final geometry.
  PumpFor(120);
  Check(moved_count >= 1 && moved_count < 15, "burst delivers coalesced WindowMovedEvent");
  Check(resized_count >= 1 && resized_count < 15, "burst delivers coalesced WindowResizedEvent");
  const nativeapi::Size expected_burst_size = window.GetSize();
  Check(std::abs(last_size.width - expected_burst_size.width) < 1.0 &&
            std::abs(last_size.height - expected_burst_size.height) < 1.0,
        "coalesced final size matches actual window size");

  // 5. Interactive WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE: zero synchronous events
  //    during rapid steps, immediate flush of final geometry on WM_EXITSIZEMOVE.
  PumpFor(80);
  moved_count = 0;
  resized_count = 0;
  SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
  for (int i = 1; i <= 20; ++i) {
    SetWindowPos(hwnd, nullptr, 140 + i, 120, 460 + i * 2, 320, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  Check(moved_count == 0, "no synchronous WindowMovedEvent inside WM_ENTERSIZEMOVE");
  Check(resized_count == 0, "no synchronous WindowResizedEvent inside WM_ENTERSIZEMOVE");

  SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
  Check(moved_count == 1, "WM_EXITSIZEMOVE flushes final WindowMovedEvent immediately");
  Check(resized_count == 1, "WM_EXITSIZEMOVE flushes final WindowResizedEvent immediately");
  const nativeapi::Point expected_exit_pos = window.GetPosition();
  const nativeapi::Size expected_exit_size = window.GetSize();
  Check(std::abs(last_pos.x - expected_exit_pos.x) < 1.0 &&
            std::abs(last_pos.y - expected_exit_pos.y) < 1.0,
        "WM_EXITSIZEMOVE position matches final window position");
  Check(std::abs(last_size.width - expected_exit_size.width) < 1.0 &&
            std::abs(last_size.height - expected_exit_size.height) < 1.0,
        "WM_EXITSIZEMOVE size matches final window size");

  manager.RemoveListener(moved_lid);
  manager.RemoveListener(resized_lid);
  Check(!manager.HasAnyListeners(), "HasAnyListeners false after removing listeners");

  DestroyWindow(hwnd);
  PumpFor(20);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);

  if (g_failures == 0) {
    std::printf("PASS: window_geometry_coalesce_windows_test\n");
  }
  return g_failures == 0 ? 0 : 1;
}
