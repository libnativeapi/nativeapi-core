// Real AppKit delegate and native producer routing. Only NSApplication's final
// termination/reply is substituted; no windows, input or process exit.
#import <Cocoa/Cocoa.h>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include "../src/application_quit_dispatch.h"
#include "nativeapi.h"

@interface NativeQuitTestApplication : NSApplication {
 @public
  int terminates, terminal_approvals, replies;
  NSApplicationTerminateReply latest;
}
@end
@implementation NativeQuitTestApplication
- (void)terminate:(id)sender {
  ++terminates;
  latest = [self.delegate applicationShouldTerminate:self];
  if (latest == NSTerminateNow)
    ++terminal_approvals;
}
- (void)replyToApplicationShouldTerminate:(BOOL)allowed {
  ++replies;
  if (allowed)
    ++terminal_approvals;
}
@end
@interface NativeQuitTestHost : NSObject <NSApplicationDelegate> {
 @public
  NSApplicationTerminateReply reply;
  int requests;
  BOOL reenter;
}
@end
@implementation NativeQuitTestHost
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app {
  ++requests;
  if (reenter) {
    reenter = NO;
    [NSApp terminate:nil];
  }
  return reply;
}
@end
namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
}  // namespace
int main() {
  @autoreleasepool {
    auto* platform = (NativeQuitTestApplication*)[NativeQuitTestApplication sharedApplication];
    auto* host = [[NativeQuitTestHost alloc] init];
    host->reply = NSTerminateCancel;
    NSApp.delegate = host;
    std::mutex mutex;
    std::vector<std::function<void()>> queue;
    const auto ui = std::this_thread::get_id();
    nativeapi::SetMainThreadDispatcher(
        [&](auto work) {
          std::lock_guard<std::mutex> lock(mutex);
          queue.push_back(std::move(work));
          return true;
        },
        [ui] { return std::this_thread::get_id() == ui; });
    auto drain = [&] {
      for (;;) {
        std::vector<std::function<void()>> work;
        {
          std::lock_guard<std::mutex> lock(mutex);
          work.swap(queue);
        }
        if (work.empty())
          return;
        for (auto& callback : work)
          callback();
      }
    };
    NSApp.delegate = nil;
    nativeapi::Application* instance = nullptr;
    std::thread constructing([&] { instance = &nativeapi::Application::GetInstance(); });
    constructing.join();
    auto& app = *instance;
    Check(!app.IsRunning() && NSApp.delegate != nil,
          "UI bootstrap initializes a worker-created singleton before the run loop starts");
    NSApp.delegate = host;
    drain();
    Check(NSApp.delegate == host,
          "first construction from a worker initializes on UI and preserves host");
    [NSApp terminate:nil];
    Check(platform->latest == NSTerminateCancel && host->requests == 1,
          "no native listeners preserve the original host cancellation");
    size_t worker_listener = 0;
    int worker_events = 0;
    std::thread registering([&] {
      worker_listener =
          app.AddListener<nativeapi::ApplicationQuitRequestedEvent>([&](const auto& event) {
            Check(std::this_thread::get_id() == ui,
                  "worker-registered native listener is invoked on UI");
            ++worker_events;
            event.GetRequest()->Cancel();
          });
    });
    registering.join();
    drain();
    [NSApp terminate:nil];
    Check(worker_events == 1 && host->requests == 1,
          "worker listener registration installs native monitoring on UI");
    std::thread removing([&] { app.RemoveListener(worker_listener); });
    removing.join();
    drain();
    enum Mode { Veto, Defer, Accept } mode = Veto;
    int requests = 0, required = 0, exiting = 0;
    std::shared_ptr<nativeapi::EventRequest> request;
    std::shared_ptr<nativeapi::EventDecision> a, b;
    auto listener =
        app.AddListener<nativeapi::ApplicationQuitRequestedEvent>([&](const auto& event) {
          Check(std::this_thread::get_id() == ui, "native quit callback runs on UI");
          request = event.GetRequest();
          Check(request != nullptr, "native quit always supplies a shared request");
          if (!request->IsCancelable()) {
            ++required;
            Check(!request->Cancel() && !request->Defer(),
                  "required termination cannot be vetoed or deferred");
            return;
          }
          ++requests;
          if (mode == Veto)
            request->Cancel();
          if (mode == Defer) {
            a = request->Defer();
            b = request->Defer();
          }
        });
    auto exit_listener =
        app.AddListener<nativeapi::ApplicationExitingEvent>([&](const auto&) { ++exiting; });
    [NSApp terminate:nil];
    Check(requests == 1 && host->requests == 1 && platform->latest == NSTerminateCancel,
          "reentrant native quit stays coalesced before synchronous veto");
    mode = Defer;
    [NSApp terminate:nil];
    [NSApp terminate:nil];
    app.Quit(17);
    Check(requests == 2 && host->requests == 1 && request->IsPending(),
          "native and programmatic quits share the pending confirmation");
    a->Accept();
    std::thread worker([&] { b->Accept(); });
    worker.join();
    [NSApp terminate:nil];
    Check(requests == 2 && host->requests == 1,
          "accepted worker reply stays coalesced until UI replay");
    drain();
    Check(host->requests == 2 && platform->terminal_approvals == 0,
          "async approval replays once and preserves original host refusal");
    Check(!nativeapi::detail::ApplicationQuitDispatch::CurrentRequest(),
          "host refusal releases the request for a fresh user attempt");
    mode = Accept;
    host->reply = NSTerminateLater;
    [NSApp terminate:nil];
    Check(requests == 3 && platform->latest == NSTerminateLater,
          "approved native quit preserves NSTerminateLater");
    [NSApp terminate:nil];
    app.Quit(29);
    Check(requests == 3 && host->requests == 3,
          "host's deferred decision retains the shared quit slot");
    [NSApp replyToApplicationShouldTerminate:NO];
    Check(platform->replies == 1 && !nativeapi::detail::ApplicationQuitDispatch::CurrentRequest(),
          "host's asynchronous refusal releases only its own request");
    [NSApp terminate:nil];
    [NSApp replyToApplicationShouldTerminate:YES];
    [NSNotificationCenter.defaultCenter postNotificationName:NSApplicationWillTerminateNotification
                                                      object:NSApp];
    Check(requests == 4 && required == 0 && exiting == 1 && platform->terminal_approvals == 1,
          "host's asynchronous approval emits no duplicate quit request");
    mode = Defer;
    host->reply = NSTerminateCancel;
    [NSApp terminate:nil];
    auto old_a = a, old_b = b;
    auto throwing_exit = app.AddListener<nativeapi::ApplicationExitingEvent>(
        [](const auto&) { throw std::runtime_error("terminal observer failure"); });
    [NSNotificationCenter.defaultCenter postNotificationName:NSApplicationWillTerminateNotification
                                                      object:NSApp];
    app.RemoveListener(throwing_exit);
    const int before_force = platform->terminates;
    Check(!old_a->Accept() && !old_b->Accept(), "mandatory native exit invalidates old votes");
    drain();
    Check(required == 1 && exiting == 2 && platform->terminates == before_force,
          "required native exit notification never schedules a second termination");
    [NSApp terminate:nil];
    old_a = a;
    old_b = b;
    auto* replacement = [[NativeQuitTestHost alloc] init];
    replacement->reply = NSTerminateCancel;
    NSApp.delegate = replacement;
    Check(!old_a->Accept() && !old_b->Accept(), "delegate replacement fences old approvals");
    drain();
    mode = Accept;
    [NSApp terminate:nil];
    Check(replacement->requests == 1 && NSApp.delegate == replacement,
          "replacement host retains its object identity and original decision");
    mode = Defer;
    [NSApp terminate:nil];
    app.RemoveListener(listener);
    app.RemoveListener(exit_listener);
    a->Accept();
    b->Accept();
    drain();
    Check(
        replacement->requests == 2 && !nativeapi::detail::ApplicationQuitDispatch::CurrentRequest(),
        "an owned vote outlives listener removal and completes the original host action");
    int nested_events = 0;
    auto nested_listener = app.AddListener<nativeapi::ApplicationQuitRequestedEvent>(
        [&](const auto&) { ++nested_events; });
    const int original_calls = replacement->requests;
    replacement->reenter = YES;
    [NSApp terminate:nil];
    Check(nested_events == 1 && replacement->requests == original_calls + 1,
          "a host reentering terminate during its own decision remains coalesced");
    app.RemoveListener(nested_listener);
    NSApp.delegate = nil;
    [host release];
    [replacement release];
    nativeapi::SetMainThreadDispatcher(nullptr, nullptr);
    return failures ? 1 : 0;
  }
}
