#include "../../clipboard_impl.h"

namespace nativeapi {
struct Clipboard::Impl::Platform {};
Clipboard::Impl::Impl(Clipboard* owner) : owner(owner), platform(std::make_unique<Platform>()) {}
Clipboard::Impl::~Impl() = default;
bool Clipboard::IsSupported() {
  return false;
}
bool Clipboard::IsChangeMonitoringSupported() {
  return false;
}
void Clipboard::Impl::Read(unsigned, std::function<void(bool, ClipboardData)> callback) {
  callback(false, {});
}
bool Clipboard::Impl::Write(const ClipboardData&) {
  return false;
}
bool Clipboard::Impl::Start() {
  return false;
}
void Clipboard::Impl::Stop() {
  monitoring = false;
}
}  // namespace nativeapi
