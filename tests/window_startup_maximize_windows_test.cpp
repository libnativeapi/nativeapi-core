// leanflutter/window_manager#412 / #572 (#75): Maximize() on a window that is
// not shown yet keeps it hidden, and the host's own ShowWindow(SW_SHOWNORMAL)
// - a Flutter runner's, on its first frame - shows it maximized from its very
// first visible frame instead of restoring it. No input.
#include <windows.h>
#include <commctrl.h>
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
  for (int i = 0; i < 30; ++i) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(10);
  }
}
// Whether the window was maximized at the moment it first became visible.
int shown_zoomed = -1;
LRESULT CALLBACK Watch(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
  if (message == WM_WINDOWPOSCHANGED && shown_zoomed < 0 &&
      (reinterpret_cast<WINDOWPOS*>(lp)->flags & SWP_SHOWWINDOW))
    shown_zoomed = IsZoomed(hwnd) ? 1 : 0;
  return DefSubclassProc(hwnd, message, wp, lp);
}
HWND Create() {
  WNDCLASSW cls = {};
  cls.lpfnWndProc = DefWindowProcW;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH));
  cls.lpszClassName = L"NativeApiStartupMaximizeTest";
  RegisterClassW(&cls);
  return CreateWindowExW(0, cls.lpszClassName, L"nativeapi startup maximize", WS_OVERLAPPEDWINDOW,
                         200, 200, 600, 400, nullptr, nullptr, cls.hInstance, nullptr);
}
}  // namespace

int main() {
  SetProcessDPIAware();
  nativeapi::Application::GetInstance();

  HWND hwnd = Create();
  SetWindowSubclass(hwnd, Watch, 1, 0);
  nativeapi::Window window(hwnd);
  window.Maximize();
  Pump();
  Check(!IsWindowVisible(hwnd), "Maximize() leaves a window that is not shown yet hidden");
  Check(window.IsMaximized(), "but reports it maximized already");
  ShowWindow(hwnd, SW_SHOWNORMAL);  // The host's show, as a Flutter runner's.
  Pump();
  Check(IsWindowVisible(hwnd), "the host's show shows it");
  Check(shown_zoomed == 1, "maximized from its first visible frame");
  Check(IsZoomed(hwnd) && window.IsMaximized(), "and it stays maximized");
  window.Unmaximize();
  Pump();
  Check(!IsZoomed(hwnd), "Unmaximize() restores it");
  DestroyWindow(hwnd);

  hwnd = Create();
  nativeapi::Window second(hwnd);
  second.Maximize();
  second.Unmaximize();
  Check(!second.IsMaximized(), "Unmaximize() before the show takes the request back");
  ShowWindow(hwnd, SW_SHOWNORMAL);
  Pump();
  Check(IsWindowVisible(hwnd) && !IsZoomed(hwnd), "and the window then shows restored");
  DestroyWindow(hwnd);

  std::cout << (failures ? "FAILED" : "OK") << std::endl;
  return failures ? 1 : 0;
}
