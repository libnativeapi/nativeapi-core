#include "capture_source.h"

#include <atomic>
#include "screen_capture_backend.h"

namespace nativeapi {
class CaptureSource::Impl {
 public:
  std::shared_ptr<detail::CaptureTarget> target;
  std::atomic<bool> busy{false};
};

CaptureSource::CaptureSource() : pimpl_(std::make_unique<Impl>()) {}
CaptureSource::~CaptureSource() = default;
std::shared_ptr<CaptureSource> CaptureSource::FromDisplay(std::shared_ptr<Display> display) {
  try { return detail::CaptureAccess::Wrap(detail::CaptureDisplayTarget(std::move(display))); }
  catch (...) { return nullptr; }
}
std::shared_ptr<CaptureSource> CaptureSource::FromWindow(std::shared_ptr<Window> window) {
  try { return detail::CaptureAccess::Wrap(detail::CaptureWindowTarget(std::move(window))); }
  catch (...) { return nullptr; }
}
CaptureSourceType CaptureSource::GetType() const {
  try { return pimpl_->target ? pimpl_->target->Type() : CaptureSourceType::None; }
  catch (...) { return CaptureSourceType::None; }
}
std::string CaptureSource::GetName() const {
  try { return pimpl_->target ? pimpl_->target->Name() : std::string(); }
  catch (...) { return {}; }
}

namespace detail {
std::shared_ptr<CaptureSource> CaptureAccess::Wrap(std::shared_ptr<CaptureTarget> target) {
  if (!target) return nullptr;
  auto result = std::shared_ptr<CaptureSource>(new CaptureSource());
  result->pimpl_->target = std::move(target);
  return result;
}
std::shared_ptr<CaptureTarget> CaptureAccess::Acquire(const std::shared_ptr<CaptureSource>& source) {
  if (!source || !source->pimpl_->target || source->pimpl_->busy.exchange(true)) return nullptr;
  return source->pimpl_->target;
}
void CaptureAccess::Release(const std::shared_ptr<CaptureSource>& source) {
  if (source) source->pimpl_->busy.store(false);
}
CaptureLease::CaptureLease(std::shared_ptr<CaptureSource> source)
    : target(CaptureAccess::Acquire(source)), source_(target ? std::move(source) : nullptr) {}
CaptureLease::~CaptureLease() { CaptureAccess::Release(source_); }
}  // namespace detail
}  // namespace nativeapi
