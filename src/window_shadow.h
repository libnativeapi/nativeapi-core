#pragma once

#include "foundation/color.h"
#include "foundation/geometry.h"

namespace nativeapi {
/**
 * @brief Copyable custom window shadow configuration, carried by a C ABI handle.
 *
 * Distances are logical pixels. The blur radius is the finite support radius of
 * a three-pass box approximation to a Gaussian. Zero draws a hard shadow.
 * Defaults: black at 30% opacity, radius 18, offset (0, 6).
 * Applying or retrieving a configuration copies it; subsequent edits have no
 * effect until SetCustomShadow() is called again.
 */
class WindowShadow {
 public:
  WindowShadow() = default;
  /** @brief Sets the shadow color, including its opacity. */
  void SetColor(const Color& color);
  /** @brief Gets the shadow color. */
  Color GetColor() const;
  /** @brief Sets the blur radius; false for non-finite values or outside [0, 64]. */
  bool SetBlurRadius(double radius);
  /** @brief Gets the blur radius in logical pixels. */
  double GetBlurRadius() const;
  /** @brief Sets the offset; false for non-finite coordinates or outside [-64, 64]. */
  bool SetOffset(Point offset);
  /** @brief Gets the offset; positive x is right and positive y is down. */
  Point GetOffset() const;

 private:
  Color color_{0, 0, 0, 77};
  double blur_radius_ = 18;
  Point offset_{0, 6};
};
}  // namespace nativeapi
