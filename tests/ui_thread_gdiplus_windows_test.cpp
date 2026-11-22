// Regression test: GdiplusStartup spawns a background thread that owns a hidden
// top-level "GDI+ Hook Window Class" window without WS_EX_TOOLWINDOW. Starting
// GDI+ (e.g. for a tray icon) before creating a hidden non-taskbar NativeAPI
// window must never trick FindUiThreadId() or WindowManager into treating the
// GDI+ background thread as the application's UI thread.
#include <windows.h>
#include <gdiplus.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>

#include "nativeapi.h"
#include "../src/foundation/dispatcher.h"
#include "../src/platform/windows/ui_thread_windows.h"

#pragma comment(lib, "gdiplus.lib")

namespace {
using Clock = std::chrono::steady_clock;
int failures = 0;

void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}

void Pump() {
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

bool Until(const std::function<bool()>& done, int milliseconds = 3000) {
  const auto start = Clock::now();
  while (!done()) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    if (elapsed >= milliseconds) break;
    Pump();
    Sleep(5);
  }
  return done();
}
}  // namespace

int main() {
  const DWORD main_tid = GetCurrentThreadId();

  ULONG_PTR gdiplus_token = 0;
  Gdiplus::GdiplusStartupInput input;
  Check(Gdiplus::GdiplusStartup(&gdiplus_token, &input, nullptr) == Gdiplus::Ok,
        "GdiplusStartup succeeds before any application window exists");

  // Wait briefly for GDI+'s background notification window to appear if enabled.
  Sleep(30);
  Pump();

  Check(nativeapi::detail::FindUiThreadId() == 0,
        "FindUiThreadId ignores GDI+ background hook window when no app window exists");
  Check(nativeapi::IsMainThread(),
        "IsMainThread stays true on the main thread after GdiplusStartup");

  auto panel = std::make_shared<nativeapi::Window>();
  panel->SetFrameless(true);
  panel->SetVisibleInTaskbar(false);  // Adds WS_EX_TOOLWINDOW while still hidden.

  Check(nativeapi::detail::FindUiThreadId() == main_tid,
        "FindUiThreadId selects the main thread owning a hidden WS_EX_TOOLWINDOW NativeAPI window");
  Check(nativeapi::IsMainThread(),
        "IsMainThread stays true on the main thread with a hidden tool-style NativeAPI window");

  std::atomic<DWORD> callback_tid{0};
  const auto listener = panel->AddListener<nativeapi::WindowCreatedEvent>(
      [&](const nativeapi::WindowCreatedEvent&) {
        callback_tid.store(GetCurrentThreadId());
      });

  panel->Show();
  Check(Until([&] { return callback_tid.load() != 0; }),
        "WindowCreatedEvent fires when the hidden panel is shown");
  Check(callback_tid.load() == main_tid,
        "WinEvent callbacks run on the main UI thread rather than GDI+'s background thread");

  panel->RemoveListener(listener);
  panel->Close();
  Pump();
  Gdiplus::GdiplusShutdown(gdiplus_token);

  std::cout << (failures ? "FAILED " : "ALL PASS ") << failures << " failure(s)" << std::endl;
  return failures ? 1 : 0;
}
