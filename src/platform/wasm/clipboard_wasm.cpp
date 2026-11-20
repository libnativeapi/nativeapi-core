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
  if (callback)
    callback(false, {});
}
bool Clipboard::Impl::Write(const ClipboardData&) {
  return false;
}
void Clipboard::Impl::ReadCustomFormatBase64(
    const std::string&,
    std::function<void(bool, std::optional<std::string>)> callback) {
  if (callback)
    callback(false, std::nullopt);
}
bool Clipboard::Impl::WriteCustomFormatBase64(const std::string&, const std::string&) {
  return false;
}
int64_t Clipboard::Impl::GetChangeCount() const {
  return 0;
}
bool Clipboard::Impl::IsConcealed() const {
  return false;
}
bool Clipboard::Impl::HasFormat(ClipboardReadFormat) const {
  return false;
}
bool Clipboard::Impl::HasCustomFormat(const std::string&) const {
  return false;
}
bool Clipboard::Impl::Start() {
  return false;
}
void Clipboard::Impl::Stop() {}
}  // namespace nativeapi
