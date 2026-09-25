#include "view_layout.h"

#include <algorithm>

namespace nativeapi {

namespace {

double NonNegative(double value) {
  return value < 0.0 ? 0.0 : value;
}

double Natural(double preferred, double intrinsic) {
  return preferred > 0.0 ? preferred : intrinsic;
}

}  // namespace

std::vector<Rectangle> ComputeStackLayout(ViewLayout layout, Size container, EdgeInsets padding,
                                          double spacing, const std::vector<LayoutChild>& children) {
  std::vector<Rectangle> frames(children.size(), Rectangle{0.0, 0.0, 0.0, 0.0});
  if (layout == ViewLayout::Absolute) {
    return frames;
  }
  const bool row = layout == ViewLayout::Row;

  // Work in main / cross terms and convert back at the end.
  const double main_extent = NonNegative(
      (row ? container.width - padding.left - padding.right
           : container.height - padding.top - padding.bottom));
  const double cross_extent = NonNegative(
      (row ? container.height - padding.top - padding.bottom
           : container.width - padding.left - padding.right));
  const double main_start = row ? padding.left : padding.top;
  const double cross_start = row ? padding.top : padding.left;

  auto main_of = [&](Size size) { return row ? size.width : size.height; };
  auto cross_of = [&](Size size) { return row ? size.height : size.width; };
  auto natural_main = [&](const LayoutChild& child) {
    return Natural(main_of(child.preferred), main_of(child.intrinsic));
  };
  auto natural_cross = [&](const LayoutChild& child) {
    return Natural(cross_of(child.preferred), cross_of(child.intrinsic));
  };

  size_t visible_count = 0;
  double fixed_total = 0.0;
  double flex_total = 0.0;
  for (const auto& child : children) {
    if (!child.visible) {
      continue;
    }
    ++visible_count;
    if (child.flex > 0.0) {
      flex_total += child.flex;
    } else {
      fixed_total += natural_main(child);
    }
  }
  const double gaps = visible_count > 1 ? spacing * static_cast<double>(visible_count - 1) : 0.0;
  const double leftover = NonNegative(main_extent - fixed_total - gaps);

  double cursor = main_start;
  bool first = true;
  for (size_t i = 0; i < children.size(); ++i) {
    const auto& child = children[i];
    if (!child.visible) {
      continue;
    }
    if (!first) {
      cursor += spacing;
    }
    first = false;

    const double main_size = child.flex > 0.0 ? leftover * (child.flex / flex_total)
                                              : natural_main(child);
    double cross_size = cross_extent;
    double cross_pos = cross_start;
    if (child.alignment != ViewAlignment::Stretch) {
      cross_size = std::min(natural_cross(child), cross_extent);
      switch (child.alignment) {
        case ViewAlignment::Center:
          cross_pos = cross_start + (cross_extent - cross_size) / 2.0;
          break;
        case ViewAlignment::End:
          cross_pos = cross_start + cross_extent - cross_size;
          break;
        case ViewAlignment::Start:
        case ViewAlignment::Stretch:
          break;
      }
    }

    frames[i] = row ? Rectangle{cursor, cross_pos, main_size, cross_size}
                    : Rectangle{cross_pos, cursor, cross_size, main_size};
    cursor += main_size;
  }
  return frames;
}

Size ComputeStackContentSize(ViewLayout layout, EdgeInsets padding, double spacing,
                             const std::vector<LayoutChild>& children) {
  if (layout == ViewLayout::Absolute) {
    return Size{0.0, 0.0};
  }
  const bool row = layout == ViewLayout::Row;
  double main_total = 0.0;
  double cross_max = 0.0;
  size_t visible_count = 0;
  for (const auto& child : children) {
    if (!child.visible) {
      continue;
    }
    ++visible_count;
    const double width = Natural(child.preferred.width, child.intrinsic.width);
    const double height = Natural(child.preferred.height, child.intrinsic.height);
    main_total += row ? width : height;
    cross_max = std::max(cross_max, row ? height : width);
  }
  if (visible_count > 1) {
    main_total += spacing * static_cast<double>(visible_count - 1);
  }
  const double horizontal = padding.left + padding.right;
  const double vertical = padding.top + padding.bottom;
  return row ? Size{main_total + horizontal, cross_max + vertical}
             : Size{cross_max + horizontal, main_total + vertical};
}

}  // namespace nativeapi
