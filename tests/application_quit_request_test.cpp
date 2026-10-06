// Real Application quit producers with a binding-owned loop completion. No
// native windows, input or process termination; GTK needs a private display.
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include "../src/application_quit_dispatch.h"
#include "nativeapi.h"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
@interface QuitTestHost : NSObject <NSApplicationDelegate>
@end
@implementation QuitTestHost
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  return NSTerminateCancel;
}
@end
#endif

namespace {
int failures = 0;
void Check(bool value, const char* label) {
  std::cout << (value ? "PASS " : "FAIL ") << label << std::endl;
  failures += !value;
}
std::mutex mutex;
std::atomic<bool> reject_dispatch{false};
std::vector<std::function<void()>> queued;
void Drain() {
  for (;;) {
    std::vector<std::function<void()>> work;
    {
      std::lock_guard<std::mutex> lock(mutex);
      work.swap(queued);
    }
    if (work.empty())
      return;
    for (auto& callback : work)
      callback();
  }
}
}  // namespace

int main() {
#if defined(__APPLE__)
  @autoreleasepool {
    [NSApplication sharedApplication];
    auto* host = [[QuitTestHost alloc] init];
    NSApp.delegate = host;
#endif
    const auto ui = std::this_thread::get_id();
    nativeapi::SetMainThreadDispatcher(
        [](auto fn) {
          if (reject_dispatch)
            return false;
          std::lock_guard<std::mutex> lock(mutex);
          queued.push_back(std::move(fn));
          return true;
        },
        [ui] { return std::this_thread::get_id() == ui; });
    auto& app = nativeapi::Application::GetInstance();
    enum class Mode { Cancel, Defer, Reenter, Throw, Allow } mode = Mode::Cancel;
    int requests = 0, stops = 0, exits = 0, last_exit = -1;
    bool reenter_on_exit = false;
    std::shared_ptr<nativeapi::EventRequest> request;
    std::shared_ptr<nativeapi::EventDecision> vote1, vote2;
    auto listener =
        app.AddListener<nativeapi::ApplicationQuitRequestedEvent>([&](const auto& event) {
          ++requests;
          request = event.GetRequest();
          Check(request && request->IsCancelable() && request->IsPending(),
                "Quit supplies a live shared cancelable request");
          if (mode == Mode::Cancel)
            request->Cancel();
          else if (mode == Mode::Defer) {
            vote1 = request->Defer();
            vote2 = request->Defer();
          } else if (mode == Mode::Reenter)
            app.Quit(29);
          else if (mode == Mode::Throw)
            throw std::runtime_error("failed confirmation");
        });
    auto exiting = app.AddListener<nativeapi::ApplicationExitingEvent>([&](const auto& event) {
      ++exits;
      last_exit = event.GetExitCode();
      Check(std::this_thread::get_id() == ui, "exiting event stays on the UI thread");
      if (reenter_on_exit)
        app.Quit(97);
    });
    auto stop = [&](int code) {
      ++stops;
      Check(std::this_thread::get_id() == ui && code == last_exit,
            "loop stops on UI after its exiting event");
    };
    auto quit_loop = [&](int code) {
      nativeapi::detail::ApplicationQuitDispatch::RequestForLoop(code, stop);
    };

    app.Quit(7);
    Check(requests == 1 && request->IsCancelled() && !request->IsPending() && exits == 0,
          "synchronous veto prevents the native Quit action");
    mode = Mode::Defer;
    quit_loop(11);
    app.Quit(13);
    quit_loop(17);
    Check(requests == 2 && request->IsPending() && stops == 0,
          "repeated Quit shares pending confirmation");
    std::thread([&] { Check(vote1->Accept(), "first worker accepts its vote"); }).join();
    Drain();
    Check(request->IsPending() && stops == 0,
          "first approval cannot bypass another outstanding vote");
    std::thread([&] { Check(vote2->Cancel(), "worker veto resolves request"); }).join();
    Check(stops == 0 && exits == 0, "worker veto never stops the loop");
    Drain();
    Check(request->IsCancelled() && !request->IsPending() && stops == 0,
          "UI completion clears the cancelled attempt");
    quit_loop(19);
    Check(requests == 3, "a later quit starts a fresh request");
    vote1->Accept();
    std::thread([&] { vote2->Accept(); }).join();
    Check(stops == 0 && exits == 0, "worker approval cannot execute the UI action inline");
    app.Quit(23);
    Check(requests == 3, "approval queued to UI still deduplicates another Quit");
    Drain();
    Check(stops == 1 && exits == 1 && last_exit == 23,
          "all votes accept once with the last requested exit code");
    Check(!vote2->Accept() && !request->Cancel(), "late decisions cannot quit again");
    mode = Mode::Reenter;
    quit_loop(27);
    Check(requests == 4 && stops == 2 && last_exit == 29,
          "listener reentrant Quit updates code without recursive dispatch");
    mode = Mode::Throw;
    quit_loop(31);
    Check(requests == 5 && stops == 2 && request->IsCancelled(),
          "throwing confirmation fails closed without escaping Quit");
    mode = Mode::Allow;
    std::thread([&] { quit_loop(37); }).join();
    Check(requests == 5, "worker Quit queues the entire confirmation to UI");
    Drain();
    Check(requests == 6 && stops == 3 && last_exit == 37,
          "queued worker Quit emits and completes once on UI");
    mode = Mode::Defer;
    quit_loop(39);
    vote1->Accept();
    reject_dispatch = true;
    std::thread([&] { vote2->Accept(); }).join();
    Check(stops == 3, "failed UI dispatch cannot execute termination on a worker");
    reject_dispatch = false;
    mode = Mode::Allow;
    quit_loop(40);
    Check(stops == 4 && requests == 8, "a UI retry can recover after completion dispatch fails");
    reenter_on_exit = true;
    quit_loop(41);
    Check(stops == 5 && requests == 9 && last_exit == 41,
          "Quit from an exiting listener cannot recursively terminate or change the announced exit "
          "code");
    app.RemoveListener(listener);
    app.RemoveListener(exiting);
    nativeapi::detail::ApplicationQuitDispatch::RequestForLoop(43, [&](int code) {
      ++stops;
      Check(std::this_thread::get_id() == ui && code == 43,
            "unobserved loop completion remains on UI with its exit code");
    });
    Check(stops == 6, "no listeners approves immediately");

    std::shared_ptr<nativeapi::EventDecision> owned_vote;
    std::shared_ptr<nativeapi::EventRequest> owned_request;
    int owned_requests = 0, owned_stops = 0;
    auto owned_listener =
        app.AddListener<nativeapi::ApplicationQuitRequestedEvent>([&](const auto& event) {
          ++owned_requests;
          owned_request = event.GetRequest();
          owned_vote = owned_request->Defer();
        });
    auto owned_stop = [&](int) { ++owned_stops; };
    auto owner = std::make_shared<int>(0);
    nativeapi::detail::ApplicationQuitDispatch::RequestForLoop(51, owned_stop, owner);
    auto previous_vote = owned_vote;
    auto previous_request = owned_request;
    owner.reset();
    owner = std::make_shared<int>(0);
    nativeapi::detail::ApplicationQuitDispatch::RequestForLoop(53, owned_stop, owner);
    Check(owned_requests == 2 && !previous_request->IsPending() && !previous_vote->Accept(),
          "new loop invalidates an expired owner's outstanding confirmation");
    owned_vote->Accept();
    Check(owned_stops == 1, "only the new loop can stop after the previous owner expires");
    nativeapi::detail::ApplicationQuitDispatch::RequestForLoop(55, owned_stop, owner);
    previous_vote = owned_vote;
    std::weak_ptr<void> expired_owner = owner;
    owner.reset();
    nativeapi::detail::ApplicationQuitDispatch::CancelForLoop(expired_owner);
    Check(!owned_request->IsPending() && !previous_vote->Accept() && owned_stops == 1,
          "ending a loop eagerly invalidates its outstanding decisions");
    owner = std::make_shared<int>(0);
    nativeapi::detail::ApplicationQuitDispatch::RequestForLoop(57, owned_stop, owner);
    std::thread([&] { owned_vote->Accept(); }).join();
    owner.reset();
    Drain();
    Check(owned_stops == 1, "UI-queued approval cannot stop an expired loop");
    owner = std::make_shared<int>(0);
    std::thread([&] {
      nativeapi::detail::ApplicationQuitDispatch::RequestForLoop(59, owned_stop, owner);
    }).join();
    const int before_queued = owned_requests;
    owner.reset();
    Drain();
    Check(owned_requests == before_queued && owned_stops == 1,
          "queued worker quit cannot start confirmation after the loop ends");
    owner = std::make_shared<int>(0);
    nativeapi::detail::ApplicationQuitDispatch::RequestForLoop(61, owned_stop, owner);
    expired_owner = owner;
    std::thread([&] {
      nativeapi::detail::ApplicationQuitDispatch::CancelForLoop(expired_owner);
    }).join();
    owner.reset();
    owner = std::make_shared<int>(0);
    nativeapi::detail::ApplicationQuitDispatch::RequestForLoop(63, owned_stop, owner);
    Drain();
    Check(owned_request->IsPending(), "old queued cancellation cannot invalidate a new loop");
    owned_vote->Accept();
    Check(owned_stops == 2, "new loop survives a late cancellation of its predecessor");
    app.RemoveListener(owned_listener);
    Drain();
    nativeapi::SetMainThreadDispatcher(nullptr, nullptr);
#if defined(__APPLE__)
    NSApp.delegate = nil;
#if !__has_feature(objc_arc)
    [host release];
#endif
  }
#endif
  return failures ? 1 : 0;
}
