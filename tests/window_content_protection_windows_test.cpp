// Explicit desktop integration: captures one pixel inside our own two windows.
// No images are saved, and no synthetic input is sent.
#include <windows.h>
#include <dwmapi.h>
#include <winternl.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include "nativeapi.h"

namespace {
int failures = 0;
void Check(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  failures += !ok;
}
void Settle() {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
  while (std::chrono::steady_clock::now() < end) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) continue;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  DwmFlush();
}
DWORD BuildNumber() {
  using GetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  auto get = reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
  RTL_OSVERSIONINFOW info = {};
  info.dwOSVersionInfoSize = sizeof(info);
  return get && get(&info) == 0 ? info.dwBuildNumber : 0;
}
COLORREF CapturePixel(POINT point, HWND expected_owner) {
  // Abort the sample if another app covers the point. Never sample its pixels.
  if (WindowFromPoint(point) != expected_owner) return CLR_INVALID;
  HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, 1, 1);
  HGDIOBJ previous = SelectObject(memory, bitmap);
  const bool copied = BitBlt(memory, 0, 0, 1, 1, screen, point.x, point.y, SRCCOPY | CAPTUREBLT);
  const COLORREF color = copied ? GetPixel(memory, 0, 0) : CLR_INVALID;
  SelectObject(memory, previous);
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(nullptr, screen);
  return color;
}
bool NearColor(COLORREF color, COLORREF expected) {
  return color != CLR_INVALID && std::abs(int(GetRValue(color)) - int(GetRValue(expected))) <= 3 &&
      std::abs(int(GetGValue(color)) - int(GetGValue(expected))) <= 3 &&
      std::abs(int(GetBValue(color)) - int(GetBValue(expected))) <= 3;
}
}  // namespace

int main() {
  using namespace nativeapi;
  using SetDpiContext = BOOL(WINAPI*)(HANDLE);
  auto set_dpi = reinterpret_cast<SetDpiContext>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
  if (set_dpi) set_dpi(reinterpret_cast<HANDLE>(-4));
  Application::GetInstance();
  BOOL composition = FALSE;
  const bool supported = SUCCEEDED(DwmIsCompositionEnabled(&composition)) && composition;
  Check(Window::IsContentProtectionSupported() == supported &&
        native_window_is_content_protection_supported() == supported, "capability matches compositor");
  if (!supported) return failures ? 1 : 77;
  Window background, window;
  HWND bg = static_cast<HWND>(background.GetNativeObject());
  HWND hwnd = static_cast<HWND>(window.GetNativeObject());
  Check(!window.IsContentProtected(), "initially unprotected");
  DWORD affinity = WDA_NONE;
  Check(window.SetContentProtection(true) && GetWindowDisplayAffinity(hwnd, &affinity) &&
        affinity == (BuildNumber() >= 19041 ? 0x11u : WDA_MONITOR), "native affinity matches Windows version");
  {
    Window wrapper(hwnd);
    Check(wrapper.IsContentProtected() && wrapper.SetContentProtection(false) &&
          !window.IsContentProtected(), "wrappers read and change the same native policy");
    SetWindowDisplayAffinity(hwnd, WDA_MONITOR);
    Check(window.IsContentProtected() && wrapper.IsContentProtected(), "external native change read live");
  }
  Check(window.IsContentProtected(), "wrapper disposal keeps native policy");
  const auto handle = native_window_create_with_native_window(hwnd);
  Check(handle && native_window_set_content_protection(handle, false) &&
        !native_window_is_content_protected(handle) && !window.IsContentProtected(), "C ABI disables shared policy");
  Check(native_window_set_content_protection(handle, true) &&
        native_window_is_content_protected(handle) && window.IsContentProtected(), "C ABI enables shared policy");
  native_window_free(handle);
  HWND child = CreateWindowW(L"STATIC", L"", WS_CHILD, 0, 0, 20, 20, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
  {
    Window wrapper(child);
    Check(!wrapper.SetContentProtection(true) && !wrapper.IsContentProtected(), "non top-level window rejects protection");
  }
  DestroyWindow(child);
  background.SetTitleBarStyle(TitleBarStyle::Hidden);
  background.SetHasShadow(false);
  background.SetBackgroundColor(Color::FromRGBA(20, 60, 180, 255));
  window.SetTitleBarStyle(TitleBarStyle::Hidden);
  window.SetHasShadow(false);
  window.SetBackgroundColor(Color::FromRGBA(200, 40, 30, 255));
  Check(window.IsContentProtected(), "frame changes preserve native affinity");
  RECT work;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int left = (work.left + work.right - 360) / 2;
  const int top = (work.top + work.bottom - 240) / 2;
  SetWindowPos(bg, HWND_TOPMOST, left, top, 360, 240, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetWindowPos(hwnd, HWND_TOPMOST, left + 20, top + 20, 320, 200, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  POINT point = {left + 180, top + 120};
  window.SetContentProtection(false);
  Settle();
  Check(NearColor(CapturePixel(point, hwnd), RGB(200, 40, 30)), "unprotected capture contains our foreground content");
  Check(window.SetContentProtection(true), "visible window protection enabled");
  Settle();
  Check(NearColor(CapturePixel(point, hwnd), BuildNumber() >= 19041 ? RGB(20, 60, 180) : RGB(0, 0, 0)),
        "protected capture excludes content or blanks it on older Windows");
  Check(window.SetContentProtection(false), "visible protection disabled");
  Settle();
  Check(NearColor(CapturePixel(point, hwnd), RGB(200, 40, 30)), "disabling restores captured content");
  DestroyWindow(hwnd);
  DestroyWindow(bg);
  Check(!window.SetContentProtection(true) && !window.IsContentProtected(), "destroyed window returns false");
  Check(!native_window_set_content_protection(0, true) && !native_window_is_content_protected(0), "invalid C handle safe");
  std::cout << (failures ? "FAILURES" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
}
