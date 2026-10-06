#include "event_request.h"
#include "event_request_dispatch.h"

#include <functional>
#include <mutex>
#include <utility>

namespace nativeapi {
namespace {

struct RequestState {
  mutable std::mutex mutex;
  bool cancelable = false;
  bool cancelled = false;
  bool dispatching = true;
  bool finished = false;
  size_t pending_votes = 0;
  std::function<void(bool)> completion;
};

struct Completion {
  std::function<void(bool)> callback;
  bool accepted = false;
  void Run() {
    // A continuation is producer code; keep destructor-driven cancellation
    // noexcept even if that code unexpectedly throws.
    if (callback) {
      try {
        callback(accepted);
      } catch (...) {
      }
    }
  }
};

// Called with state.mutex held. Move the continuation out so its execution and
// captured-object destruction both occur outside the lock.
Completion TakeCompletion(RequestState& state) {
  if (state.finished || state.dispatching || (!state.cancelled && state.pending_votes != 0))
    return {};
  state.finished = true;
  return {std::move(state.completion), !state.cancelled};
}

}  // namespace

class EventRequest::Impl {
 public:
  std::shared_ptr<RequestState> state = std::make_shared<RequestState>();
};

class EventDecision::Impl {
 public:
  std::shared_ptr<RequestState> state;
  bool pending = true;

  bool Resolve(bool accept) {
    Completion completion;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (!pending)
        return false;
      pending = false;
      if (state->finished)
        return false;
      --state->pending_votes;
      if (!accept)
        state->cancelled = true;
      completion = TakeCompletion(*state);
    }
    completion.Run();
    return true;
  }
};

EventDecision::EventDecision() : pimpl_(std::make_unique<Impl>()) {}
EventDecision::~EventDecision() {
  if (pimpl_->state)
    pimpl_->Resolve(false);
}
bool EventDecision::Accept() {
  return pimpl_->Resolve(true);
}
bool EventDecision::Cancel() {
  return pimpl_->Resolve(false);
}
bool EventDecision::IsPending() const {
  std::lock_guard<std::mutex> lock(pimpl_->state->mutex);
  return pimpl_->pending && !pimpl_->state->finished;
}

EventRequest::EventRequest() : pimpl_(std::make_unique<Impl>()) {}
EventRequest::~EventRequest() = default;
bool EventRequest::IsCancelable() const {
  std::lock_guard<std::mutex> lock(pimpl_->state->mutex);
  return pimpl_->state->cancelable;
}
bool EventRequest::IsCancelled() const {
  std::lock_guard<std::mutex> lock(pimpl_->state->mutex);
  return pimpl_->state->cancelled;
}
bool EventRequest::IsPending() const {
  std::lock_guard<std::mutex> lock(pimpl_->state->mutex);
  return !pimpl_->state->finished;
}
bool EventRequest::Cancel() {
  Completion completion;
  {
    std::lock_guard<std::mutex> lock(pimpl_->state->mutex);
    auto& state = *pimpl_->state;
    if (!state.cancelable || state.cancelled || state.finished)
      return false;
    state.cancelled = true;
    completion = TakeCompletion(state);
  }
  completion.Run();
  return true;
}
std::shared_ptr<EventDecision> EventRequest::Defer() {
  Completion completion;
  std::shared_ptr<EventDecision> decision;
  {
    std::lock_guard<std::mutex> lock(pimpl_->state->mutex);
    const auto& state = pimpl_->state;
    if (!state->cancelable || state->cancelled || state->finished)
      return nullptr;
    try {
      decision = std::shared_ptr<EventDecision>(new EventDecision());
      decision->pimpl_->state = state;
      ++state->pending_votes;
    } catch (...) {
      // Failure to allocate a confirmation cannot silently approve it.
      state->cancelled = true;
      completion = TakeCompletion(*state);
    }
  }
  completion.Run();
  return decision;
}

std::shared_ptr<EventRequest> detail::EventRequestDispatch::Create(
    bool cancelable,
    std::function<void(bool)> completion) {
  auto request = std::shared_ptr<EventRequest>(new EventRequest());
  request->pimpl_->state->cancelable = cancelable;
  request->pimpl_->state->completion = std::move(completion);
  return request;
}

void detail::EventRequestDispatch::Finish(const std::shared_ptr<EventRequest>& request) {
  if (!request)
    return;
  Completion completion;
  {
    std::lock_guard<std::mutex> lock(request->pimpl_->state->mutex);
    request->pimpl_->state->dispatching = false;
    completion = TakeCompletion(*request->pimpl_->state);
  }
  completion.Run();
}

void detail::EventRequestDispatch::Invalidate(const std::shared_ptr<EventRequest>& request) {
  if (!request)
    return;
  std::function<void(bool)> discarded;
  {
    std::lock_guard<std::mutex> lock(request->pimpl_->state->mutex);
    auto& state = *request->pimpl_->state;
    state.finished = true;
    discarded = std::move(state.completion);
  }
}

}  // namespace nativeapi
