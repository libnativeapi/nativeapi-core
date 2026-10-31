#include "../../screen_capture_backend.h"

namespace nativeapi::detail {
bool CaptureBackendSupported() { return false; }
bool CapturePickerSupported() { return false; }
bool CaptureEncodingSupported() { return false; }
std::shared_ptr<CaptureTarget> CaptureDisplayTarget(std::shared_ptr<Display>) { return nullptr; }
std::shared_ptr<CaptureTarget> CaptureWindowTarget(std::shared_ptr<Window>) { return nullptr; }
void SelectCaptureTarget(const CaptureSourceSelectionOptions&,
    std::function<void(bool, std::shared_ptr<CaptureTarget>)> callback) { callback(false, nullptr); }
std::shared_ptr<Image> CaptureFrameImage(const CaptureFrame&) { return nullptr; }
std::unique_ptr<CaptureEncoder> CreateCaptureEncoder(const std::string&, int, int,
    const ScreenRecordingOptions&, std::string& error) {
  error = "Screen recording is not supported on this platform";
  return nullptr;
}
}  // namespace nativeapi::detail
