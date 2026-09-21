#pragma once

#include <vector>

namespace nativeapi::window_shadow {
// Three separable box passes approximate a Gaussian in linear time, independent
// of polygon vertex count. Black premultiplied pixels need only an alpha channel.
inline void Blur(std::vector<unsigned char>& pixels, int width, int height, int radius) {
  if (radius <= 0 || width <= 0 || height <= 0) return;
  std::vector<unsigned char> temp(pixels.size());
  std::vector<int> sums(width);
  auto* data = pixels.data();
  auto* scratch = temp.data();
  const int diameter = radius * 2 + 1;
  for (int pass = 0; pass < 3; ++pass) {
    for (int y = 0; y < height; ++y) {
      const auto* row = data + y * width;
      auto* out = scratch + y * width;
      int sum = 0;
      for (int x = 0; x < width + radius; ++x) {
        if (x < width) sum += row[x];
        if (x >= diameter) sum -= row[x - diameter];
        if (x >= radius) out[x - radius] = sum / diameter;
      }
    }
    // Traverse rows in both passes. Walking a whole column per pixel stream
    // thrashes the cache on Retina-sized shadows; per-column sums preserve the
    // exact same zero padding and integer rounding without that strided access.
    auto* sum = sums.data();
    for (int x = 0; x < width; ++x) sum[x] = 0;
    for (int y = 0; y < height + radius; ++y) {
      if (y < height) {
        const auto* row = scratch + y * width;
        for (int x = 0; x < width; ++x) sum[x] += row[x];
      }
      if (y >= diameter) {
        const auto* row = scratch + (y - diameter) * width;
        for (int x = 0; x < width; ++x) sum[x] -= row[x];
      }
      if (y >= radius) {
        auto* out = data + (y - radius) * width;
        for (int x = 0; x < width; ++x) out[x] = sum[x] / diameter;
      }
    }
  }
}

}  // namespace nativeapi::window_shadow

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../window_shadow.h"
#include "../window_shape.h"

namespace nativeapi::window_shadow {
inline int Radius(const WindowShadow& options, double scale = 1) {
  return static_cast<int>(std::ceil(options.GetBlurRadius() * scale / 3));
}
inline int Margin(const WindowShadow& options, double scale = 1) {
  auto offset = options.GetOffset();
  return Radius(options, scale) * 3 +
         static_cast<int>(std::ceil((std::max)(std::abs(offset.x), std::abs(offset.y)) * scale)) +
         2;
}
inline uint32_t Pixel(unsigned char mask, Color color) {
  const unsigned int a = (static_cast<unsigned int>(mask) * color.a + 127) / 255;
  return (a << 24) | ((color.r * a / 255) << 16) | ((color.g * a / 255) << 8) | (color.b * a / 255);
}
inline std::vector<uint32_t> Render(int content_width,
                                    int content_height,
                                    const WindowShape& polygon,
                                    const WindowShadow& options,
                                    double scale = 1) {
  const int margin = Margin(options, scale);
  const int width = content_width + margin * 2, height = content_height + margin * 2;
  if (width <= 0 || height <= 0 || int64_t(width) * height > 16000000)
    return {};
  std::vector<unsigned char> alpha(static_cast<size_t>(width) * height);
  std::vector<Point> points;
  for (size_t i = 0; i < polygon.GetPointCount(); ++i)
    points.push_back(polygon.GetPointAt(i));
  if (points.empty())
    points = {{0, 0},
              {content_width / scale, 0},
              {content_width / scale, content_height / scale},
              {0, content_height / scale}};
  const auto offset = options.GetOffset();
  for (auto& p : points) {
    p.x = p.x * scale + margin + offset.x * scale;
    p.y = p.y * scale + margin + offset.y * scale;
  }
  std::vector<double> crossings;
  for (int y = 0; y < height; ++y) {
    crossings.clear();
    const double scan = y + .5;
    for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
      const auto a = points[i], b = points[j];
      if ((a.y <= scan && b.y > scan) || (b.y <= scan && a.y > scan))
        crossings.push_back(a.x + (scan - a.y) * (b.x - a.x) / (b.y - a.y));
    }
    std::sort(crossings.begin(), crossings.end());
    for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
      int left = (std::max)(0, int(std::ceil(crossings[i] - .5)));
      int right = (std::min)(width, int(std::ceil(crossings[i + 1] - .5)));
      for (int x = left; x < right; ++x)
        alpha[y * width + x] = 255;
    }
  }
  if (Radius(options, scale) > 0)
    Blur(alpha, width, height, Radius(options, scale));
  std::vector<uint32_t> result(alpha.size());
  for (size_t i = 0; i < alpha.size(); ++i)
    result[i] = Pixel(alpha[i], options.GetColor());
  return result;
}
}  // namespace nativeapi::window_shadow
