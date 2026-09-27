#include "../../display.h"

#include <windows.h>
#include "dpi_utils_windows.h"
#include "string_utils_windows.h"

namespace nativeapi {

// Private implementation class
class Display::Impl {
 public:
  Impl() = default;
  Impl(HMONITOR monitor) : h_monitor_(monitor) {}

  const DisplayId id_ = IdAllocator::Allocate<Display>();
  HMONITOR h_monitor_ = nullptr;
};

Display::Display(void* display) : pimpl_(std::make_unique<Impl>()) {
  if (display) {
    pimpl_->h_monitor_ = (HMONITOR)display;
  }
}

Display::~Display() = default;

void* Display::GetNativeObjectInternal() const {
  return pimpl_->h_monitor_;
}

// Helper function to get monitor info
MONITORINFOEXW GetMonitorInfoEx(HMONITOR hMonitor) {
  MONITORINFOEXW monitorInfo;
  monitorInfo.cbSize = sizeof(MONITORINFOEXW);
  GetMonitorInfoW(hMonitor, &monitorInfo);
  return monitorInfo;
}

// Getters - directly read from HMONITOR
DisplayId Display::GetId() const {
  return pimpl_->id_;
}

std::string Display::GetName() const {
  if (!pimpl_->h_monitor_)
    return "";
  MONITORINFOEXW monitorInfo = GetMonitorInfoEx(pimpl_->h_monitor_);
  return WCharArrayToString(monitorInfo.szDevice);
}

// Bounds and work area are in the library's screen coordinates, which lay the
// monitors out side by side in logical pixels (see screen_layout_windows.h).
Point Display::GetPosition() const {
  Rectangle bounds = {0.0, 0.0, 0.0, 0.0};
  if (!pimpl_->h_monitor_ || !GetMonitorLogicalRects(pimpl_->h_monitor_, &bounds, nullptr))
    return {0.0, 0.0};
  return {bounds.x, bounds.y};
}

Size Display::GetSize() const {
  Rectangle bounds = {0.0, 0.0, 0.0, 0.0};
  if (!pimpl_->h_monitor_ || !GetMonitorLogicalRects(pimpl_->h_monitor_, &bounds, nullptr))
    return {0.0, 0.0};
  return {bounds.width, bounds.height};
}

Rectangle Display::GetWorkArea() const {
  Rectangle work_area = {0.0, 0.0, 0.0, 0.0};
  if (!pimpl_->h_monitor_ || !GetMonitorLogicalRects(pimpl_->h_monitor_, nullptr, &work_area))
    return {0.0, 0.0, 0.0, 0.0};
  return work_area;
}

double Display::GetScaleFactor() const {
  if (!pimpl_->h_monitor_)
    return 1.0;
  double scale = GetScaleFactorForMonitor(pimpl_->h_monitor_);
  return (scale > 0.0) ? scale : 1.0;
}

bool Display::IsPrimary() const {
  if (!pimpl_->h_monitor_)
    return false;
  MONITORINFOEXW monitorInfo = GetMonitorInfoEx(pimpl_->h_monitor_);
  return (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;
}

DisplayOrientation Display::GetOrientation() const {
  if (!pimpl_->h_monitor_)
    return DisplayOrientation::kPortrait;
  Size size = GetSize();
  return (size.width > size.height) ? DisplayOrientation::kLandscape
                                    : DisplayOrientation::kPortrait;
}

int Display::GetRefreshRate() const {
  return 60;  // Default refresh rate, would need additional Windows APIs to get
              // actual value
}

int Display::GetBitDepth() const {
  return 32;  // Default bit depth for modern displays
}

}  // namespace nativeapi
