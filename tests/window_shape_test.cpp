#include "../src/window_shape.h"
#include "../src/platform/window_shadow_blur.h"

#include <cmath>
#include <iostream>
#include <limits>

int main() {
  nativeapi::WindowShape shape;
  int failures = 0;
  auto check = [&](bool ok) {
    if (!ok)
      ++failures;
  };
  check(shape.GetPointCount() == 0);
  check(!shape.AddPoint({NAN, 1}));
  check(!shape.AddPoint({1, std::numeric_limits<double>::infinity()}));
  check(!shape.AddPoint({-1, 0}));
  check(!shape.AddPoint({16385, 0}));
  check(shape.GetPointCount() == 0);
  check(shape.AddPoint({12.5, 24.25}));
  auto copy = shape;
  shape.Clear();
  check(shape.GetPointCount() == 0 && copy.GetPointCount() == 1);
  check(copy.GetPointAt(0).x == 12.5 && copy.GetPointAt(0).y == 24.25);
  check(copy.GetPointAt(100).x == 0 && copy.GetPointAt(100).y == 0);
  for (size_t i = 1; i < 4096; ++i)
    check(copy.AddPoint({0, 16384}));
  check(!copy.AddPoint({0, 0}));
  check(copy.GetPointCount() == 4096);
  nativeapi::WindowShadow shadow;
  check(shadow.GetBlurRadius() == 18);
  check(!shadow.SetBlurRadius(NAN));
  check(!shadow.SetBlurRadius(-1));
  check(!shadow.SetBlurRadius(65));
  check(shadow.GetBlurRadius() == 18);
  check(shadow.SetBlurRadius(0));
  check(!shadow.SetOffset({INFINITY, 0}));
  check(!shadow.SetOffset({0, -65}));
  check(shadow.SetOffset({-8, 12}));
  shadow.SetColor({240, 100, 20, 128});
  auto snapshot = shadow;
  shadow.SetColor({0, 0, 0, 0});
  check(snapshot.GetColor().r == 240 && snapshot.GetColor().a == 128);
  // A compact square produces a symmetric soft exterior, with no wraparound
  // at row boundaries and no visible alpha beyond the reserved gutter.
  constexpr int width = 96;
  std::vector<unsigned char> alpha(width * width, 0);
  for (int y = 32; y < 64; ++y)
    for (int x = 32; x < 64; ++x) alpha[y * width + x] = 255;
  nativeapi::window_shadow::Blur(alpha, width, width, 6);
  check(alpha[48 * width + 31] > 0 && alpha[48 * width + 31] < 255);
  check(alpha[48 * width + 48] > alpha[48 * width + 31]);
  for (int i = 0; i < width; ++i) {
    check(alpha[i] == 0 && alpha[(width - 1) * width + i] == 0);
    check(alpha[i * width] == 0 && alpha[i * width + width - 1] == 0);
    check(alpha[48 * width + i] == alpha[48 * width + width - 1 - i]);
  }
  std::cout << "WindowShape failures: " << failures << '\n';
  return failures ? 1 : 0;
}
