#import <Foundation/Foundation.h>

#include "../../protocol_handler_dispatch.h"

// UIKit hands URLs to the application or scene delegate, which belongs to the
// host. The host forwards them (launch options / connection options as launch
// URLs, -application:openURL:options: and -scene:openURLContexts: as received
// URLs) through nativeapi_protocol_handler_host_deliver; the Flutter plugin
// protocol_handler (leanflutter) does exactly that.

namespace nativeapi::detail::protocol_platform {

bool IsSupported() {
  return ProtocolHandlerDispatch::IsHostAttached();
}

void StartCapture() {}

bool Register(const std::string& scheme) {
  @autoreleasepool {
    NSArray* types = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleURLTypes"];
    if (![types isKindOfClass:[NSArray class]])
      return false;
    NSString* wanted = [NSString stringWithUTF8String:scheme.c_str()];
    for (NSDictionary* type in types) {
      if (![type isKindOfClass:[NSDictionary class]])
        continue;
      NSArray* schemes = type[@"CFBundleURLSchemes"];
      if (![schemes isKindOfClass:[NSArray class]])
        continue;
      for (NSString* declared in schemes) {
        if ([declared isKindOfClass:[NSString class]] &&
            [declared caseInsensitiveCompare:wanted] == NSOrderedSame)
          return true;
      }
    }
    return false;
  }
}

std::optional<std::string> ReadLaunchUrl() {
  return std::nullopt;
}

}  // namespace nativeapi::detail::protocol_platform
