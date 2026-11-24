// Regression test for libnativeapi/nativeapi#53:
// With TitleBarStyle::Hidden, WM_NCCALCSIZE for a maximized window must query
// the monitor for the incoming proposed rect (MonitorFromRect(&proposed))
// rather than MonitorFromWindow(hwnd), which still points at the minimized
// placeholder (-32000, -32000) while restoring from minimized on a secondary
// monitor.

#include <windows.h>

#include <cstdio>
#include <vector>

#include "../src/window.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}

void PumpFor(DWORD ms) {
  const ULONGLONG end = GetTickCount64() + ms;
  MSG msg = {};
  while (true) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (GetTickCount64() >= end) {
      break;
    }
    Sleep(5);
  }
}

RECT GetClientRectInScreen(HWND hwnd) {
  RECT client = {};
  GetClientRect(hwnd, &client);
  POINT tl = {client.left, client.top};
  POINT br = {client.right, client.bottom};
  ClientToScreen(hwnd, &tl);
  ClientToScreen(hwnd, &br);
  return {tl.x, tl.y, br.x, br.y};
}

bool RectsEqual(const RECT& a, const RECT& b) {
  return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

LRESULT CALLBACK TestWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int main() {
  SetProcessDPIAware();

  WNDCLASSW wc = {};
  wc.lpfnWndProc = TestWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"NativeAPIIssue53TestWindow";
  RegisterClassW(&wc);

  std::vector<MONITORINFO> monitors;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR hmon, HDC, LPRECT, LPARAM data) -> BOOL {
        auto* list = reinterpret_cast<std::vector<MONITORINFO>*>(data);
        MONITORINFO info = {sizeof(info)};
        if (GetMonitorInfoW(hmon, &info)) {
          list->push_back(info);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&monitors));
  Check(!monitors.empty(), "at least one monitor present");
  if (monitors.empty()) {
    return 1;
  }

  // Pick a secondary monitor if available; otherwise use the primary monitor.
  MONITORINFO target_monitor = monitors.front();
  bool has_secondary = false;
  for (const auto& m : monitors) {
    if (!(m.dwFlags & MONITORINFOF_PRIMARY)) {
      target_monitor = m;
      has_secondary = true;
      break;
    }
  }

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Issue53 Test", WS_OVERLAPPEDWINDOW,
                              target_monitor.rcWork.left + 40, target_monitor.rcWork.top + 40,
                              640, 480, nullptr, nullptr, wc.hInstance, nullptr);
  Check(hwnd != nullptr, "create test window");
  if (!hwnd) {
    return 1;
  }

  nativeapi::Window window(hwnd);
  window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
  ShowWindow(hwnd, SW_SHOWMAXIMIZED);
  PumpFor(80);

  const RECT before_client = GetClientRectInScreen(hwnd);
  Check(RectsEqual(before_client, target_monitor.rcWork),
        "maximized hidden-title-bar client rect matches target monitor work area");

  // Direct WM_NCCALCSIZE check with a proposed rect on each monitor while the
  // window is zoomed: rgrc[0] must resolve to the proposed rect's monitor.
  for (const auto& m : monitors) {
    NCCALCSIZE_PARAMS params = {};
    params.rgrc[0] = {m.rcWork.left - 8, m.rcWork.top - 8, m.rcWork.right + 8,
                      m.rcWork.bottom + 8};
    SendMessageW(hwnd, WM_NCCALCSIZE, TRUE, reinterpret_cast<LPARAM>(&params));
    Check(RectsEqual(params.rgrc[0], m.rcWork),
          "WM_NCCALCSIZE resolves monitor from proposed rect");
  }

  // Minimize and restore: client rect and hit testing must stay on target_monitor.
  ShowWindow(hwnd, SW_MINIMIZE);
  PumpFor(80);
  ShowWindow(hwnd, SW_RESTORE);
  PumpFor(120);

  Check(IsZoomed(hwnd) != FALSE, "window returns to maximized state after restore");
  const RECT after_client = GetClientRectInScreen(hwnd);
  Check(RectsEqual(after_client, target_monitor.rcWork),
        has_secondary
            ? "restored maximized window on secondary monitor keeps secondary work area"
            : "restored maximized window keeps monitor work area");

  const int mid_x = (target_monitor.rcWork.left + target_monitor.rcWork.right) / 2;
  const int mid_y = (target_monitor.rcWork.top + target_monitor.rcWork.bottom) / 2;
  const LRESULT hit = SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(mid_x, mid_y));
  Check(hit == HTCLIENT, "WM_NCHITTEST at center of restored window returns HTCLIENT");

  DestroyWindow(hwnd);
  PumpFor(20);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);

  if (g_failures == 0) {
    std::printf("PASS: window_hidden_titlebar_nccalcsize_windows_test (monitors=%zu)\n",
                monitors.size());
  }
  return g_failures == 0 ? 0 : 1;
}
