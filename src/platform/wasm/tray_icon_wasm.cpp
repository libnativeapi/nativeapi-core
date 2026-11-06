// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../tray_icon.h"

namespace nativeapi {

class TrayIcon::Impl {
 public:
  Impl() {}
};

TrayIcon::TrayIcon() : TrayIcon(static_cast<void*>(nullptr)) {}
TrayIcon::TrayIcon(const std::string&) : TrayIcon() {}
TrayIcon::TrayIcon(void* tray) : pimpl_(std::make_unique<Impl>()) {}
TrayIcon::~TrayIcon() {
  ShutdownEmitter();
}

void* TrayIcon::GetNativeObjectInternal() const {
  return nullptr;
}

TrayIconId TrayIcon::GetId() {
  return IdAllocator::kInvalidId;
}

std::string TrayIcon::GetIdentifier() const {
  return "";
}

void TrayIcon::SetIcon(std::shared_ptr<Image> image) {}

std::shared_ptr<Image> TrayIcon::GetIcon() const {
  return nullptr;
}

void TrayIcon::SetIconTemplate(bool is_icon_template) {}

bool TrayIcon::IsIconTemplate() const {
  return false;
}

void TrayIcon::SetIconSize(Size size) {}

Size TrayIcon::GetIconSize() const {
  return Size{0, 0};
}

void TrayIcon::SetIconPosition(TrayIconPosition position) {}

TrayIconPosition TrayIcon::GetIconPosition() const {
  return TrayIconPosition::Left;
}

void TrayIcon::SetContentView(std::shared_ptr<View> view) {}

std::shared_ptr<View> TrayIcon::GetContentView() const {
  return nullptr;
}

void TrayIcon::SetTitle(std::optional<std::string> title) {}

std::optional<std::string> TrayIcon::GetTitle() {
  return std::nullopt;
}

void TrayIcon::SetTooltip(std::optional<std::string> tooltip) {}

std::optional<std::string> TrayIcon::GetTooltip() {
  return std::nullopt;
}

void TrayIcon::SetContextMenu(std::shared_ptr<Menu> menu) {}

std::shared_ptr<Menu> TrayIcon::GetContextMenu() {
  return nullptr;
}

void TrayIcon::SetContextMenuTrigger(ContextMenuTrigger trigger) {}

ContextMenuTrigger TrayIcon::GetContextMenuTrigger() {
  return ContextMenuTrigger::None;
}

Rectangle TrayIcon::GetBounds() {
  return Rectangle{0.0, 0.0, 0.0, 0.0};
}

bool TrayIcon::SetVisible(bool visible) {
  return false;
}

bool TrayIcon::IsVisible() {
  return false;
}

bool TrayIcon::OpenContextMenu() {
  return false;
}

void TrayIcon::StartEventListening() {}

void TrayIcon::StopEventListening() {}

bool TrayIcon::CloseContextMenu() {
  return false;
}

}  // namespace nativeapi
