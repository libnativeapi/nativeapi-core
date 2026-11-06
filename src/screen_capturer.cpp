#include "screen_capturer.h"

#include <atomic>
#include <thread>
#include "foundation/dispatcher.h"
#include "screen_capture_backend.h"
#include "window_manager.h"

namespace nativeapi {
namespace {
std::atomic<bool> picker_busy{false};
template <typename F> void Deliver(F callback) {
  // Dispatcher teardown revokes delivery; never call a UI callback on the worker.
  RunOnMainThread(std::move(callback));
}
}  // namespace
bool ScreenCapturer::IsSupported() {
  try { return IsMainThreadDispatchSupported() && detail::CaptureBackendSupported(); }
  catch (...) { return false; }
}
bool ScreenCapturer::IsSourceSelectionSupported() {
  try { return IsSupported() && detail::CapturePickerSupported(); }
  catch (...) { return false; }
}
bool ScreenCapturer::IsDirectCaptureSupported(CaptureSourceType type) {
  return (type == CaptureSourceType::Display || type == CaptureSourceType::Window) && IsSupported();
}
void ScreenCapturer::SelectSource(const CaptureSourceSelectionOptions& options,
    std::function<void(bool, std::shared_ptr<CaptureSource>)> callback) {
  if (!callback) return;
  if (!IsMainThreadDispatchSupported()) { callback(false, nullptr); return; }
  if (!IsSourceSelectionSupported() || (!options.allow_displays && !options.allow_windows) ||
      picker_busy.exchange(true)) {
    Deliver([callback] { callback(false, nullptr); }); return;
  }
  auto once = std::make_shared<std::atomic<bool>>(false);
  auto finish = [callback, once, options](bool ok, std::shared_ptr<detail::CaptureTarget> target) {
    if (once->exchange(true)) return;
    std::shared_ptr<CaptureSource> source;
    try {
      if (target) {
        auto type = target->Type();
        if ((type == CaptureSourceType::Window && options.allow_windows) ||
            (type == CaptureSourceType::Display && options.allow_displays) ||
            (type == CaptureSourceType::Unknown && options.allow_windows && options.allow_displays))
          source = detail::CaptureAccess::Wrap(std::move(target));
        else ok = false;
      }
    } catch (...) { ok = false; }
    Deliver([callback, ok, source] {
      picker_busy.store(false);
      callback(ok, ok ? source : nullptr);
    });
  };
  try {
    auto resolved = options;
    if (!resolved.parent_window) resolved.parent_window = WindowManager::GetInstance().GetCurrent();
    detail::SelectCaptureTarget(resolved, finish);
  } catch (...) { finish(false, nullptr); }
}

void ScreenCapturer::Capture(std::shared_ptr<CaptureSource> source,
    const ScreenCaptureOptions& options, std::function<void(bool, std::shared_ptr<Image>)> callback) {
  if (!callback) return;
  if (!IsMainThreadDispatchSupported()) { callback(false, nullptr); return; }
  auto lease = std::make_shared<detail::CaptureLease>(std::move(source));
  if (!lease->target || !IsSupported() || !detail::ValidCaptureOptions(options)) {
    lease.reset();
    Deliver([callback] { callback(false, nullptr); }); return;
  }
  try {
    std::thread([lease, callback, options]() mutable {
      std::shared_ptr<Image> image;
      try {
        std::atomic<bool> cancel{false};
        std::string error;
        auto stream = lease->target->Open(options, 30, cancel, error);
        if (stream) {
          auto deadline = detail::CaptureClock::now() + std::chrono::seconds(15);
          while (detail::CaptureClock::now() < deadline && error.empty()) {
            auto frame = stream->NextFrame(100, error);
            if (!frame) continue;
            detail::CaptureFrame cropped;
            if (detail::CropCaptureFrame(*frame, options, cropped))
              image = detail::CaptureFrameImage(cropped);
            break;
          }
          if (!stream->Stop(error) || !error.empty()) image.reset();
        }
      } catch (...) { image.reset(); }
      Deliver([lease = std::move(lease), callback, image]() mutable {
        lease.reset();
        callback(image != nullptr, image);
      });
    }).detach();
  } catch (...) {
    lease.reset();
    Deliver([callback] { callback(false, nullptr); });
  }
}
}  // namespace nativeapi
