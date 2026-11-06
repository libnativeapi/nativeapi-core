// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../drag_source_impl.h"

namespace nativeapi {

bool DragSource::IsOperationSelectionSupported() {
  return false;
}

bool DragSource::IsPositionSupported() {
  return false;
}

struct DragSource::Impl::Platform {};

bool DragSource::IsSupported() {
  return false;
}

DragSource::Impl::Impl(DragSource* owner) : owner(owner), platform(std::make_unique<Platform>()) {}

DragSource::Impl::~Impl() = default;

bool DragSource::Impl::Start() {
  return false;
}

}  // namespace nativeapi
