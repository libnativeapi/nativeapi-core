// Real AppKit close routing on unshown windows; no input or app termination.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <iostream>
#include <thread>
#include <vector>
#include "nativeapi.h"

@interface WindowCloseHost : NSObject <NSWindowDelegate> {
 @public
  int asked;
  bool allow;
}
@end
@implementation WindowCloseHost
- (BOOL)windowShouldClose:(NSWindow*)sender {
  ++asked;
  return allow;
}
@end
@interface WindowCloseTarget : NSWindow {
 @public
  int performed, closed;
}
@end
@implementation WindowCloseTarget
- (void)performClose:(id)sender {
  ++performed;
  [super performClose:sender];
}
- (void)close {
  ++closed;
  [super close];
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
    [NSApplication sharedApplication];
    std::vector<std::function<void()>> jobs;
    auto ui = std::this_thread::get_id();
    std::mutex mutex;
    nativeapi::SetMainThreadDispatcher(
        [&](auto job) {
          std::lock_guard<std::mutex> lock(mutex);
          jobs.push_back(std::move(job));
          return true;
        },
        [ui] { return std::this_thread::get_id() == ui; });
    auto drain = [&] {
      for (;;) {
        std::vector<std::function<void()>> work;
        {
          std::lock_guard<std::mutex> lock(mutex);
          work.swap(jobs);
        }
        if (work.empty())
          break;
        for (auto& job : work)
          job();
      }
    };
    auto* host = [[WindowCloseHost alloc] init];
    host->allow = false;
    auto make = [&] {
      auto* window = [[WindowCloseTarget alloc]
          initWithContentRect:NSMakeRect(50, 50, 160, 100)
                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                      backing:NSBackingStoreBuffered
                        defer:NO];
      window.releasedWhenClosed = NO;
      window.delegate = host;
      return window;
    };
    auto* native = make();
    {
      nativeapi::Window first(native), second(native);
      Class original_class = object_getClass(native);
      int events = 0;
      enum Mode { Veto, Defer, Allow } mode = Veto;
      std::shared_ptr<nativeapi::EventRequest> request;
      std::shared_ptr<nativeapi::EventDecision> a, b;
      auto listener =
          first.AddListener<nativeapi::WindowCloseRequestedEvent>([&](const auto& event) {
            ++events;
            request = event.GetRequest();
            Check(std::this_thread::get_id() == ui, "request callback on the UI thread");
            if (mode == Veto)
              request->Cancel();
            if (mode == Defer)
              a = request->Defer();
          });
      auto alias = second.AddListener<nativeapi::WindowCloseRequestedEvent>([&](const auto& event) {
        Check(event.GetRequest() == request, "native aliases share one request");
        if (mode == Defer)
          b = request->Defer();
      });
      Check(first.GetId() == second.GetId(), "wrapped native identity is shared");
      Check(first.Close() && events == 1 && native->performed == 0 && host->asked == 0,
            "public veto runs before the original host close path");
      [native performClose:nil];
      Check(events == 2 && native->performed == 0,
            "native performClose passes through the same gate");
      mode = Defer;
      first.Close();
      second.Close();
      Check(events == 3 && request->IsPending(), "pending aliases coalesce");
      a->Accept();
      Check(native->performed == 0, "one alias cannot approve the other");
      std::thread worker([&] { b->Accept(); });
      worker.join();
      Check(native->performed == 0, "background approval waits for UI dispatch");
      first.Close();
      Check(events == 3, "queued approval still shares pending state");
      drain();
      Check(native->performed == 1 && host->asked == 1 && native->closed == 0,
            "approval preserves the host delegate's refusal");
      Check(object_getClass(native) == original_class && native.delegate == host,
            "runtime class and original delegate are preserved");
      mode = Allow;
      host->allow = true;
      first.Close();
      Check(events == 4 && native->performed == 2 && native->closed == 1,
            "retry after host refusal closes once through original methods");
      Check(!first.Close(), "closed native lifetime cannot be reused");
      first.RemoveListener(listener);
      second.RemoveListener(alias);
    }
    [native release];
    native = make();
    host->allow = true;
    {
      auto wrapper = std::make_unique<nativeapi::Window>(native);
      int cancelable = 0, required = 0;
      std::shared_ptr<nativeapi::EventDecision> old;
      wrapper->AddListener<nativeapi::WindowCloseRequestedEvent>([&](const auto& event) {
        auto request = event.GetRequest();
        if (request->IsCancelable()) {
          ++cancelable;
          old = request->Defer();
        } else {
          ++required;
          request->Cancel();
          Check(!request->Defer(), "force notification cannot defer");
        }
      });
      wrapper->Close();
      [native close];
      old->Accept();
      drain();
      Check(cancelable == 1 && required == 1 && native->closed == 1 && native->performed == 0,
            "raw native close proceeds and fences the old deferred action");
      wrapper.reset();
    }
    [native release];
    native = make();
    {
      auto wrapper = std::make_unique<nativeapi::Window>(native);
      int callbacks = 0;
      std::thread worker([&] {
        wrapper->AddListener<nativeapi::WindowCloseRequestedEvent>(
            [&](const auto&) { ++callbacks; });
        wrapper->Close();
      });
      worker.join();
      wrapper.reset();
      drain();
      Check(callbacks == 0 && native->closed == 0,
            "destroyed C++ wrapper fences queued setup and close");
    }
    [native close];
    [native release];
    [host release];
    nativeapi::SetMainThreadDispatcher(nullptr, nullptr);
    return failures ? 1 : 0;
  }
}
