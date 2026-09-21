#pragma once

#include <cstddef>
#include <vector>
#include "foundation/geometry.h"

namespace nativeapi {

/**
 * @brief A reusable polygon describing a window's visible region.
 *
 * Points use logical pixels from the content's top-left corner. The last point
 * is joined to the first automatically; intersections use the even-odd rule.
 * Approximate curves with short line segments. This is a copyable value object,
 * carried by a handle across the C ABI, like PositioningStrategy.
 *
 * @code
 * auto shape = std::make_shared<WindowShape>();
 * shape->AddPoint({0, 0});
 * shape->AddPoint({200, 0});
 * shape->AddPoint({100, 200});
 * window->SetShape(shape);
 * @endcode
 */
class WindowShape {
 public:
  WindowShape() = default;

  /**
   * @brief Adds a vertex in logical pixels.
   * @param point A finite coordinate in [0, 16384] on each axis.
   * @return False for invalid coordinates or more than 4096 vertices.
   */
  bool AddPoint(Point point);

  /** @brief Removes all vertices. An empty shape cannot be applied. */
  void Clear();

  /** @brief Gets the number of vertices. */
  size_t GetPointCount() const;

  /** @brief Gets a vertex; an out-of-range index returns the origin. */
  Point GetPointAt(size_t index) const;

 private:
  std::vector<Point> points_;
};

}  // namespace nativeapi
