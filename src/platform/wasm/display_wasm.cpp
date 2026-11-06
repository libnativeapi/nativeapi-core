#include "../../display.h"
namespace nativeapi {
class Display::Impl {};
Display::Display(void*) : pimpl_(std::make_unique<Impl>()) {}
Display::~Display() = default;
void* Display::GetNativeObjectInternal() const {
  return nullptr;
}
DisplayId Display::GetId() const {
  return IdAllocator::kInvalidId;
}
std::string Display::GetName() const {
  return "";
}
Point Display::GetPosition() const {
  return {0, 0};
}
Size Display::GetSize() const {
  return {0, 0};
}
Rectangle Display::GetWorkArea() const {
  return {0, 0, 0, 0};
}
double Display::GetScaleFactor() const {
  return 1;
}
bool Display::IsPrimary() const {
  return false;
}
DisplayOrientation Display::GetOrientation() const {
  return DisplayOrientation::kLandscape;
}
int Display::GetRefreshRate() const {
  return 0;
}
int Display::GetBitDepth() const {
  return 0;
}
}  // namespace nativeapi
