#include <windows.h>

#include <cstdlib>
#include <iostream>

#include "nativeapi.h"

// Run in a logged-on desktop session; no synthetic input is needed.
int main() {
  SetProcessDPIAware();
  nativeapi::Application::GetInstance();
  nativeapi::Window window;
  window.SetTitle("nativeapi maximize regression");
  auto hwnd = static_cast<HWND>(window.GetNativeObject());
  window.SetMinimumSize({320, 240});
  SetWindowPos(hwnd, nullptr, 100, 100, 640, 480,
               SWP_NOZORDER | SWP_NOACTIVATE);

  bool passed = true;
  for (auto style : {nativeapi::TitleBarStyle::Hidden,
                     nativeapi::TitleBarStyle::Normal,
                     nativeapi::TitleBarStyle::Hidden}) {
    window.SetTitleBarStyle(style);
    window.Show();
    RECT restored = {};
    GetWindowRect(hwnd, &restored);
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    window.Maximize();

    RECT frame = {};
    GetWindowRect(hwnd, &frame);
    const int border = GetSystemMetrics(SM_CXSIZEFRAME) +
                       GetSystemMetrics(SM_CXPADDEDBORDER);
    const auto& work = monitor.rcWork;
    const bool within_work_area =
        std::abs(frame.left - work.left) <= border &&
        std::abs(frame.top - work.top) <= border &&
        std::abs(frame.right - work.right) <= border &&
        std::abs(frame.bottom - work.bottom) <= border;
    passed &= window.IsMaximized() && !window.IsFullScreen() && within_work_area;
    if (style == nativeapi::TitleBarStyle::Hidden) {
      RECT client = {};
      GetClientRect(hwnd, &client);
      MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&client), 2);
      passed &= EqualRect(&client, &work) != FALSE;
    }
    std::cout << "style=" << static_cast<int>(style) << " maximized frame="
              << frame.left << ',' << frame.top << ',' << frame.right << ','
              << frame.bottom << " work=" << work.left << ',' << work.top << ','
              << work.right << ',' << work.bottom << '\n';

    window.Unmaximize();
    GetWindowRect(hwnd, &frame);
    passed &= !window.IsMaximized() && !window.IsFullScreen() &&
              EqualRect(&frame, &restored);
  }
  window.Hide();
  std::cout << (passed ? "PASS" : "FAIL") << ": maximize/work area/restore\n";
  return passed ? 0 : 1;
}
