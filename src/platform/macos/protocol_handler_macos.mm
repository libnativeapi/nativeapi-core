#import <Cocoa/Cocoa.h>
#import <CoreServices/CoreServices.h>

#include "../../protocol_handler_dispatch.h"

// kAEGetURL is the Apple event LaunchServices sends for a URL of one of the
// app's CFBundleURLTypes schemes, both for the launch and for a running app.
// Installing a handler takes precedence over -application:openURLs: on the
// application delegate, so URLs reach nativeapi even when the host's delegate
// (FlutterAppDelegate) also implements it.
@interface NativeAPIProtocolUrlReceiver : NSObject {
 @public
  // The launch URL arrives as a GURL event inside -finishLaunching, after
  // NSApp reports isRunning and NSRunningApplication reports
  // isFinishedLaunching but before NSApplicationDidFinishLaunchingNotification,
  // so that notification is what ends the launch.
  BOOL launchFinished;
}
@end

@implementation NativeAPIProtocolUrlReceiver

- (void)install {
  [[NSAppleEventManager sharedAppleEventManager]
      setEventHandler:self
          andSelector:@selector(handleGetURLEvent:withReplyEvent:)
        forEventClass:kInternetEventClass
           andEventID:kAEGetURL];
}

// AppKit installs its own handlers while finishing launching; installing again
// here is the documented point at which a handler sees the launch event.
- (void)applicationWillFinishLaunching:(NSNotification*)notification {
  [self install];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  launchFinished = YES;
}

- (void)handleGetURLEvent:(NSAppleEventDescriptor*)event
           withReplyEvent:(NSAppleEventDescriptor*)reply {
  NSString* url = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
  if (url.length == 0)
    return;
  nativeapi::detail::ProtocolHandlerDispatch::Deliver(url.UTF8String, !launchFinished);
}

@end

namespace nativeapi::detail::protocol_platform {

namespace {

NativeAPIProtocolUrlReceiver* g_receiver = nil;

void InstallOnMainThread() {
  if (g_receiver)
    return;
  NativeAPIProtocolUrlReceiver* receiver = [[NativeAPIProtocolUrlReceiver alloc] init];
  g_receiver = receiver;
  [receiver install];
  NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
  [center addObserver:receiver
             selector:@selector(applicationWillFinishLaunching:)
                 name:NSApplicationWillFinishLaunchingNotification
               object:nil];
  [center addObserver:receiver
             selector:@selector(applicationDidFinishLaunching:)
                 name:NSApplicationDidFinishLaunchingNotification
               object:nil];
  if ([NSApp isRunning]) {
    // Created by a running app: either inside -finishLaunching (a host's
    // applicationWillFinishLaunching), where the notification still follows,
    // or later, when the launch is over once this turn of the loop ends.
    dispatch_async(dispatch_get_main_queue(), ^{
      receiver->launchFinished = YES;
    });
  }
}

}  // namespace

bool IsSupported() {
  return true;
}

void StartCapture() {
  if ([NSThread isMainThread]) {
    InstallOnMainThread();
  } else {
    dispatch_async(dispatch_get_main_queue(), ^{
      InstallOnMainThread();
    });
  }
}

bool Register(const std::string& scheme) {
  @autoreleasepool {
    NSBundle* bundle = [NSBundle mainBundle];
    NSArray* types = [bundle objectForInfoDictionaryKey:@"CFBundleURLTypes"];
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
            [declared caseInsensitiveCompare:wanted] == NSOrderedSame) {
          // A freshly built bundle may not be known to LaunchServices yet.
          LSRegisterURL((__bridge CFURLRef)bundle.bundleURL, false);
          return true;
        }
      }
    }
    return false;
  }
}

std::optional<std::string> ReadLaunchUrl() {
  return std::nullopt;
}

}  // namespace nativeapi::detail::protocol_platform
