// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../drop_target_impl.h"

namespace nativeapi {

struct DropTarget::Impl::Platform {};

bool DropTarget::IsSupported() {
  return false;
}

DropTarget::Impl::Impl(DropTarget* owner, std::shared_ptr<Window> window)
    : owner(owner),
      window(std::move(window)),
      window_id(this->window ? this->window->GetId() : 0),
      platform(std::make_unique<Platform>()) {}

DropTarget::Impl::~Impl() = default;

bool DropTarget::Impl::Register() {
  return false;
}

void DropTarget::Impl::Unregister() {}

}  // namespace nativeapi
