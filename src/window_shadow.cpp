#include "window_shadow.h"
#include <cmath>
namespace nativeapi {
void WindowShadow::SetColor(const Color& color) {
  color_ = color;
}
Color WindowShadow::GetColor() const {
  return color_;
}
bool WindowShadow::SetBlurRadius(double radius) {
  if (!std::isfinite(radius) || radius < 0 || radius > 64)
    return false;
  blur_radius_ = radius;
  return true;
}
double WindowShadow::GetBlurRadius() const {
  return blur_radius_;
}
bool WindowShadow::SetOffset(Point offset) {
  if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || std::abs(offset.x) > 64 ||
      std::abs(offset.y) > 64)
    return false;
  offset_ = offset;
  return true;
}
Point WindowShadow::GetOffset() const {
  return offset_;
}
}  // namespace nativeapi
