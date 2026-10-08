#pragma once

// Internal platform seam; not part of the exported API.
#include "clipboard.h"
#include "foundation/dispatcher.h"
#include "image.h"

#include <cstdint>
#include <filesystem>
#include <utility>

namespace nativeapi {

enum class ClipboardReadFormat { None, Text, Html, Image, FilePaths };

class Clipboard::Impl {
 public:
  explicit Impl(Clipboard* owner);
  ~Impl();
  void Read(unsigned mask, std::function<void(bool, ClipboardData)> callback);
  bool Write(const ClipboardData& data);
  bool Start();
  void Stop();
  void Changed() { owner->EmitAsync<ClipboardChangedEvent>(); }

  static constexpr unsigned Bit(ClipboardReadFormat format) {
    return 1u << static_cast<unsigned>(format);
  }
  static constexpr unsigned All = 30;
  Clipboard* owner;
  bool monitoring = false;
  struct Platform;
  std::unique_ptr<Platform> platform;
};

namespace clipboard_internal {

// Validate Unicode scalar values, rejecting overlong encodings and embedded NUL.
inline bool ValidString(const std::string& text) {
  size_t i = 0;
  while (i < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[i++]);
    if (c == 0)
      return false;
    if (c < 0x80)
      continue;
    uint32_t value;
    unsigned count;
    uint32_t minimum;
    if (c >= 0xc2 && c <= 0xdf) {
      value = c & 31;
      count = 1;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      value = c & 15;
      count = 2;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      value = c & 7;
      count = 3;
      minimum = 0x10000;
    } else
      return false;
    if (text.size() - i < count)
      return false;
    while (count--) {
      c = static_cast<unsigned char>(text[i++]);
      if ((c & 0xc0) != 0x80)
        return false;
      value = (value << 6) | (c & 63);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
      return false;
  }
  return true;
}

inline bool ValidData(const ClipboardData& data) {
  if ((data.text && !ValidString(*data.text)) || (data.html && !ValidString(*data.html)))
    return false;
  for (const auto& path : data.file_paths) {
    if (!ValidString(path) || !std::filesystem::u8path(path).is_absolute())
      return false;
  }
  if (data.image) {
    const auto size = data.image->GetSize();
    if (size.width <= 0 || size.height <= 0 || !data.image->GetNativeObject())
      return false;
  }
  return true;
}

inline std::string Base64(const unsigned char* bytes, size_t size) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve((size + 2) / 3 * 4);
  for (size_t i = 0; i < size; i += 3) {
    uint32_t value = static_cast<uint32_t>(bytes[i]) << 16;
    if (i + 1 < size)
      value |= static_cast<uint32_t>(bytes[i + 1]) << 8;
    if (i + 2 < size)
      value |= bytes[i + 2];
    result += alphabet[(value >> 18) & 63];
    result += alphabet[(value >> 12) & 63];
    result += i + 1 < size ? alphabet[(value >> 6) & 63] : '=';
    result += i + 2 < size ? alphabet[value & 63] : '=';
  }
  return result;
}

inline std::vector<unsigned char> DecodeBase64(const std::string& input) {
  const size_t comma = input.find(',');
  const size_t start = comma == std::string::npos ? 0 : comma + 1;
  std::vector<unsigned char> result;
  uint32_t value = 0;
  unsigned bits = 0;
  for (size_t i = start; i < input.size(); ++i) {
    const char c = input[i];
    if (c == '=')
      break;
    unsigned digit;
    if (c >= 'A' && c <= 'Z')
      digit = c - 'A';
    else if (c >= 'a' && c <= 'z')
      digit = c - 'a' + 26;
    else if (c >= '0' && c <= '9')
      digit = c - '0' + 52;
    else if (c == '+')
      digit = 62;
    else if (c == '/')
      digit = 63;
    else
      return {};
    value = (value << 6) | digit;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result.push_back(static_cast<unsigned char>(value >> bits));
    }
  }
  return result;
}

}  // namespace clipboard_internal
}  // namespace nativeapi
