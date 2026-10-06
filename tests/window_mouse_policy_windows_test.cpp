// Native policy and lifecycle on hidden HWNDs. No window shown and no input sent.
#include "nativeapi.h"
#include <windows.h>
#include <iostream>

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Pump() {
  MSG message;
  const auto deadline = GetTickCount64() + 60;
  while (GetTickCount64() < deadline) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    Sleep(1);
  }
}
}
int main() {
  Check(nativeapi::Window::IsMouseMoveForwardingSupported() && native_window_is_mouse_move_forwarding_supported(),
        "Windows movement-forwarding capability");
  for (bool layered : {false, true}) {
    nativeapi::Window window;
    HWND hwnd = static_cast<HWND>(window.GetNativeObject());
    Check(IsWindow(hwnd) && !IsWindowVisible(hwnd), "hidden native window");
    if (layered) {
      SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
      SetLayeredWindowAttributes(hwnd, 0, 37, LWA_ALPHA);
    }
    const LONG_PTR previous = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const auto handle = native_window_create_with_native_window(hwnd);
    Check(window.SetIgnoreMouseEvents(true, true), "enable pass-through with forwarding");
    const LONG_PTR ignored = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    Check((ignored & (WS_EX_TRANSPARENT | WS_EX_LAYERED)) == (WS_EX_TRANSPARENT | WS_EX_LAYERED),
          "native layered transparent hit testing");
    Check(window.IsMouseMoveForwardingEnabled() && native_window_is_mouse_move_forwarding_enabled(handle) &&
          native_window_is_ignore_mouse_events(handle), "native policy shared by wrappers");
    Pump();
    Check(native_window_set_ignore_mouse_events(handle, true, false) && window.IsIgnoreMouseEvents() &&
          !window.IsMouseMoveForwardingEnabled(), "turn off movement while retaining pass-through");
    Check(native_window_set_ignore_mouse_events(handle, false, true) && !window.IsIgnoreMouseEvents() &&
          !window.IsMouseMoveForwardingEnabled(), "disable pass-through also stops forwarding");
    Check(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) == previous, "original extended styles restored");
    if (layered) {
      COLORREF key = 0; BYTE alpha = 0; DWORD flags = 0;
      Check(GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags) && alpha == 37 && flags == LWA_ALPHA,
            "host's existing layered alpha preserved");
    }
    Check(window.SetIgnoreMouseEvents(true) && !window.IsMouseMoveForwardingEnabled(),
          "C++ default forward false");
    Check(window.SetIgnoreMouseEvents(true, true), "reenable forwarding");
    native_window_free(handle);
    Check(window.IsMouseMoveForwardingEnabled(), "native policy survives wrapper release");
    const auto live = native_window_create_with_native_window(hwnd);
    DestroyWindow(hwnd); Pump();
    Check(!window.IsMouseMoveForwardingEnabled() && !window.IsIgnoreMouseEvents() &&
          !window.SetIgnoreMouseEvents(true, true) && !native_window_set_ignore_mouse_events(live, true, true),
          "destroyed native windows reject policy and stop sampler");
    native_window_free(live);
  }
  Check(!native_window_set_ignore_mouse_events(0, true, true) && !native_window_is_mouse_move_forwarding_enabled(0),
        "invalid C handles rejected");
  std::cout << (failures ? "FAILED" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
}
