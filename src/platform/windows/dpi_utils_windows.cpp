#include "dpi_utils_windows.h"

#include <cmath>
#include <vector>

#include "screen_layout_windows.h"

namespace nativeapi {

// Internal: per-monitor DPI via Shcore when available
double GetScaleFactorForMonitor(HMONITOR hmonitor) {
  if (!hmonitor)
    return 1.0;
  typedef HRESULT(WINAPI * GetDpiForMonitorFunc)(HMONITOR, int, UINT*, UINT*);
  static GetDpiForMonitorFunc pGetDpiForMonitor = nullptr;
  static bool resolved = false;
  if (!resolved) {
    HMODULE hShcore = LoadLibraryW(L"Shcore.dll");
    if (hShcore) {
      pGetDpiForMonitor =
          reinterpret_cast<GetDpiForMonitorFunc>(GetProcAddress(hShcore, "GetDpiForMonitor"));
    }
    resolved = true;
  }
  if (pGetDpiForMonitor) {
    UINT dpiX = 96, dpiY = 96;
    if (SUCCEEDED(pGetDpiForMonitor(hmonitor, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY))) {
      return static_cast<double>(dpiX) / 96.0;
    }
  }
  return 1.0;
}

double GetScaleFactorForWindow(HWND hwnd) {
  if (hwnd) {
    // Prefer GetDpiForWindow if available
    typedef UINT(WINAPI * GetDpiForWindowFunc)(HWND);
    static GetDpiForWindowFunc pGetDpiForWindow = nullptr;
    static bool resolved_win = false;
    if (!resolved_win) {
      HMODULE hUser32 = LoadLibraryW(L"user32.dll");
      if (hUser32) {
        pGetDpiForWindow =
            reinterpret_cast<GetDpiForWindowFunc>(GetProcAddress(hUser32, "GetDpiForWindow"));
      }
      resolved_win = true;
    }
    if (pGetDpiForWindow) {
      UINT dpi = pGetDpiForWindow(hwnd);
      if (dpi > 0) {
        return static_cast<double>(dpi) / 96.0;
      }
    }

    // Fallback: per-monitor DPI
    HMONITOR hmonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    double monitor_scale = GetScaleFactorForMonitor(hmonitor);
    if (monitor_scale > 0.0)
      return monitor_scale;
  }

  // Fallback: system DPI
  HDC hdc = GetDC(nullptr);
  if (hdc) {
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    if (dpiX > 0) {
      return static_cast<double>(dpiX) / 96.0;
    }
  }
  return 1.0;
}

namespace {

struct LaidOutMonitor {
  HMONITOR handle;
  RECT work;
};

// Enumerates the monitors and lays them out. Cheap enough to do per call, and
// never stale: monitors come and go, and their factors change.
std::vector<screen_layout::Monitor> CurrentLayout(std::vector<LaidOutMonitor>* handles = nullptr) {
  struct Collected {
    std::vector<screen_layout::Monitor> monitors;
    std::vector<LaidOutMonitor> handles;
  } collected;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto* c = reinterpret_cast<Collected*>(data);
        MONITORINFO info = {sizeof(info)};
        if (!GetMonitorInfoW(monitor, &info))
          return TRUE;
        const RECT& r = info.rcMonitor;
        c->monitors.push_back({{r.left, r.top, r.right, r.bottom},
                               GetScaleFactorForMonitor(monitor),
                               (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
                               {0, 0, 0, 0}});
        c->handles.push_back({monitor, info.rcWork});
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&collected));
  screen_layout::Arrange(collected.monitors);
  if (handles)
    *handles = std::move(collected.handles);
  return collected.monitors;
}

double SafeScale(double scale) {
  return scale > 0.0 ? scale : 1.0;
}

}  // namespace

Point PhysicalToLogicalPoint(POINT point) {
  return screen_layout::PhysicalToLogical(CurrentLayout(), point.x, point.y);
}

POINT LogicalToPhysicalPoint(Point point) {
  long x = 0, y = 0;
  screen_layout::LogicalToPhysical(CurrentLayout(), point, &x, &y);
  return {x, y};
}

Rectangle PhysicalToLogicalRect(const RECT& rect, double scale) {
  const Point origin = PhysicalToLogicalPoint({rect.left, rect.top});
  scale = SafeScale(scale);
  return {origin.x, origin.y, (rect.right - rect.left) / scale, (rect.bottom - rect.top) / scale};
}

RECT LogicalToPhysicalRect(Rectangle bounds) {
  const POINT origin = LogicalToPhysicalPoint({bounds.x, bounds.y});
  auto sized = [&](double scale) {
    scale = SafeScale(scale);
    return RECT{origin.x, origin.y, origin.x + static_cast<LONG>(std::lround(bounds.width * scale)),
                origin.y + static_cast<LONG>(std::lround(bounds.height * scale))};
  };
  const double at_origin =
      GetScaleFactorForMonitor(MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST));
  RECT rect = sized(at_origin);
  const double landing = GetScaleFactorForMonitor(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST));
  return landing == at_origin ? rect : sized(landing);
}

bool GetMonitorLogicalRects(HMONITOR monitor, Rectangle* bounds, Rectangle* work_area) {
  std::vector<LaidOutMonitor> handles;
  const auto monitors = CurrentLayout(&handles);
  for (size_t i = 0; i < monitors.size(); ++i) {
    if (handles[i].handle != monitor)
      continue;
    const auto& m = monitors[i];
    if (bounds)
      *bounds = m.logical;
    if (work_area) {
      const RECT& w = handles[i].work;
      const double s = SafeScale(m.scale);
      *work_area = {m.logical.x + (w.left - m.physical.left) / s,
                    m.logical.y + (w.top - m.physical.top) / s, (w.right - w.left) / s,
                    (w.bottom - w.top) / s};
    }
    return true;
  }
  return false;
}

}  // namespace nativeapi
