#include "window_shape.h"

#include <cmath>

namespace nativeapi {

bool WindowShape::AddPoint(Point point) {
  if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 ||
      point.x > 16384 || point.y > 16384 || points_.size() >= 4096) {
    return false;
  }
  points_.push_back(point);
  return true;
}

void WindowShape::Clear() {
  points_.clear();
}
size_t WindowShape::GetPointCount() const {
  return points_.size();
}
Point WindowShape::GetPointAt(size_t index) const {
  return index < points_.size() ? points_[index] : Point{0, 0};
}

}  // namespace nativeapi
