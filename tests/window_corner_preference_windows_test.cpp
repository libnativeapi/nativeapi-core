// Explicit desktop regression. --preview also shows all four policies together
// for visual inspection; no synthetic input is sent.
#include <windows.h>
#include <dwmapi.h>
#include <winternl.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "nativeapi.h"

namespace {
int failures = 0;
void Check(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  failures += !ok;
}
void Settle(int milliseconds = 150) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
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
bool EqualRect(const RECT& first, const RECT& second) {
  return first.left == second.left && first.top == second.top &&
         first.right == second.right && first.bottom == second.bottom;
}
}  // namespace

int main(int argc, char** argv) {
  using namespace nativeapi;
  using SetDpiContext = BOOL(WINAPI*)(HANDLE);
  auto set_dpi = reinterpret_cast<SetDpiContext>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
  if (set_dpi) set_dpi(reinterpret_cast<HANDLE>(-4));
  Application::GetInstance();
  const bool supported = BuildNumber() >= 22000;
  Check(Window::IsCornerPreferenceSupported() == supported, "capability matches actual Windows build");
  Check(native_window_is_corner_preference_supported() == supported, "C ABI capability");
  Window window;
  HWND hwnd = static_cast<HWND>(window.GetNativeObject());
  window.SetTitle("nativeapi corner policy regression");
  Check(window.GetCornerPreference() == WindowCornerPreference::Default, "initial system default");
  for (auto preference : {WindowCornerPreference::DoNotRound, WindowCornerPreference::Round,
                          WindowCornerPreference::RoundSmall, WindowCornerPreference::Default}) {
    RECT before, client_before, after, client_after;
    GetWindowRect(hwnd, &before);
    GetClientRect(hwnd, &client_before);
    const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const auto ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool shadow = window.HasShadow();
    Check(window.SetCornerPreference(preference) == supported, "setter matches support");
    Check(window.GetCornerPreference() == (supported ? preference : WindowCornerPreference::Default),
          "accepted preference read back");
    GetWindowRect(hwnd, &after);
    GetClientRect(hwnd, &client_after);
    Check(EqualRect(before, after) && EqualRect(client_before, client_after), "corner change preserves geometry");
    Check(GetWindowLongPtrW(hwnd, GWL_STYLE) == style && GetWindowLongPtrW(hwnd, GWL_EXSTYLE) == ex_style &&
          window.HasShadow() == shadow, "corner change preserves title bar styles and shadow");
  }
  if (supported) {
    Check(window.SetCornerPreference(WindowCornerPreference::RoundSmall), "small rounding accepted");
    Check(!window.SetCornerPreference(static_cast<WindowCornerPreference>(-1)) &&
          !window.SetCornerPreference(static_cast<WindowCornerPreference>(4)), "invalid preferences rejected");
    Check(window.GetCornerPreference() == WindowCornerPreference::RoundSmall, "failed setter preserves preference");
    {
      Window wrapper(hwnd);
      Check(wrapper.GetCornerPreference() == WindowCornerPreference::RoundSmall, "wrappers share native policy");
      Check(wrapper.SetCornerPreference(WindowCornerPreference::DoNotRound) &&
            window.GetCornerPreference() == WindowCornerPreference::DoNotRound, "another wrapper changes shared policy");
    }
    Check(window.GetCornerPreference() == WindowCornerPreference::DoNotRound, "wrapper disposal preserves policy");
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    Settle();
    window.SetTitleBarStyle(TitleBarStyle::Hidden);
    Check(window.GetCornerPreference() == WindowCornerPreference::DoNotRound, "hidden title bar preserves preference");
    window.SetHasShadow(false);
    window.SetHasShadow(true);
    Check(window.GetCornerPreference() == WindowCornerPreference::DoNotRound, "shadow toggles preserve preference");
    window.SetTitleBarStyle(TitleBarStyle::Normal);
    window.Maximize();
    Settle();
    window.Unmaximize();
    Settle();
    Check(window.GetCornerPreference() == WindowCornerPreference::DoNotRound, "maximize restore preserves preference");
    window.SetFullScreen(true);
    Settle();
    window.SetFullScreen(false);
    Settle();
    Check(window.GetCornerPreference() == WindowCornerPreference::DoNotRound, "full screen restore preserves preference");
    ShowWindow(hwnd, SW_HIDE);
  }
  const auto handle = native_window_create_with_native_window(hwnd);
  Check(handle != 0, "C ABI wrapping constructor");
  Check(native_window_set_corner_preference(handle, NATIVE_WINDOW_CORNER_PREFERENCE_ROUND) == supported,
        "C ABI setter accepts enum");
  Check(native_window_get_corner_preference(handle) == (supported ? NATIVE_WINDOW_CORNER_PREFERENCE_ROUND
                                                               : NATIVE_WINDOW_CORNER_PREFERENCE_DEFAULT),
        "C ABI enum round trip");
  Check(!native_window_set_corner_preference(handle, static_cast<native_window_corner_preference_t>(99)),
        "C ABI rejects invalid enum");
  Check(native_window_get_corner_preference(handle) == (supported ? NATIVE_WINDOW_CORNER_PREFERENCE_ROUND
                                                               : NATIVE_WINDOW_CORNER_PREFERENCE_DEFAULT),
        "invalid C enum preserves accepted preference");
  native_window_free(handle);
  DestroyWindow(hwnd);
  Check(!window.SetCornerPreference(WindowCornerPreference::Round) &&
        window.GetCornerPreference() == WindowCornerPreference::Default, "destroyed window rejects setter and resets getter");
  Check(!native_window_set_corner_preference(0, NATIVE_WINDOW_CORNER_PREFERENCE_ROUND) &&
        native_window_get_corner_preference(0) == NATIVE_WINDOW_CORNER_PREFERENCE_DEFAULT, "invalid C handle is safe");

  if (supported && argc > 1 && std::string(argv[1]) == "--preview") {
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = 800, height = 540;
    const int left = work.left + (work.right - work.left - width) / 2;
    const int top = work.top + (work.bottom - work.top - height) / 2;
    Window background;
    HWND bg = static_cast<HWND>(background.GetNativeObject());
    background.SetTitle("nativeapi corner comparison");
    background.SetTitleBarStyle(TitleBarStyle::Hidden);
    background.SetHasShadow(false);
    background.SetBackgroundColor({20, 50, 60, 255});
    SetWindowPos(bg, HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    std::vector<std::unique_ptr<Window>> windows;
    const char* names[] = {"Default", "DoNotRound", "Round", "RoundSmall"};
    for (int index = 0; index < 4; ++index) {
      auto preview = std::make_unique<Window>();
      preview->SetTitle(names[index]);
      preview->SetBackgroundColor({245, 245, 245, 255});
      Check(preview->SetCornerPreference(static_cast<WindowCornerPreference>(index)), "preview preference accepted");
      HWND native = static_cast<HWND>(preview->GetNativeObject());
      const COLORREF caption = RGB(230, 180, 80);
      DwmSetWindowAttribute(native, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
      SetWindowPos(native, HWND_TOPMOST, left + 25 + (index % 2) * 390,
                   top + 25 + (index / 2) * 260, 360, 220, SWP_NOACTIVATE | SWP_SHOWWINDOW);
      windows.push_back(std::move(preview));
    }
    Settle(500);
    std::cout << "PREVIEW " << left << ' ' << top << ' ' << width << ' ' << height << std::endl;
    Settle(8000);
    for (auto& preview : windows) DestroyWindow(static_cast<HWND>(preview->GetNativeObject()));
    DestroyWindow(bg);
  }
  std::cout << (failures ? "FAILURES" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
}
