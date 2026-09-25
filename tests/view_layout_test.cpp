#include "../src/view_layout.h"

#include <iostream>

using nativeapi::ComputeStackContentSize;
using nativeapi::ComputeStackLayout;
using nativeapi::EdgeInsets;
using nativeapi::LayoutChild;
using nativeapi::Size;
using nativeapi::ViewAlignment;
using nativeapi::ViewLayout;

namespace {

LayoutChild Child(double width, double height, double flex = 0.0) {
  LayoutChild child;
  child.intrinsic = Size{width, height};
  child.flex = flex;
  return child;
}

bool Equal(Size a, Size b) {
  return a.width == b.width && a.height == b.height;
}

}  // namespace

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::cerr << "FAIL: " << what << std::endl;
      ++failures;
    }
  };
  const EdgeInsets none{0, 0, 0, 0};
  const EdgeInsets pad{4, 6, 8, 10};  // top, right, bottom, left

  // A row: widths end to end plus gaps, the tallest height, padding around.
  std::vector<LayoutChild> row{Child(40, 20), Child(30, 24), Child(10, 10)};
  check(Equal(ComputeStackContentSize(ViewLayout::Row, pad, 5, row), Size{16 + 80 + 10, 12 + 24}),
        "row content size");

  // A column is the same with the axes swapped.
  check(Equal(ComputeStackContentSize(ViewLayout::Column, none, 2, row), Size{40, 54 + 4}),
        "column content size");

  // Preferred sizes win over intrinsic ones, per axis.
  std::vector<LayoutChild> preferred{Child(40, 20)};
  preferred[0].preferred = Size{0, 50};
  check(Equal(ComputeStackContentSize(ViewLayout::Row, none, 0, preferred), Size{40, 50}),
        "preferred height, intrinsic width");

  // Flex children count at their natural size; hidden ones not at all, and no
  // gap is left for them.
  std::vector<LayoutChild> mixed{Child(40, 20, 1.0), Child(99, 99), Child(30, 10)};
  mixed[1].visible = false;
  check(Equal(ComputeStackContentSize(ViewLayout::Row, none, 8, mixed), Size{78, 20}),
        "flex counted, hidden skipped");

  // Nothing visible: only the padding.
  check(Equal(ComputeStackContentSize(ViewLayout::Column, pad, 8, {}), Size{16, 12}),
        "empty container is its padding");

  // Absolute containers have no content size.
  check(Equal(ComputeStackContentSize(ViewLayout::Absolute, pad, 8, row), Size{0, 0}),
        "absolute is zero");

  // Laid out in exactly its content size, a row gives every child its natural
  // size: nothing is squeezed and nothing is left over.
  const Size content = ComputeStackContentSize(ViewLayout::Row, pad, 5, row);
  for (auto& child : row) {
    child.alignment = ViewAlignment::Start;
  }
  const auto frames = ComputeStackLayout(ViewLayout::Row, content, pad, 5, row);
  check(frames.size() == 3 && frames[0].x == 10 && frames[0].width == 40 && frames[1].x == 55 &&
            frames[2].x == 90 && frames[2].x + frames[2].width == 100,
        "content size fits the layout");

  if (failures == 0) {
    std::cout << "view_layout_test: all passed" << std::endl;
  }
  return failures == 0 ? 0 : 1;
}
