#include "../src/platform/window_shadow_blur.h"
#include <iostream>
#include <random>

// Deliberately simple convolution oracle: checks byte-exact padding/rounding,
// including kernels wider than the image. This protects cross-platform parity.
static std::vector<unsigned char> Reference(std::vector<unsigned char> pixels,
                                            int width, int height, int radius) {
  for (int pass = 0; pass < 3; ++pass) {
    for (int axis = 0; axis < 2; ++axis) {
      auto previous = pixels;
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          int sum = 0;
          for (int offset = -radius; offset <= radius; ++offset) {
            const int xx = x + (axis == 0 ? offset : 0);
            const int yy = y + (axis == 1 ? offset : 0);
            if (xx >= 0 && xx < width && yy >= 0 && yy < height)
              sum += previous[yy * width + xx];
          }
          pixels[y * width + x] = sum / (2 * radius + 1);
        }
      }
    }
  }
  return pixels;
}

int main() {
  std::mt19937 random(1234);
  for (int width : {1, 2, 7, 32, 65}) {
    for (int height : {1, 3, 16, 37}) {
      for (int radius : {0, 1, 3, 12, 32}) {
        std::vector<unsigned char> pixels(width * height);
        for (auto& pixel : pixels) pixel = random() % 256;
        const auto expected = Reference(pixels, width, height, radius);
        nativeapi::window_shadow::Blur(pixels, width, height, radius);
        if (pixels != expected) {
          std::cerr << "Blur mismatch: " << width << 'x' << height << " radius " << radius << '\n';
          return 1;
        }
      }
    }
  }
  std::cout << "PASS 100 byte-exact blur comparisons\n";
}
