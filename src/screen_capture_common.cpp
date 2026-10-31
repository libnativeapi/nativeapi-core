#include "screen_capture_backend.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <random>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace nativeapi::detail {
namespace {
constexpr int kMaxDimension = 32768;
bool FrameValid(const CaptureFrame& frame) {
  return frame.width > 0 && frame.height > 0 && frame.width <= kMaxDimension &&
      frame.height <= kMaxDimension &&
      frame.pixels.size() == size_t(frame.width) * size_t(frame.height) * 4;
}
bool ValidUtf8(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    uint32_t c = static_cast<uint8_t>(s[i++]);
    if (!c) return false;
    if (c < 0x80) continue;
    int count;
    uint32_t minimum;
    if (c >= 0xc2 && c <= 0xdf) { count = 1; c &= 0x1f; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { count = 2; c &= 0x0f; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { count = 3; c &= 7; minimum = 0x10000; }
    else return false;
    while (count--) {
      if (i == s.size()) return false;
      uint8_t next = static_cast<uint8_t>(s[i++]);
      if ((next & 0xc0) != 0x80) return false;
      c = (c << 6) | (next & 0x3f);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
  }
  return true;
}
}  // namespace

bool ValidCaptureOptions(const ScreenCaptureOptions& options) {
  if (!options.crop_enabled) return true;
  auto r = options.crop;
  for (double n : {r.x, r.y, r.width, r.height})
    if (!std::isfinite(n) || std::floor(n) != n || n < 0 || n > kMaxDimension) return false;
  return r.width > 0 && r.height > 0;
}
bool ValidRecordingOptions(const ScreenRecordingOptions& options) {
  return ValidCaptureOptions(options.capture) && std::isfinite(options.frame_rate) &&
      options.frame_rate >= 0.1 && options.frame_rate <= 240.0 &&
      options.video_bitrate <= std::numeric_limits<uint32_t>::max();
}
bool ValidRecordingPath(const std::string& path) {
  if (path.empty() || !ValidUtf8(path)) return false;
  try {
    auto p = std::filesystem::u8path(path);
    if (!p.is_absolute()) return false;
    auto extension = p.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return extension == ".mp4";
  } catch (...) { return false; }
}
bool CropCaptureFrame(const CaptureFrame& input, const ScreenCaptureOptions& options,
                      CaptureFrame& output) {
  if (!FrameValid(input) || !ValidCaptureOptions(options)) return false;
  Rectangle r = options.crop_enabled ? options.crop :
      Rectangle{0, 0, double(input.width), double(input.height)};
  if (r.x + r.width > input.width || r.y + r.height > input.height) return false;
  output.width = int(r.width);
  output.height = int(r.height);
  output.time = input.time;
  output.pixels.resize(size_t(output.width) * output.height * 4);
  for (int y = 0; y < output.height; ++y)
    std::memcpy(output.pixels.data() + size_t(y) * output.width * 4,
        input.pixels.data() + (size_t(y + int(r.y)) * input.width + int(r.x)) * 4,
        size_t(output.width) * 4);
  return true;
}
bool FitCaptureFrame(const CaptureFrame& input, int width, int height, CaptureFrame& output) {
  if (!FrameValid(input) || width <= 0 || height <= 0 ||
      width > kMaxDimension || height > kMaxDimension) return false;
  output.width = width;
  output.height = height;
  output.time = input.time;
  output.pixels.assign(size_t(width) * height * 4, 0);
  for (size_t i = 3; i < output.pixels.size(); i += 4) output.pixels[i] = 255;
  // First-frame odd dimensions are padded rather than rescaled.
  double scale = (width - input.width <= 1 && width >= input.width &&
                  height - input.height <= 1 && height >= input.height) ? 1.0 :
      std::min(double(width) / input.width, double(height) / input.height);
  int w = std::max(1, int(std::floor(input.width * scale)));
  int h = std::max(1, int(std::floor(input.height * scale)));
  int left = (width - w) / 2, top = (height - h) / 2;
  for (int y = 0; y < h; ++y) {
    double sy = std::clamp((y + 0.5) / scale - 0.5, 0.0, double(input.height - 1));
    int y0 = int(sy), y1 = std::min(y0 + 1, input.height - 1);
    for (int x = 0; x < w; ++x) {
      double sx = std::clamp((x + 0.5) / scale - 0.5, 0.0, double(input.width - 1));
      int x0 = int(sx), x1 = std::min(x0 + 1, input.width - 1);
      for (int c = 0; c < 3; ++c) {
        auto pixel = [&](int px, int py) { return input.pixels[(size_t(py) * input.width + px) * 4 + c]; };
        double a = pixel(x0, y0) * (1 - sx + x0) + pixel(x1, y0) * (sx - x0);
        double b = pixel(x0, y1) * (1 - sx + x0) + pixel(x1, y1) * (sx - x0);
        output.pixels[(size_t(y + top) * width + x + left) * 4 + c] =
            uint8_t(std::lround(a * (1 - sy + y0) + b * (sy - y0)));
      }
    }
  }
  return true;
}

CaptureOutputFile::~CaptureOutputFile() { std::string ignored; Discard(ignored); }
bool CaptureOutputFile::Create(const std::string& destination, std::string& error) {
  if (!temporary_.empty() || !ValidRecordingPath(destination)) {
    error = "Invalid recording output path"; return false;
  }
  try {
    auto path = std::filesystem::u8path(destination);
    std::error_code ec;
    auto status = std::filesystem::symlink_status(path, ec);
    if (status.type() != std::filesystem::file_type::not_found ||
        !std::filesystem::is_directory(path.parent_path())) {
      error = "Output already exists or parent directory is unavailable"; return false;
    }
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
      auto directory = path.parent_path() /
          (".nativeapi-recording-" + std::to_string(random()) + "-" + std::to_string(random()));
      if (std::filesystem::create_directory(directory, ec)) {
        directory_ = directory.u8string();
        temporary_ = (directory / "video.mp4").u8string();
        destination_ = destination;
        return true;
      }
      if (ec && ec != std::errc::file_exists) break;
    }
    error = "Cannot reserve a temporary recording directory";
  } catch (...) { error = "Cannot prepare the recording output"; }
  return false;
}
bool CaptureOutputFile::Publish(std::string& error) {
  if (temporary_.empty()) { error = "Missing temporary recording"; return false; }
  try {
#ifdef _WIN32
    bool ok = MoveFileExW(std::filesystem::u8path(temporary_).c_str(),
        std::filesystem::u8path(destination_).c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    // link is atomic and fails with EEXIST, unlike an overwriting rename.
    bool ok = link(temporary_.c_str(), destination_.c_str()) == 0;
#endif
    if (!ok) { error = "Cannot publish recording without replacing an existing file"; return false; }
    // Publication has committed. Never remove destination_, including on cleanup failure.
    return Discard(error);
  } catch (...) { error = "Recording publication failed"; return false; }
}
bool CaptureOutputFile::Discard(std::string& error) {
  if (directory_.empty()) return true;
  std::error_code ec;
  if (!temporary_.empty()) std::filesystem::remove(std::filesystem::u8path(temporary_), ec);
  if (!ec) std::filesystem::remove(std::filesystem::u8path(directory_), ec);
  if (ec) { error = "Failed to remove temporary recording: " + directory_; return false; }
  temporary_.clear();
  directory_.clear();
  return true;
}
}  // namespace nativeapi::detail
