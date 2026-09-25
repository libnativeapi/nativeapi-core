#pragma once

#include <vector>

#include "foundation/geometry.h"
#include "view.h"

namespace nativeapi {

/// What the layout needs to know about one subview.
struct LayoutChild {
  bool visible = true;
  /// Explicit size on each axis; 0 means "use intrinsic".
  Size preferred{0.0, 0.0};
  Size intrinsic{0.0, 0.0};
  double flex = 0.0;
  ViewAlignment alignment = ViewAlignment::Stretch;
};

/**
 * Computes one frame per child for a Row or Column of the given size.
 *
 * Hidden children get an empty frame and take no space. On the main axis a
 * child with flex > 0 receives its share of whatever is left after the fixed
 * children, spacing and padding; a fixed child gets its preferred size, or its
 * intrinsic size where the preferred one is 0. On the cross axis Stretch fills
 * the padded extent; the other alignments use the preferred / intrinsic size.
 * Nothing is ever clamped below 0.
 *
 * Absolute is not handled here: those children keep the frame they were given.
 */
std::vector<Rectangle> ComputeStackLayout(ViewLayout layout, Size container, EdgeInsets padding,
                                          double spacing, const std::vector<LayoutChild>& children);

/**
 * The size a Row or Column needs to give every visible child its preferred or
 * intrinsic size (flex children included) without stretching: the children end
 * to end plus spacing on the main axis, the largest child on the cross axis,
 * padding around both. Zero for Absolute, whose children place themselves.
 */
Size ComputeStackContentSize(ViewLayout layout, EdgeInsets padding, double spacing,
                             const std::vector<LayoutChild>& children);

}  // namespace nativeapi
