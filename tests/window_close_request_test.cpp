// Exercises request ownership, dispatch fences and native-lifetime invalidation.
// No window or desktop required.
#include <iostream>
#include <thread>
#include <vector>
#include "../src/foundation/event_request.h"
#include "../src/window_close_dispatch.h"

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
}  // namespace
int main() {
  using namespace nativeapi;
  using detail::WindowCloseState;
  std::vector<std::function<void()>> jobs;
  bool valid = true, reject = false;
  auto state = WindowCloseState::Create(
      9001,
      [&](auto job) {
        if (reject)
          return false;
        jobs.push_back(std::move(job));
        return true;
      },
      [&] { return valid; });
  auto drain = [&] {
    auto work = std::move(jobs);
    jobs.clear();
    for (auto& job : work)
      job();
  };
  int actions = 0, events = 0, before_broadcast = 0;
  std::shared_ptr<EventRequest> request;
  std::shared_ptr<EventDecision> a, b;
  auto first = state->Subscribe([&](const WindowEvent& event) {
    auto* close = dynamic_cast<const WindowCloseRequestedEvent*>(&event);
    if (!close)
      return;
    ++events;
    before_broadcast = actions;
    request = close->GetRequest();
    a = request->Defer();
  });
  auto second = state->Subscribe([&](const WindowEvent& event) {
    auto* close = dynamic_cast<const WindowCloseRequestedEvent*>(&event);
    Check(close && close->GetRequest() == request, "aliases receive the identical request");
    Check(actions == before_broadcast, "host action waits for the full broadcast");
    b = close->GetRequest()->Defer();
  });
  Check(state->Request([&] { ++actions; }), "request accepted");
  Check(state->Request([&] { actions += 100; }) && events == 1, "repeated request coalesces");
  a->Accept();
  drain();
  Check(actions == 0 && request->IsPending(), "all aliases must approve");
  std::thread worker([&] { b->Accept(); });
  worker.join();
  Check(actions == 0 && state->IsPending(), "worker approval only queues the UI action");
  Check(state->Request([&] { actions += 100; }) && events == 1,
        "approved queued request still coalesces");
  drain();
  Check(actions == 1 && !state->IsPending(), "original action executes once");
  state->Request([&] { ++actions; });
  a->Accept();
  b->Cancel();
  drain();
  Check(actions == 1 && !state->IsPending(), "one veto stops every alias");
  state->Request([&] { ++actions; });
  auto old_a = a, old_b = b;
  second.reset();
  first.reset();
  int required = 0;
  auto force = state->Subscribe([&](const WindowEvent& event) {
    auto* close = dynamic_cast<const WindowCloseRequestedEvent*>(&event);
    ++required;
    Check(!close->GetRequest()->IsCancelable() && !close->GetRequest()->Defer(),
          "required native close cannot acquire a veto");
    close->GetRequest()->Cancel();
  });
  state->NotifyRequired();
  old_a->Accept();
  old_b->Accept();
  drain();
  Check(required == 1 && actions == 1 && !state->IsPending(),
        "required close invalidates late old approvals");
  force.reset();
  reject = true;
  Check(state->Request([&] { ++actions; }), "request survives a failed UI post");
  reject = false;
  Check(state->Request([&] { ++actions; }), "later request recovers after failed post");
  drain();
  Check(actions == 2, "only recovery action runs");
  state->Request([&] { ++actions; });
  valid = false;
  drain();
  Check(actions == 2, "native identity change fences a queued action");
  valid = true;
  state->Invalidate();
  Check(!state->Request([&] { ++actions; }), "destroyed native lifetime never revives");
  return failures ? 1 : 0;
}
