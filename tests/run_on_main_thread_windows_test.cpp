// RunOnMainThread() on Windows has to reach the thread that reads Win32 messages,
// not the thread that happened to load the library: a Flutter app with the default
// thread policy loads it from its UI isolate, which never pumps. Real desktop, real
// message loop, no input.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "nativeapi.h"

namespace {

int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}

}  // namespace

int main() {
  std::atomic<bool> platform_ready{false};
  std::atomic<DWORD> platform_thread{0};
  std::atomic<bool> ran{false};
  std::atomic<DWORD> ran_on{0};
  std::atomic<bool> main_thread_seen{false};

  // The platform thread owns the window and runs the message loop.
  std::thread platform([&] {
    platform_thread = GetCurrentThreadId();
    WNDCLASSW cls = {};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"NativeApiDispatcherThreadTestWindow";
    RegisterClassW(&cls);
    HWND window =
        CreateWindowW(cls.lpszClassName, L"nativeapi dispatcher thread", WS_OVERLAPPEDWINDOW, 60,
                      60, 300, 200, nullptr, nullptr, cls.hInstance, nullptr);
    ShowWindow(window, SW_SHOWNORMAL);
    platform_ready = true;

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    DestroyWindow(window);
  });

  while (!platform_ready.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  Check(platform_thread.load() != GetCurrentThreadId(),
        "the loader thread is not the window thread");
  Check(nativeapi::IsMainThreadDispatchSupported(), "main-thread dispatch is supported");
  Check(!nativeapi::IsMainThread(), "the loader thread is not reported as the UI thread");

  const bool queued = nativeapi::RunOnMainThread([&] {
    ran_on = GetCurrentThreadId();
    main_thread_seen = nativeapi::IsMainThread();
    ran = true;
  });
  Check(queued, "deferred work is accepted from the loader thread");

  for (int waited = 0; waited < 5000 && !ran.load(); ++waited) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  Check(ran.load(), "deferred work runs without the loader thread pumping");
  Check(ran_on.load() == platform_thread.load(), "deferred work runs on the window thread");
  Check(main_thread_seen.load(), "the window thread reports itself as the UI thread");
  Check(!nativeapi::IsMainThread(), "the loader thread keeps reporting itself as a worker");

  PostThreadMessageW(platform_thread.load(), WM_QUIT, 0, 0);
  platform.join();

  std::cout << (failures ? "FAILED " : "OK ") << failures << " failure(s)" << std::endl;
  return failures ? 1 : 0;
}
