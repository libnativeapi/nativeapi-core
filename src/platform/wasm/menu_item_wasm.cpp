// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../menu.h"

namespace nativeapi {

class MenuItem::Impl {
 public:
  Impl() {}
};

MenuItem::MenuItem(const std::string& label, MenuItemType type) : MenuItem(nullptr) {}

MenuItem::MenuItem(void* native_item) : pimpl_(std::make_unique<Impl>()) {}

MenuItem::~MenuItem() {
  ShutdownEmitter();
}

void* MenuItem::GetNativeObjectInternal() const {
  return nullptr;
}

MenuItemId MenuItem::GetId() const {
  return IdAllocator::kInvalidId;
}

MenuItemType MenuItem::GetType() const {
  return MenuItemType::Normal;
}

void MenuItem::SetLabel(const std::optional<std::string>& label) {}

std::optional<std::string> MenuItem::GetLabel() const {
  return std::nullopt;
}

void MenuItem::SetIcon(std::shared_ptr<Image> image) {}

std::shared_ptr<Image> MenuItem::GetIcon() const {
  return nullptr;
}

void MenuItem::SetTooltip(const std::optional<std::string>& tooltip) {}

std::optional<std::string> MenuItem::GetTooltip() const {
  return std::nullopt;
}

void MenuItem::SetAccelerator(const std::optional<KeyboardAccelerator>& accelerator) {}

KeyboardAccelerator MenuItem::GetAccelerator() const {
  return KeyboardAccelerator("");
}

void MenuItem::SetEnabled(bool enabled) {}

bool MenuItem::IsEnabled() const {
  return false;
}

void MenuItem::SetState(MenuItemState state) {}

MenuItemState MenuItem::GetState() const {
  return MenuItemState::Unchecked;
}

void MenuItem::SetRadioGroup(int group_id) {}

int MenuItem::GetRadioGroup() const {
  return -1;
}

void MenuItem::SetSubmenu(std::shared_ptr<Menu> submenu) {}

std::shared_ptr<Menu> MenuItem::GetSubmenu() const {
  return nullptr;
}

}  // namespace nativeapi
