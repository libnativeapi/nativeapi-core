#pragma once

namespace nativeapi {

/**
 * Point is a 2D point in the coordinate system.
 */
struct Point {
  double x;
  double y;
};

/**
 * Size is a 2D size in the coordinate system.
 */
struct Size {
  double width;
  double height;
};

/**
 * Rectangle is a 2D rectangle in the coordinate system.
 */
struct Rectangle {
  double x;
  double y;
  double width;
  double height;
};

/**
 * EdgeInsets are distances from the four edges of a rectangle, in logical
 * points: the space a View keeps between its edges and its content.
 */
struct EdgeInsets {
  double top;
  double right;
  double bottom;
  double left;

  /** The same inset on every edge. */
  static EdgeInsets All(double value) { return EdgeInsets{value, value, value, value}; }

  /** One inset for top and bottom, another for left and right. */
  static EdgeInsets Symmetric(double vertical, double horizontal) {
    return EdgeInsets{vertical, horizontal, vertical, horizontal};
  }
};

}  // namespace nativeapi