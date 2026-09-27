#pragma once
#include <windows.h>

#include "../../foundation/geometry.h"

namespace nativeapi {

// Returns the DPI scale factor for the given window (1.0 at 96 DPI)
double GetScaleFactorForWindow(HWND hwnd);

// Returns the DPI scale factor for the given monitor (1.0 at 96 DPI)
double GetScaleFactorForMonitor(HMONITOR hmonitor);

// The library's screen coordinates: logical pixels in one space for all
// monitors, laid out as screen_layout_windows.h describes. Screen positions go
// through these; sizes are scaled by the factor of the monitor they are on.
Point PhysicalToLogicalPoint(POINT point);
POINT LogicalToPhysicalPoint(Point point);

// A screen rectangle in logical pixels: its top-left through the screen space,
// its size divided by `scale` (the window's, for a window).
Rectangle PhysicalToLogicalRect(const RECT& rect, double scale);

// The physical rectangle that logical `bounds` stand for: the top-left through
// the screen space, the size scaled by the factor of the monitor the rectangle
// ends up on (where a window moved there takes its DPI from).
RECT LogicalToPhysicalRect(Rectangle bounds);

// A monitor's bounds and work area in logical pixels. False for a monitor that
// is gone.
bool GetMonitorLogicalRects(HMONITOR monitor, Rectangle* bounds, Rectangle* work_area);

}  // namespace nativeapi
