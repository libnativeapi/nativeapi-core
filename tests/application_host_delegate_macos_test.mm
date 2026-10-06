// Uses a test-owned NSApplication delegate. No windows, pointer events or
// real termination: the test host rejects every actual termination request.
#include "nativeapi.h"
#import <Cocoa/Cocoa.h>
#include <iostream>
#include <string>

static int failures;
static int destroyed_hosts;
static void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
@interface HostDelegate : NSObject <NSApplicationDelegate> {
 @public
  NSApplicationTerminateReply reply;
  int requests, activations, exits;
}
@end
@implementation HostDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  ++requests;
  return reply;
}
- (void)applicationDidBecomeActive:(NSNotification*)note { ++activations; }
- (void)applicationWillTerminate:(NSNotification*)note { ++exits; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }
- (void)dealloc {
  ++destroyed_hosts;
#if !__has_feature(objc_arc)
  [super dealloc];
#endif
}
@end
@interface DerivedHostDelegate : HostDelegate
@end
@implementation DerivedHostDelegate
@end

int main(int argc, char** argv) {
 @autoreleasepool {
  [NSApplication sharedApplication];
  auto* host = [[HostDelegate alloc] init];
  host->reply = NSTerminateCancel;
  if (argc == 2 && std::string(argv[1]) == "--late") {
    NSApp.delegate = nil;
    auto& app = nativeapi::Application::GetInstance();
    int requests = 0;
    auto listener = app.AddListener<nativeapi::ApplicationQuitRequestedEvent>([&](const auto&) { ++requests; });
    NSApp.delegate = host;
    Check(NSApp.delegate == host, "late host keeps its exact delegate object");
    Check([NSApp.delegate applicationShouldTerminate:NSApp] == NSTerminateCancel && requests == 1 && host->requests == 1,
          "host installed after standalone initialization retains its quit decision and observation");
    app.RemoveListener(listener);
    [NSApp.delegate applicationShouldTerminate:NSApp];
    Check(requests == 1 && host->requests == 2, "late host continues after native observation stops");
    NSApp.delegate = nil;
#if !__has_feature(objc_arc)
    [host release];
#endif
    return failures ? 1 : 0;
  }
  NSApp.delegate = host;
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  auto& app = nativeapi::Application::GetInstance();
  Check(NSApp.delegate == host, "initialization preserves exact host delegate object");
  Check(NSApp.activationPolicy == NSApplicationActivationPolicyAccessory, "initialization preserves host activation policy");
  Check([NSApp.delegate applicationShouldTerminate:NSApp] == NSTerminateCancel && host->requests == 1,
        "no native listeners preserves host cancellation");
  Check([NSApp.delegate respondsToSelector:@selector(applicationShouldTerminateAfterLastWindowClosed:)] &&
        [NSApp.delegate applicationShouldTerminateAfterLastWindowClosed:NSApp], "other host delegate methods remain available");
  int quit_events = 0, active_events = 0, exit_events = 0;
  auto listener = app.AddListener<nativeapi::ApplicationEvent>([&](const auto& event) {
    if (dynamic_cast<const nativeapi::ApplicationQuitRequestedEvent*>(&event)) ++quit_events;
    if (dynamic_cast<const nativeapi::ApplicationActivatedEvent*>(&event)) ++active_events;
    if (dynamic_cast<const nativeapi::ApplicationExitingEvent*>(&event)) ++exit_events;
  });
  Check(NSApp.delegate == host && [NSApp.delegate class] == [HostDelegate class], "listening preserves host class identity");
  for (auto reply : {NSTerminateCancel, NSTerminateLater, NSTerminateNow}) {
    host->reply = reply;
    int before = quit_events;
    Check([NSApp.delegate applicationShouldTerminate:NSApp] == reply && quit_events == before + 1,
          "native quit observation keeps exact cancel/defer/allow reply");
    if (reply == NSTerminateLater) [NSApp replyToApplicationShouldTerminate:NO];
  }
  host->reply = NSTerminateCancel;
  if (NSApp.delegate == host) {
    const int before = quit_events;
    app.Quit(7);
    Check(quit_events == before + 1, "explicit Quit emits once and host can still cancel termination");
    [NSApp.delegate applicationShouldTerminate:NSApp];
    Check(quit_events == before + 2, "later user quit is a fresh request after host cancellation");
  }
  auto* center = NSNotificationCenter.defaultCenter;
  [center postNotificationName:NSApplicationDidBecomeActiveNotification object:NSApp];
  Check(active_events == 1 && host->activations == 1, "host lifecycle and native notification both delivered once");
  [center postNotificationName:NSApplicationWillTerminateNotification object:NSApp];
  Check(exit_events == 1 && host->exits == 1, "host cleanup notification and native exiting event preserved");
  app.RemoveListener(listener);
  const int before_stopped = quit_events;
  [NSApp.delegate applicationShouldTerminate:NSApp];
  [center postNotificationName:NSApplicationDidBecomeActiveNotification object:NSApp];
  Check(NSApp.delegate == host && quit_events == before_stopped && active_events == 1 && host->activations == 2,
        "last listener removal stops native observation while host continues");
  listener = app.AddListener<nativeapi::ApplicationQuitRequestedEvent>([&](const auto&) { ++quit_events; });
  const int before_restart = quit_events;
  [NSApp.delegate applicationShouldTerminate:NSApp];
  Check(quit_events == before_restart + 1, "registration restarts without duplicate observers");
  app.RemoveListener(listener);
  auto* derived = [[DerivedHostDelegate alloc] init];
  derived->reply = NSTerminateCancel;
  NSApp.delegate = derived;
  listener = app.AddListener<nativeapi::ApplicationQuitRequestedEvent>([&](const auto&) { ++quit_events; });
  const int before_derived = quit_events;
  [NSApp.delegate applicationShouldTerminate:NSApp];
  Check(quit_events == before_derived + 1 && derived->requests == 1, "inherited delegate interception emits only once");
  NSApp.delegate = host;
  const int before_replacement = quit_events;
  [NSApp.delegate applicationShouldTerminate:NSApp];
  Check(quit_events == before_replacement + 1 && NSApp.delegate == host,
        "delegate replacement while listening is observed without changing identity");
  [derived applicationShouldTerminate:NSApp];
  Check(quit_events == before_replacement + 1, "former delegate no longer announces application quit");
  app.RemoveListener(listener);
  NSApp.delegate = nil;
#if !__has_feature(objc_arc)
  [host release]; [derived release];
#else
  host = nil; derived = nil;
#endif
 }
  Check(destroyed_hosts == 2, "observer does not retain host delegate objects after draining autoreleases");
 return failures ? 1 : 0;
}
