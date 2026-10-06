#pragma once

// Private producer protocol. Not part of API_HEADERS or the public umbrella.
#include <functional>
#include <memory>
#include "event_request.h"

namespace nativeapi::detail {

class EventRequestDispatch {
 public:
  static std::shared_ptr<EventRequest> Create(bool cancelable,
                                              std::function<void(bool)> completion);
  // Close synchronous delivery. Deferred votes may still hold the request.
  static void Finish(const std::shared_ptr<EventRequest>& request);
  // The native target disappeared or a required request superseded this one.
  // Stop the old continuation; late responses cannot act on another target.
  static void Invalidate(const std::shared_ptr<EventRequest>& request);
};

}  // namespace nativeapi::detail
