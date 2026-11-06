#include "../../image.h"
namespace nativeapi {
class Image::Impl {};
Image::Image() : pimpl_(std::make_unique<Impl>()) {}
Image::~Image() = default;
Image::Image(const Image&) : Image() {}
Image::Image(Image&& other) noexcept : pimpl_(std::move(other.pimpl_)) {}
std::shared_ptr<Image> Image::FromFile(const std::string&) {
  return nullptr;
}
std::shared_ptr<Image> Image::FromBase64(const std::string&) {
  return nullptr;
}
Size Image::GetSize() const {
  return {0, 0};
}
std::string Image::GetFormat() const {
  return "";
}
std::string Image::ToBase64() const {
  return "";
}
bool Image::SaveToFile(const std::string&) const {
  return false;
}
void* Image::GetNativeObjectInternal() const {
  return nullptr;
}
}  // namespace nativeapi
