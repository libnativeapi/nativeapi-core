#pragma once

// Private binding protocol; intentionally absent from API_HEADERS and umbrella.
#include "event_emitter.h"

namespace nativeapi::detail {
struct EventListenerDispatch {
  template <typename EventType, typename BaseEventType>
  static size_t AddListener(EventEmitter<BaseEventType>& emitter,
                            std::function<void(const EventType&)> callback,
                            std::shared_ptr<std::atomic<bool>> active) {
    return emitter.template AddCallbackListener<EventType>(std::move(callback), std::move(active));
  }
};
}  // namespace nativeapi::detail
