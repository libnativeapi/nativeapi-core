#include "../../display.h"

#include <windows.h>

#include <string>
#include <vector>
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

// The monitor's own name (its EDID model, "DELL U2720Q"), found by matching the
// GDI device name against the active display paths. Empty when Windows has
// none, as for many built-in panels.
static std::wstring FriendlyMonitorName(const WCHAR* gdi_device_name) {
  UINT32 path_count = 0;
  UINT32 mode_count = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
      ERROR_SUCCESS) {
    return L"";
  }
  std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count,
                         modes.data(), nullptr) != ERROR_SUCCESS) {
    return L"";
  }
  for (UINT32 i = 0; i < path_count; ++i) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = paths[i].sourceInfo.adapterId;
    source.header.id = paths[i].sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
        wcscmp(source.viewGdiDeviceName, gdi_device_name) != 0) {
      continue;
    }
    DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
    target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    target.header.size = sizeof(target);
    target.header.adapterId = paths[i].targetInfo.adapterId;
    target.header.id = paths[i].targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS &&
        target.monitorFriendlyDeviceName[0] != L'\0') {
      return target.monitorFriendlyDeviceName;
    }
  }
  return L"";
}

std::string Display::GetName() const {
  if (!pimpl_->h_monitor_)
    return "";
  MONITORINFOEXW monitorInfo = GetMonitorInfoEx(pimpl_->h_monitor_);
  std::wstring name = FriendlyMonitorName(monitorInfo.szDevice);
  if (name.empty()) {
    // The monitor's driver description ("Generic PnP Monitor"), still better
    // than the adapter output name "\\.\DISPLAY1".
    DISPLAY_DEVICEW device = {};
    device.cb = sizeof(device);
    if (EnumDisplayDevicesW(monitorInfo.szDevice, 0, &device, 0)) {
      name = device.DeviceString;
    }
  }
  return name.empty() ? WCharArrayToString(monitorInfo.szDevice) : WStringToString(name);
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
