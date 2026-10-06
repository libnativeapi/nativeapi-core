// Optional real Flutter framework regression. No engine or windows are launched.
// A test coordinator stands in for Dart's asynchronous exit response; this tests
// actual FlutterAppDelegate routing, not a complete Dart AppLifecycleListener.
#include "nativeapi.h"
#import <Cocoa/Cocoa.h>
#import <FlutterMacOS/FlutterAppDelegate.h>
#include <iostream>
@interface TerminationCoordinator : NSObject {
 @public
  int requests;
}
@property(nonatomic) BOOL shouldTerminate;
- (void)requestApplicationTermination:(NSApplication*)app exitType:(NSUInteger)type result:(id)result;
@end
@implementation TerminationCoordinator
- (void)requestApplicationTermination:(NSApplication*)app exitType:(NSUInteger)type result:(id)result { ++requests; }
@end
int main() {
 @autoreleasepool {
  [NSApplication sharedApplication];
  auto* host = [[FlutterAppDelegate alloc] init];
  auto* coordinator = [[TerminationCoordinator alloc] init];
  [host setValue:coordinator forKey:@"terminationHandler"];
  NSApp.delegate = host;
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  auto& app = nativeapi::Application::GetInstance();
  if (NSApp.delegate != host || ![NSApp.delegate isKindOfClass:[FlutterAppDelegate class]]) return 1;
  if ([NSApp.delegate applicationShouldTerminate:NSApp] != NSTerminateCancel || coordinator->requests != 1) return 2;
  std::cout << "PASS actual FlutterAppDelegate identity and no-listener async exit-coordinator routing" << std::endl;
  int events = 0;
  auto listener = app.AddListener<nativeapi::ApplicationQuitRequestedEvent>([&](const auto&) { ++events; });
  if ([NSApp.delegate applicationShouldTerminate:NSApp] != NSTerminateCancel || coordinator->requests != 2 || events != 1) return 3;
  coordinator.shouldTerminate = YES;
  if ([NSApp.delegate applicationShouldTerminate:NSApp] != NSTerminateNow || events != 2) return 4;
  std::cout << "PASS native observation keeps Flutter coordinator cancellation and later approval" << std::endl;
  app.RemoveListener(listener);
  coordinator.shouldTerminate = NO;
  if ([NSApp.delegate applicationShouldTerminate:NSApp] != NSTerminateCancel || coordinator->requests != 3 || events != 2) return 5;
  std::cout << "PASS removing native listener preserves Flutter exit routing" << std::endl;
  NSApp.delegate = nil;
#if !__has_feature(objc_arc)
  [host release]; [coordinator release];
#endif
 }
 return 0;
}
