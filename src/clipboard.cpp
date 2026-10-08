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
bool Clipboard::Clear() {
  return Write({});
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
