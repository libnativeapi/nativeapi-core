#include "clipboard_impl.h"

namespace nativeapi {

Clipboard& Clipboard::GetInstance() {
  static Clipboard instance;
  return instance;
}
Clipboard::Clipboard() : pimpl_(std::make_unique<Impl>(this)) {}
Clipboard::~Clipboard() {
  ShutdownEmitter();
  pimpl_->Stop();
}

void Clipboard::Read(std::function<void(bool, ClipboardData)> callback) {
  if (callback)
    pimpl_->Read(Impl::All, CreateGuardedCallback(std::move(callback)));
}
void Clipboard::ReadText(std::function<void(bool, std::optional<std::string>)> callback) {
  if (callback)
    pimpl_->Read(Impl::Bit(ClipboardReadFormat::Text),
                 CreateGuardedCallback(std::function<void(bool, ClipboardData)>(
                     [callback = std::move(callback)](bool ok, ClipboardData data) {
                       callback(ok, std::move(data.text));
                     })));
}
void Clipboard::ReadHtml(std::function<void(bool, std::optional<std::string>)> callback) {
  if (callback)
    pimpl_->Read(Impl::Bit(ClipboardReadFormat::Html),
                 CreateGuardedCallback(std::function<void(bool, ClipboardData)>(
                     [callback = std::move(callback)](bool ok, ClipboardData data) {
                       callback(ok, std::move(data.html));
                     })));
}
void Clipboard::ReadRtf(std::function<void(bool, std::optional<std::string>)> callback) {
  if (callback)
    pimpl_->Read(Impl::Bit(ClipboardReadFormat::Rtf),
                 CreateGuardedCallback(std::function<void(bool, ClipboardData)>(
                     [callback = std::move(callback)](bool ok, ClipboardData data) {
                       callback(ok, std::move(data.rtf));
                     })));
}
void Clipboard::ReadImage(std::function<void(bool, std::shared_ptr<Image>)> callback) {
  if (callback)
    pimpl_->Read(Impl::Bit(ClipboardReadFormat::Image),
                 CreateGuardedCallback(std::function<void(bool, ClipboardData)>(
                     [callback = std::move(callback)](bool ok, ClipboardData data) {
                       callback(ok, std::move(data.image));
                     })));
}
void Clipboard::ReadFilePaths(std::function<void(bool, std::vector<std::string>)> callback) {
  if (callback)
    pimpl_->Read(Impl::Bit(ClipboardReadFormat::FilePaths),
                 CreateGuardedCallback(std::function<void(bool, ClipboardData)>(
                     [callback = std::move(callback)](bool ok, ClipboardData data) {
                       callback(ok, std::move(data.file_paths));
                     })));
}
void Clipboard::ReadFormatBase64(const std::string& format,
                                 std::function<void(bool, std::optional<std::string>)> callback) {
  if (!callback)
    return;
  if (format.empty() || !clipboard_internal::ValidString(format)) {
    auto guarded = CreateGuardedCallback(std::move(callback));
    RunOnMainThread([guarded = std::move(guarded)] { guarded(false, std::nullopt); });
    return;
  }
  pimpl_->ReadCustomFormatBase64(format, CreateGuardedCallback(std::move(callback)));
}

bool Clipboard::Write(const ClipboardData& data) {
  try {
    return IsSupported() && clipboard_internal::ValidData(data) && pimpl_->Write(data);
  } catch (...) {
    return false;
  }
}
bool Clipboard::WriteText(const std::string& text) {
  ClipboardData data;
  data.text = text;
  return Write(data);
}
bool Clipboard::WriteHtml(const std::string& html) {
  ClipboardData data;
  data.html = html;
  return Write(data);
}
bool Clipboard::WriteRtf(const std::string& rtf) {
  ClipboardData data;
  data.rtf = rtf;
  return Write(data);
}
bool Clipboard::WriteImage(std::shared_ptr<Image> image) {
  ClipboardData data;
  data.image = std::move(image);
  return Write(data);
}
bool Clipboard::WriteFilePaths(const std::vector<std::string>& paths) {
  ClipboardData data;
  data.file_paths = paths;
  return Write(data);
}
bool Clipboard::WriteFormatBase64(const std::string& format, const std::string& base64_data) {
  try {
    if (!IsSupported() || format.empty() || !clipboard_internal::ValidString(format))
      return false;
    return pimpl_->WriteCustomFormatBase64(format, base64_data);
  } catch (...) {
    return false;
  }
}
bool Clipboard::Clear() {
  return Write({});
}
int64_t Clipboard::GetChangeCount() const {
  return IsSupported() ? pimpl_->GetChangeCount() : 0;
}
bool Clipboard::IsConcealed() const {
  return IsSupported() && pimpl_->IsConcealed();
}
bool Clipboard::HasText() const {
  return IsSupported() && pimpl_->HasFormat(ClipboardReadFormat::Text);
}
bool Clipboard::HasHtml() const {
  return IsSupported() && pimpl_->HasFormat(ClipboardReadFormat::Html);
}
bool Clipboard::HasRtf() const {
  return IsSupported() && pimpl_->HasFormat(ClipboardReadFormat::Rtf);
}
bool Clipboard::HasImage() const {
  return IsSupported() && pimpl_->HasFormat(ClipboardReadFormat::Image);
}
bool Clipboard::HasFilePaths() const {
  return IsSupported() && pimpl_->HasFormat(ClipboardReadFormat::FilePaths);
}
bool Clipboard::HasFormat(const std::string& format) const {
  return IsSupported() && !format.empty() && clipboard_internal::ValidString(format) &&
         pimpl_->HasCustomFormat(format);
}
bool Clipboard::IsMonitoring() const {
  return pimpl_->monitoring;
}
void Clipboard::StartEventListening() {
  if (!pimpl_->monitoring)
    pimpl_->monitoring = pimpl_->Start();
}
void Clipboard::StopEventListening() {
  pimpl_->Stop();
  pimpl_->monitoring = false;
}

}  // namespace nativeapi
