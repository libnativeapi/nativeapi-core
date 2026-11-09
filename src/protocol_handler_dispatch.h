#pragma once

// Private platform/host protocol; not part of API_HEADERS or the umbrella.
#include <optional>
#include <string>

namespace nativeapi::detail {

// The narrow platform seam of ProtocolHandler, defined once per platform.
namespace protocol_platform {
// Whether this process can receive URLs now (Android/iOS: a host has attached).
bool IsSupported();
// Called once, from the ProtocolHandler constructor, on the thread that first
// asked for the instance. Starts whatever native capture the platform has.
void StartCapture();
// scheme is already validated and lower-cased.
bool Register(const std::string& scheme);
// A launch URL the platform can read at any time (the Windows command line).
std::optional<std::string> ReadLaunchUrl();
}  // namespace protocol_platform

class ProtocolHandlerDispatch {
 public:
  // Any thread. A launch URL becomes the initial URL when none was recorded and
  // no other URL arrived first; everything else is emitted or queued.
  static void Deliver(const std::string& url, bool is_launch);
  // Mobile hosts (Android/iOS) mark the process as able to receive URLs.
  static void MarkHostAttached();
  static bool IsHostAttached();
};

}  // namespace nativeapi::detail

// Host bridge for hosts that cannot link core but load its shared library, i.e.
// the leanflutter protocol_handler Flutter plugin (dlsym on Apple, JNI on Android).
// The names and signatures are a stable contract; see specs/protocol-handler.md.
extern "C" {
// Creates the handler (starting native capture) and marks the host attached.
void nativeapi_protocol_handler_host_attach(void);
// url is UTF-8; is_launch is non-zero for the URL that launched the process.
void nativeapi_protocol_handler_host_deliver(const char* url, int is_launch);
}
