#pragma once

// Private C++ support for generated asynchronous event subscriptions.
// The payload and user_data survive until the consumer acknowledges delivery.

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

#include "../foundation/event_request.h"
#include "../foundation/event_listener_dispatch.h"
#include "user_data.h"

namespace nativeapi::capi {

struct EventDeliveryContext {
  explicit EventDeliveryContext(std::shared_ptr<UserData> value) : holder(std::move(value)) {}
  std::shared_ptr<std::atomic<bool>> active = std::make_shared<std::atomic<bool>>(true);
  std::shared_ptr<UserData> holder;
};

// Only the registered native callback owns this marker. A queued delivery
// retains its context, never the registration, so unsubscription remains visible.
struct EventDeliveryRegistration {
  explicit EventDeliveryRegistration(std::shared_ptr<UserData> holder)
      : context(std::make_shared<EventDeliveryContext>(std::move(holder))) {}
  ~EventDeliveryRegistration() { context->active->store(false); }
  std::shared_ptr<EventDeliveryContext> context;
};

class EventDelivery {
 public:
  EventDelivery(std::shared_ptr<EventDeliveryContext> context,
                std::shared_ptr<void> payload, std::shared_ptr<EventDecision> vote)
      : context_(std::move(context)), payload_(std::move(payload)), vote_(std::move(vote)) {}
  ~EventDelivery() { Complete(false); }

  bool IsActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !completed_ && context_->active->load();
  }

  bool Complete(bool accept) {
    std::shared_ptr<void> payload;
    std::shared_ptr<EventDecision> vote;
    std::shared_ptr<EventDeliveryContext> context;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (completed_) return false;
      completed_ = true;
      payload = std::move(payload_);
      vote = std::move(vote_);
      context = std::move(context_);
    }
    // Free borrowed handles before a continuation can destroy their native
    // target. Neither payload destruction nor completion runs under mutex_.
    payload.reset();
    if (vote) {
      if (accept) vote->Accept();
      else vote->Cancel();
    }
    return true;
  }

 private:
  mutable std::mutex mutex_;
  bool completed_ = false;
  std::shared_ptr<EventDeliveryContext> context_;
  std::shared_ptr<void> payload_;
  std::shared_ptr<EventDecision> vote_;
};

}  // namespace nativeapi::capi
