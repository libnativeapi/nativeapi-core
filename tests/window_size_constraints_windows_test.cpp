#include <windows.h>
#include <cmath>
#include <iostream>
#include <memory>

#include "../src/window_registry.h"
#include "nativeapi.h"

namespace {
int failures = 0;
void Check(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  failures += !ok;
}
MINMAXINFO Query(HWND hwnd) {
  MINMAXINFO info = {};
  info.ptMinTrackSize = {17, 19};
  info.ptMaxTrackSize = {3000, 3001};
  SendMessageW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&info));
  return info;
}
void CheckLimits(HWND hwnd, const char* name) {
  const double scale = GetDpiForWindow(hwnd) / 96.0;
  const auto info = Query(hwnd);
  Check(info.ptMinTrackSize.x == std::lround(400 * scale) &&
            info.ptMinTrackSize.y == std::lround(300 * scale) &&
            info.ptMaxTrackSize.x == std::lround(1000 * scale) &&
            info.ptMaxTrackSize.y == std::lround(700 * scale),
        name);
}
}  // namespace

// Explicit desktop test. Checks the native sizing protocol and actual window
// resizes without sending mouse or keyboard input.
int main() {
  SetProcessDPIAware();
  nativeapi::Application::GetInstance();
  HWND hwnd = nullptr;
  {
    nativeapi::Window window;
    hwnd = static_cast<HWND>(window.GetNativeObject());
    window.SetTitle("nativeapi Windows size constraint regression");
    window.SetMinimumSize({400, 300});
    window.SetMaximumSize({1000, 700});
    CheckLimits(hwnd, "constraints before registration and mapping");
    auto other = std::make_shared<nativeapi::Window>(hwnd);
    nativeapi::WindowRegistry::GetInstance().Add(window.GetId(), other);
    CheckLimits(hwnd, "constraints independent of registry wrapper");
    window.Show();
    window.SetSize({100, 100}, false);
    auto size = window.GetSize();
    Check(std::abs(size.width - 400) < 2 && std::abs(size.height - 300) < 2,
          "native resize enforces minimum");
    window.SetSize({1400, 1000}, false);
    size = window.GetSize();
    Check(std::abs(size.width - 1000) < 2 && std::abs(size.height - 700) < 2,
          "native resize enforces maximum");
    window.Maximize();
    window.Unmaximize();
    CheckLimits(hwnd, "constraints survive maximize and restore");
    window.SetMinimumSize({420, 0});
    window.SetMaximumSize({0, 650});
    const double scale = GetDpiForWindow(hwnd) / 96.0;
    auto info = Query(hwnd);
    Check(info.ptMinTrackSize.x == std::lround(420 * scale) && info.ptMinTrackSize.y == 19 &&
              info.ptMaxTrackSize.x == 3000 && info.ptMaxTrackSize.y == std::lround(650 * scale),
          "independent dimension limits");
    window.SetMinimumSize({0, 0});
    window.SetMaximumSize({-1, -1});
    info = Query(hwnd);
    Check(info.ptMinTrackSize.x == 17 && info.ptMinTrackSize.y == 19 &&
              info.ptMaxTrackSize.x == 3000 && info.ptMaxTrackSize.y == 3001,
          "clearing limits preserves native defaults");
    window.Hide();
    nativeapi::WindowRegistry::GetInstance().Remove(window.GetId());
  }
  // The message callback must not outlive the wrapper whose state it reads.
  Query(hwnd);
  DestroyWindow(hwnd);
  return failures ? 1 : 0;
}
