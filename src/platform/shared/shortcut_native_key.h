#pragma once

#include <charconv>
#include <cstdint>
#include <string>

namespace nativeapi::detail {

// Explicit platform keycodes preserve keys which have no portable accelerator
// name. Input is lowercased by the caller; reject overflow and partial numbers.
inline bool ParseNativeShortcutKey(const std::string& token, uint32_t& keycode) {
  constexpr char prefix[] = "nativekey";
  constexpr size_t length = sizeof(prefix) - 1;
  if (token.compare(0, length, prefix) != 0 || token.size() == length) return false;
  const auto* begin = token.data() + length;
  const auto* end = token.data() + token.size();
  if (*begin < '0' || *begin > '9') return false;
  auto result = std::from_chars(begin, end, keycode);
  return result.ec == std::errc() && result.ptr == end;
}

}  // namespace nativeapi::detail
