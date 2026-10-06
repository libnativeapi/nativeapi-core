// Runs on its own desktop, never switched onto the user's screen. No input sent.
#include "nativeapi.h"
#include <windows.h>
#include <iostream>
#include <string>
static int failures;
static void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
int main() {
  const auto previous = GetThreadDesktop(GetCurrentThreadId());
  const std::wstring name = L"NativeAPI-DoubleClick-Test-" + std::to_wstring(GetCurrentProcessId());
  const auto desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  Check(desktop && SetThreadDesktop(desktop), "test owns a private desktop");
  if (failures) { if (desktop) CloseDesktop(desktop); return 1; }
  {
    nativeapi::Window window;
    HWND hwnd = static_cast<HWND>(window.GetNativeObject());
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    window.ShowInactive();
    Check(window.PerformTitleBarDoubleClick() && IsZoomed(hwnd), "hidden title bar maximizes using native command");
    const auto handle = native_window_create_with_native_window(hwnd);
    Check(native_window_perform_title_bar_double_click(handle) && !IsZoomed(hwnd), "C ABI double-click restores window");
    window.SetMaximizable(false);
    Check(!window.PerformTitleBarDoubleClick() && !IsZoomed(hwnd), "disabled maximize rejects gesture");
    window.SetMaximizable(true);
    window.Minimize();
    Check(IsIconic(hwnd) && !window.PerformTitleBarDoubleClick(), "minimized window rejects gesture");
    window.Restore();
    window.SetFullScreen(true);
    Check(!window.PerformTitleBarDoubleClick(), "full-screen window rejects gesture");
    window.SetFullScreen(false);
    Check(!native_window_perform_title_bar_double_click(0), "invalid handle rejected");
    DestroyWindow(hwnd);
    Check(!window.PerformTitleBarDoubleClick(), "destroyed native window rejected");
    native_window_free(handle);
  }
  if (SetThreadDesktop(previous)) CloseDesktop(desktop);
  return failures ? 1 : 0;
}
