#include "../src/capi/event_request_c.h"
#include "../src/foundation/event_request_dispatch.h"
#include "../src/foundation/handle_table.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <thread>
#include <vector>

using nativeapi::detail::EventRequestDispatch;
static std::atomic<bool> fail_next_allocation{false};
void* operator new(std::size_t size) {
  if (fail_next_allocation.exchange(false))
    throw std::bad_alloc();
  if (void* result = std::malloc(size ? size : 1))
    return result;
  throw std::bad_alloc();
}
void operator delete(void* value) noexcept {
  std::free(value);
}
void operator delete(void* value, std::size_t) noexcept {
  std::free(value);
}
static int failures;
static void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}

int main() {
  {
    int calls = 0;
    bool accepted = true;
    auto request = EventRequestDispatch::Create(true, [&](bool result) {
      ++calls;
      accepted = result;
    });
    fail_next_allocation = true;
    auto vote = request->Defer();
    Check(!vote && !fail_next_allocation && request->IsCancelled() && calls == 0,
          "failed confirmation allocation cancels without throwing or completing inside dispatch");
    EventRequestDispatch::Finish(request);
    Check(calls == 1 && !accepted, "failed confirmation cannot silently approve the action");
  }
  {
    int calls = 0;
    bool accepted = false;
    auto request = EventRequestDispatch::Create(true, [&](bool result) {
      ++calls;
      accepted = result;
    });
    Check(request->IsCancelable() && request->IsPending() && !request->IsCancelled(),
          "new request is shared live pending state");
    EventRequestDispatch::Finish(request);
    EventRequestDispatch::Finish(request);
    Check(calls == 1 && accepted && !request->IsPending(),
          "no veto or deferral accepts exactly once after dispatch");
    Check(!request->Cancel() && !request->Defer(),
          "completed request rejects late cancellation and deferral");
  }
  {
    int calls = 0;
    bool accepted = true;
    auto request = EventRequestDispatch::Create(true, [&](bool result) {
      ++calls;
      accepted = result;
    });
    auto copy = request;
    Check(request->Cancel() && copy->IsCancelled() && !copy->Cancel() && !copy->Defer(),
          "one listener's veto is monotonic across copies");
    Check(calls == 0, "a synchronous veto does not complete inside event delivery");
    EventRequestDispatch::Finish(request);
    Check(calls == 1 && !accepted && !copy->IsPending(),
          "veto finishes after delivery without performing the action");
  }
  {
    bool accepted = false;
    auto request = EventRequestDispatch::Create(false, [&](bool result) { accepted = result; });
    Check(!request->IsCancelable() && !request->Cancel() && !request->Defer(),
          "required system request cannot be vetoed or deferred");
    EventRequestDispatch::Finish(request);
    Check(accepted && !request->IsCancelled() && !request->IsPending(),
          "required system request proceeds after notification");
  }
  {
    int calls = 0;
    bool accepted = false;
    auto request = EventRequestDispatch::Create(true, [&](bool result) {
      ++calls;
      accepted = result;
    });
    auto vote = request->Defer();
    EventRequestDispatch::Finish(request);
    Check(vote && vote->IsPending() && request->IsPending() && calls == 0,
          "deferred vote outlives synchronous delivery");
    Check(vote->Accept() && accepted && calls == 1 && !request->IsPending() && !vote->IsPending(),
          "deferred acceptance completes the request");
    Check(!vote->Accept() && !vote->Cancel() && !request->Cancel(),
          "duplicate or contrary late responses fail safely");
  }
  {
    int calls = 0;
    auto request = EventRequestDispatch::Create(true, [&](bool result) {
      ++calls;
      Check(!result, "deferred cancellation is delivered");
    });
    auto vote = request->Defer();
    Check(vote->Cancel() && calls == 0, "vote may resolve before event delivery ends");
    EventRequestDispatch::Finish(request);
    Check(calls == 1 && request->IsCancelled(),
          "early deferred veto completes only after dispatch");
  }
  {
    int calls = 0;
    bool accepted = true;
    auto request = EventRequestDispatch::Create(true, [&](bool result) {
      ++calls;
      accepted = result;
    });
    auto first = request->Defer(), second = request->Defer(), third = request->Defer();
    EventRequestDispatch::Finish(request);
    Check(first->Accept() && calls == 0 && request->IsPending(),
          "one acceptance does not override outstanding votes");
    Check(second->Cancel() && calls == 1 && !accepted && !third->IsPending(),
          "one veto ends the request without waiting for other votes");
    Check(!third->Accept() && !third->Cancel(),
          "other pending votes cannot revive a cancelled request");
  }
  {
    int calls = 0;
    auto request = EventRequestDispatch::Create(true, [&](bool result) {
      ++calls;
      Check(result, "every deferred listener approved");
    });
    auto first = request->Defer(), second = request->Defer();
    EventRequestDispatch::Finish(request);
    first->Accept();
    second->Accept();
    Check(calls == 1 && !request->IsPending(), "all approvals complete only once");
  }
  {
    bool accepted = true;
    auto request = EventRequestDispatch::Create(true, [&](bool result) { accepted = result; });
    auto vote = request->Defer();
    auto retained = vote;
    EventRequestDispatch::Finish(request);
    vote.reset();
    Check(request->IsPending(), "dropping one shared decision reference does not answer it");
    retained.reset();
    Check(!request->IsPending() && request->IsCancelled() && !accepted,
          "dropping an unanswered last reference cancels instead of approving");
  }
  {
    int calls = 0;
    auto request = EventRequestDispatch::Create(true, [&](bool) { ++calls; });
    auto vote = request->Defer();
    EventRequestDispatch::Finish(request);
    EventRequestDispatch::Invalidate(request);
    Check(!request->IsPending() && !vote->IsPending() && !vote->Accept() && !request->Cancel(),
          "destroyed native target invalidates outstanding votes");
    vote.reset();
    EventRequestDispatch::Finish(request);
    Check(calls == 0, "invalidated continuation never acts on a replacement target");
  }
  {
    bool accepted = false;
    auto request = EventRequestDispatch::Create(true, [&](bool result) { accepted = result; });
    auto vote = request->Defer();
    EventRequestDispatch::Finish(request);
    request.reset();
    Check(vote->Accept() && accepted,
          "owned decision keeps state alive after event and request wrapper disappear");
  }
  {
    int calls = 0;
    std::shared_ptr<nativeapi::EventRequest> request;
    request = EventRequestDispatch::Create(true, [&](bool result) {
      ++calls;
      Check(result && !request->IsPending() && !request->Cancel() && !request->Defer(),
            "continuation executes outside the state lock and can reenter");
    });
    EventRequestDispatch::Finish(request);
    Check(calls == 1, "reentrant continuation finishes once");
  }
  {
    bool accepted = false;
    auto request = EventRequestDispatch::Create(true, [&](bool result) { accepted = result; });
    auto delivery = request->Defer();
    EventRequestDispatch::Finish(request);
    auto asynchronous_confirmation = request->Defer();
    delivery->Accept();
    Check(request->IsPending() && !accepted,
          "async delivery can acquire an explicit vote before releasing its bridge vote");
    asynchronous_confirmation->Accept();
    Check(accepted && !request->IsPending(),
          "asynchronous user confirmation completes after delivery acknowledgment");
  }
  {
    std::atomic<int> calls{0};
    std::atomic<bool> accepted{true};
    auto request = EventRequestDispatch::Create(true, [&](bool result) {
      ++calls;
      accepted = result;
    });
    std::vector<std::shared_ptr<nativeapi::EventDecision>> votes;
    for (int i = 0; i < 32; ++i)
      votes.push_back(request->Defer());
    EventRequestDispatch::Finish(request);
    std::vector<std::thread> threads;
    for (int i = 0; i < 32; ++i)
      threads.emplace_back([&, i] {
        if (i == 17)
          votes[i]->Cancel();
        else
          votes[i]->Accept();
      });
    for (auto& thread : threads)
      thread.join();
    Check(calls == 1 && !accepted && request->IsCancelled() && !request->IsPending(),
          "concurrent listener responses finish once and preserve any veto");
  }
  {
    bool accepted = false;
    auto request = EventRequestDispatch::Create(true, [&](bool result) { accepted = result; });
    auto handle = nativeapi::HandleTable::GetInstance().Insert(request);
    auto decision = native_event_request_defer(handle);
    EventRequestDispatch::Finish(request);
    Check(native_event_request_is_cancelable(handle) && native_event_request_is_pending(handle) &&
              native_event_decision_is_pending(decision),
          "C ABI exports live request and owned deferred decision handles");
    Check(!native_event_request_cancel(decision) && !native_event_decision_accept(handle),
          "type-confused request and decision handles are rejected");
    Check(native_event_decision_accept(decision) && accepted &&
              !native_event_request_is_pending(handle),
          "C ABI acceptance resolves the same native request state");
    native_event_decision_free(decision);
    native_event_request_free(handle);
    Check(!native_event_decision_accept(decision) && !native_event_request_cancel(handle),
          "released and stale handles fail safely");
  }
  {
    bool accepted = true;
    auto request = EventRequestDispatch::Create(true, [&](bool result) { accepted = result; });
    auto handle = nativeapi::HandleTable::GetInstance().Insert(request);
    auto decision = native_event_request_defer(handle);
    EventRequestDispatch::Finish(request);
    native_event_decision_free(decision);
    Check(native_event_request_is_cancelled(handle) && !accepted,
          "C ABI dropping unanswered owned decision also cancels");
    native_event_request_free(handle);
  }
  Check(!native_event_request_is_pending(0) && !native_event_request_cancel(0) &&
            !native_event_request_defer(0) && !native_event_decision_accept(0),
        "invalid C ABI handles never create or resolve requests");
  return failures ? 1 : 0;
}
