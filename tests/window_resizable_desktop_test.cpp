// Explicit desktop test for macOS and Windows; no synthetic input.
#include "nativeapi.h"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#else
#include <windows.h>
#endif
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Settle() {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
  while (std::chrono::steady_clock::now() < end) {
#ifdef __APPLE__
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
#else
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) continue;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
  }
}
void CheckSize(nativeapi::Size actual, nativeapi::Size expected, const char* label) {
  std::cout << "size=" << actual.width << 'x' << actual.height << std::endl;
  Check(std::abs(actual.width - expected.width) <= 2 &&
        std::abs(actual.height - expected.height) <= 2, label);
}
void CheckNativeResizable(nativeapi::Window& window, bool expected) {
#ifdef __APPLE__
  auto* native = (__bridge NSWindow*)window.GetNativeObject();
  const bool actual = (native.styleMask & NSWindowStyleMaskResizable) != 0;
#else
  auto native = static_cast<HWND>(window.GetNativeObject());
  const bool actual = (GetWindowLongPtrW(native, GWL_STYLE) & WS_THICKFRAME) != 0;
#endif
  Check(actual == expected, "native interactive-resize style");
}
void Run(bool before_show) {
  std::cout << "CASE before_show=" << before_show << std::endl;
  nativeapi::Window window;
  window.SetTitle("nativeapi resizable regression");
  window.SetContentSize({560, 360});
  if (before_show) {
    window.SetResizable(false);
    window.SetContentSize({620, 420});
  }
  window.Show();
  Settle();
  CheckSize(window.GetContentSize(), before_show ? nativeapi::Size{620, 420}
                                                : nativeapi::Size{560, 360}, "initial size");
  const auto initial = window.GetSize();
  window.SetResizable(false);
  Settle();
  Check(!window.IsResizable(), "getter disabled");
  CheckNativeResizable(window, false);
  CheckSize(window.GetSize(), initial, "disabling preserves frame");
  window.SetContentSize({740, 500});
  Settle();
  CheckSize(window.GetContentSize(), {740, 500}, "content growth while disabled");
  window.SetContentSize({450, 280});
  Settle();
  CheckSize(window.GetContentSize(), {450, 280}, "content shrink while disabled");
  window.SetSize({680, 480}, false);
  Settle();
  CheckSize(window.GetSize(), {680, 480}, "frame growth while disabled");
  window.SetSize({500, 350}, false);
  Settle();
  CheckSize(window.GetSize(), {500, 350}, "frame shrink while disabled");
  window.SetMinimumSize({400, 300});
  window.SetMaximumSize({850, 650});
  window.SetContentSize({550, 370});
  Settle();
  CheckSize(window.GetContentSize(), {550, 370}, "resize with limits while disabled");
  CheckNativeResizable(window, false);
  const auto fixed = window.GetSize();
  window.SetResizable(true);
  Settle();
  Check(window.IsResizable(), "getter reenabled");
  CheckNativeResizable(window, true);
  CheckSize(window.GetSize(), fixed, "reenabling preserves frame");
  CheckSize(window.GetMinimumSize(), {400, 300}, "minimum preserved");
  CheckSize(window.GetMaximumSize(), {850, 650}, "maximum preserved");
  window.SetContentSize({600, 400});
  Settle();
  CheckSize(window.GetContentSize(), {600, 400}, "resize after reenabling");
  window.Hide();
#ifdef __APPLE__
  auto* native = (__bridge NSWindow*)window.GetNativeObject();
  [native setReleasedWhenClosed:NO];
  [native close];
#else
  DestroyWindow(static_cast<HWND>(window.GetNativeObject()));
#endif
  Settle();
}
}  // namespace
int main() {
#ifdef __APPLE__
  @autoreleasepool {
#else
  SetProcessDPIAware();
#endif
  nativeapi::Application::GetInstance();
  Run(false);
  Run(true);
#ifdef __APPLE__
  }
#endif
  return failures ? 1 : 0;
}
