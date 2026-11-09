#include "../../protocol_handler_dispatch.h"

// Custom protocol activation is not available on this platform.

namespace nativeapi::detail::protocol_platform {

bool IsSupported() {
  return false;
}

void StartCapture() {}

bool Register(const std::string&) {
  return false;
}

std::optional<std::string> ReadLaunchUrl() {
  return std::nullopt;
}

}  // namespace nativeapi::detail::protocol_platform
