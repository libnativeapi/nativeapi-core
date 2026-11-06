// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../menu.h"

namespace nativeapi {

class Menu::Impl {
 public:
  Impl() {}
};

Menu::Menu() : Menu(nullptr) {}
Menu::Menu(void* native_menu) : pimpl_(std::make_unique<Impl>()) {}
Menu::~Menu() {
  ShutdownEmitter();
}

void* Menu::GetNativeObjectInternal() const {
  return nullptr;
}

MenuId Menu::GetId() const {
  return IdAllocator::kInvalidId;
}

void Menu::AddItem(std::shared_ptr<MenuItem> item) {}

void Menu::InsertItem(size_t index, std::shared_ptr<MenuItem> item) {}

bool Menu::RemoveItem(std::shared_ptr<MenuItem> item) {
  return false;
}

bool Menu::RemoveItemById(MenuItemId item_id) {
  return false;
}

bool Menu::RemoveItemAt(size_t index) {
  return false;
}

void Menu::Clear() {}

void Menu::AddSeparator() {}

void Menu::InsertSeparator(size_t index) {}

size_t Menu::GetItemCount() const {
  return 0;
}

std::shared_ptr<MenuItem> Menu::GetItemAt(size_t index) const {
  return nullptr;
}

std::shared_ptr<MenuItem> Menu::GetItemById(MenuItemId item_id) const {
  return nullptr;
}

std::vector<std::shared_ptr<MenuItem>> Menu::GetAllItems() const {
  return {};
}

bool Menu::Open(const PositioningStrategy& strategy, Placement placement) {
  return false;
}

bool Menu::Close() {
  return false;
}

}  // namespace nativeapi
