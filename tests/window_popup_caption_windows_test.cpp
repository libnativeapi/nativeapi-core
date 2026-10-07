// leanflutter/window_manager#569 (#77): a window created with WS_POPUP gets a
// caption from SetTitleBarStyle(Normal), also when that happens in full
// screen, and leaving full screen otherwise restores the style it had. The
// window is shown without activation at the bottom of the Z order; no input.
#include <windows.h>
#include <iostream>
#include "nativeapi.h"

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Pump() {
  MSG message;
  for (int i = 0; i < 20; ++i) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(10);
  }
}
HWND CreatePopup() {
  WNDCLASSW cls = {};
  cls.lpfnWndProc = DefWindowProcW;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH));
  cls.lpszClassName = L"NativeApiPopupCaptionTest";
  RegisterClassW(&cls);
  HWND hwnd = CreateWindowExW(0, cls.lpszClassName, L"nativeapi popup caption",
                              WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, 200, 200, 500, 320,
                              nullptr, nullptr, cls.hInstance, nullptr);
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  Pump();
  return hwnd;
}
bool HasCaptionBar(HWND hwnd) {
  RECT window, client;
  GetWindowRect(hwnd, &window);
  GetClientRect(hwnd, &client);
  POINT origin = {0, 0};
  ClientToScreen(hwnd, &origin);
  // A caption puts the client area well below the top edge of the frame.
  return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION &&
         origin.y - window.top >= GetSystemMetrics(SM_CYCAPTION);
}
}  // namespace

int main() {
  SetProcessDPIAware();
  nativeapi::Application::GetInstance();

  HWND hwnd = CreatePopup();
  nativeapi::Window window(hwnd);
  const LONG_PTR original = GetWindowLongPtrW(hwnd, GWL_STYLE);
  RECT before;
  GetWindowRect(hwnd, &before);
  Check(!HasCaptionBar(hwnd), "a WS_POPUP window starts without a caption");

  window.SetFullScreen(true);
  Pump();
  Check(window.IsFullScreen(), "it goes full screen");
  window.SetFullScreen(false);
  Pump();
  RECT after;
  GetWindowRect(hwnd, &after);
  Check(!window.IsFullScreen() && GetWindowLongPtrW(hwnd, GWL_STYLE) == original,
        "leaving full screen restores its own style");
  Check(EqualRect(&before, &after), "and its bounds");

  window.SetTitleBarStyle(nativeapi::TitleBarStyle::Normal);
  Pump();
  Check(HasCaptionBar(hwnd), "TitleBarStyle::Normal gives it a caption");
  DestroyWindow(hwnd);

  // The same request made while it is full screen takes effect on leaving it.
  hwnd = CreatePopup();
  nativeapi::Window second(hwnd);
  second.SetFullScreen(true);
  Pump();
  second.SetTitleBarStyle(nativeapi::TitleBarStyle::Normal);
  Pump();
  Check((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) != WS_CAPTION,
        "in full screen the caption stays hidden");
  second.SetFullScreen(false);
  Pump();
  Check(HasCaptionBar(hwnd), "leaving full screen brings the caption set meanwhile");
  DestroyWindow(hwnd);

  std::cout << (failures ? "FAILED" : "OK") << std::endl;
  return failures ? 1 : 0;
}
