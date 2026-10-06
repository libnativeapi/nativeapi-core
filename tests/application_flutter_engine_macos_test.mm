// Optional integration against a real headless FlutterEngine and Dart
// AppLifecycleListener. No NSWindow is created and no input is generated.
// Requires a bundle containing application_exit_flutter.dart's debug assets.
#import <Cocoa/Cocoa.h>
#import <FlutterMacOS/FlutterMacOS.h>
#include <functional>
#include <iostream>
#include <thread>
#include "../src/application_quit_dispatch.h"
#include "nativeapi.h"

// Replace only final AppKit termination. Native replay still calls terminate:
// and the original Flutter delegate/engine/channel perform their real work.
@interface HeadlessQuitApplication : NSApplication
@end
@implementation HeadlessQuitApplication
- (void)terminate:(id)sender {
  [self.delegate applicationShouldTerminate:self];
}
@end

struct TestState {
  bool ready = false;
  int requests = 0, approvals = 0;
  NSMutableArray* replies = [NSMutableArray array];
};

static bool WaitFor(const std::function<bool()>& predicate) {
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:10];
  while (!predicate() && deadline.timeIntervalSinceNow > 0)
    [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode
                           beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
  return predicate();
}

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc != 2)
      return 1;
    NSBundle* assets = [NSBundle bundleWithPath:[NSString stringWithUTF8String:argv[1]]];
    if (!assets)
      return 2;
    [HeadlessQuitApplication sharedApplication];
    auto* host = [[FlutterAppDelegate alloc] init];
    NSApp.delegate = host;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    // Reproduce initialization before Flutter creates its termination handler.
    nativeapi::Application* instance = nullptr;
    std::thread constructing([&] { instance = &nativeapi::Application::GetInstance(); });
    constructing.join();
    auto& app = *instance;
    if (NSApp.delegate != host)
      return 3;
    auto* project = [[FlutterDartProject alloc] initWithPrecompiledDartBundle:assets];
    auto* engine = [[FlutterEngine alloc] initWithName:@"nativeapi-exit-test"
                                               project:project
                                allowHeadlessExecution:YES];
    id coordinator = [host valueForKey:@"terminationHandler"];
    if (!coordinator)
      return 4;
    auto state = std::make_shared<TestState>();
    // Flutter's own test seam substitutes only the final process termination.
    // The actual handler, channels, Dart callback and async response remain real.
    [coordinator setValue:[^(id sender) {
                   // Replay the actual delegate in the terminator's approval scope. Calling
                   // NSApp terminate here would really exit this headless test process.
                   if (sender == NSApp &&
                       [NSApp.delegate applicationShouldTerminate:NSApp] == NSTerminateNow)
                     ++state->approvals;
                 } copy]
                   forKey:@"terminator"];
    auto* channel = [FlutterMethodChannel methodChannelWithName:@"nativeapi/test-exit"
                                                binaryMessenger:engine.binaryMessenger];
    [channel setMethodCallHandler:^(FlutterMethodCall* call, FlutterResult result) {
      if ([call.method isEqualToString:@"ready"]) {
        state->ready = true;
        result(nil);
      } else if ([call.method isEqualToString:@"request"]) {
        ++state->requests;
        [state->replies addObject:[result copy]];
      } else
        result(FlutterMethodNotImplemented);
    }];
    if (![engine runWithEntrypoint:nil] || !WaitFor([&] { return state->ready; }))
      return 5;
    if (NSApp.windows.count != 0 || engine.viewController != nil)
      return 7;
    int failures = 0, events = 0, required = 0;
    auto check = [&](bool ok, const char* label) {
      std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
      failures += !ok;
    };
    auto ask = [&] { return [NSApp.delegate applicationShouldTerminate:NSApp]; };
    auto pending = [&] { return nativeapi::detail::ApplicationQuitDispatch::CurrentRequest(); };
    auto respond = [&](bool allow) {
      if (!state->replies.count) {
        check(false, "Dart response exists");
        return;
      }
      FlutterResult reply = [state->replies objectAtIndex:0];
      [state->replies removeObjectAtIndex:0];
      reply(@(allow));
    };
    check(ask() == NSTerminateCancel && WaitFor([&] { return state->requests == 1; }),
          "no native listeners preserve the real Dart asynchronous request");
    respond(false);
    check(WaitFor([&] { return ![[coordinator valueForKey:@"shouldTerminate"] boolValue]; }) &&
              !state->approvals,
          "no native listeners preserve real Dart cancellation");
    check(ask() == NSTerminateCancel && WaitFor([&] { return state->requests == 2; }),
          "no native listeners allow a fresh Flutter request");
    respond(true);
    check(WaitFor([&] { return state->approvals == 1; }),
          "no native listeners preserve Flutter approval");
    // The final process exit is substituted, so reset the engine's final flag.
    [coordinator setValue:@NO forKey:@"shouldTerminate"];
    enum Mode { Veto, Defer, Accept } mode = Veto;
    std::shared_ptr<nativeapi::EventDecision> a, b;
    auto listener =
        app.AddListener<nativeapi::ApplicationQuitRequestedEvent>([&](const auto& event) {
          auto request = event.GetRequest();
          check(request && NSThread.isMainThread, "native quit supplies a request on UI");
          if (!request)
            return;
          if (!request->IsCancelable()) {
            ++required;
            check(!request->Cancel() && !request->Defer(),
                  "required Flutter exit cannot be vetoed or deferred");
            return;
          }
          ++events;
          if (mode == Veto)
            request->Cancel();
          if (mode == Defer) {
            a = request->Defer();
            b = request->Defer();
          }
        });
    check(ask() == NSTerminateCancel && events == 1 && state->requests == 2 && !pending(),
          "native veto precedes Flutter's Dart request");
    mode = Defer;
    check(ask() == NSTerminateCancel && events == 2 && pending(),
          "native deferred request remains pending");
    ask();
    app.Quit(17);
    check(events == 2 && state->requests == 2,
          "native and programmatic attempts coalesce before approval");
    a->Accept();
    std::thread worker([&] { b->Accept(); });
    worker.join();
    check(WaitFor([&] { return state->requests == 3; }) && pending(),
          "worker approval resumes the real Flutter request on UI");
    check([[coordinator valueForKey:@"shouldTerminate"] boolValue] && ask() == NSTerminateCancel,
          "Flutter's early shouldTerminate flag cannot authorize a repeated user quit");
    app.Quit(23);
    check(events == 2 && state->requests == 3 && state->approvals == 1,
          "programmatic quit also coalesces while Dart confirmation is pending");
    respond(false);
    check(WaitFor([&] { return !pending(); }) && state->approvals == 1,
          "Dart cancellation releases the native request for retry");
    mode = Accept;
    check(ask() == NSTerminateCancel && WaitFor([&] { return state->requests == 4; }),
          "native retry reaches a fresh Dart decision");
    respond(true);
    check(WaitFor([&] { return state->approvals == 2; }) && events == 3 && !pending() &&
              NSApp.delegate == host,
          "Dart approval replays once with no second native prompt or host replacement");
    [coordinator setValue:@NO forKey:@"shouldTerminate"];
    check(ask() == NSTerminateCancel && WaitFor([&] { return state->requests == 5; }),
          "start a Dart request before delegate replacement");
    auto* replacement = [[FlutterAppDelegate alloc] init];
    NSApp.delegate = replacement;
    NSApp.delegate = host;
    [coordinator setValue:@NO forKey:@"shouldTerminate"];
    check(ask() == NSTerminateCancel && WaitFor([&] { return state->requests == 6; }) && pending(),
          "a fresh native request starts while the old Dart reply is outstanding");
    respond(true);  // Old engine token; the new native request is still pending.
    check(pending() && ask() == NSTerminateCancel && state->approvals == 2,
          "a stale Dart approval cannot borrow the new request's approval");
    respond(false);
    check(WaitFor([&] { return !pending(); }) && state->approvals == 2,
          "the new Dart cancellation releases only the new request");
    mode = Defer;
    check(ask() == NSTerminateCancel && pending(),
          "defer native quit before required Flutter exit");
    auto old_a = a, old_b = b;
    // Required exits bypass Dart's cancelable callback in the real engine.
    SEL selector = NSSelectorFromString(@"requestApplicationTermination:exitType:result:");
    using RequiredFn = void (*)(id, SEL, id, NSInteger, FlutterResult);
    reinterpret_cast<RequiredFn>([coordinator methodForSelector:selector])(coordinator, selector,
                                                                           NSApp, 1, nil);
    check(required == 1 && state->approvals == 3 && !pending() && !old_a->Accept() &&
              !old_b->Accept(),
          "required Flutter exit invalidates pending votes and continues without confirmation");
    [coordinator setValue:@NO forKey:@"shouldTerminate"];
    check(ask() == NSTerminateCancel && pending(),
          "owned native vote begins before listener removal");
    app.RemoveListener(listener);
    a->Accept();
    b->Accept();
    check(WaitFor([&] { return state->requests == 7; }), "owned native vote outlives its listener");
    respond(false);
    check(WaitFor([&] { return !pending(); }),
          "Dart reply releases the request after listener removal");
    [channel setMethodCallHandler:nil];
    [engine shutDownEngine];
    NSApp.delegate = nil;
    return failures ? 6 : 0;
  }
}
