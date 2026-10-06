#pragma once

#include <memory>

namespace nativeapi {

namespace detail {
class EventRequestDispatch;
}

class EventRequest;

/**
 * @brief One deferred vote on a cancellable event request.
 *
 * Obtain a decision with EventRequest::Defer() during an event callback, keep
 * it while awaiting a user response, then call Accept() or Cancel(). A decision
 * can be resolved once, from any thread. Destroying an unresolved decision
 * cancels the request; it never silently approves an unanswered confirmation.
 * One cancellation wins over every other listener's acceptance.
 */
class EventDecision {
 public:
  ~EventDecision();

  EventDecision(const EventDecision&) = delete;
  EventDecision& operator=(const EventDecision&) = delete;
  EventDecision(EventDecision&&) = delete;
  EventDecision& operator=(EventDecision&&) = delete;

  /** @brief Accept this vote; false if the decision or request already ended. */
  bool Accept();
  /** @brief Cancel this vote; false if the decision or request already ended. */
  bool Cancel();
  /** @brief Whether this vote still awaits a response on a pending request. */
  bool IsPending() const;

 private:
  friend class EventRequest;
  EventDecision();
  class Impl;
  std::unique_ptr<Impl> pimpl_;
};

/**
 * @brief Shared decision state for one cancellable event.
 *
 * Events carry a shared request rather than copying a cancellation flag. Every
 * listener observes the same live state, including across language bindings.
 * Call Cancel() during the callback to veto the action. For asynchronous work,
 * call Defer() before returning and resolve the returned EventDecision later.
 * A listener that does neither accepts the action when its callback ends.
 *
 * The action is allowed only after event delivery and every deferred vote end
 * without cancellation. A single veto cancels it. No method blocks waiting for
 * another listener, and all methods are thread-safe. Native continuations are
 * responsible for returning to their platform's UI thread.
 *
 * Required system requests may be non-cancellable: IsCancelable() is false,
 * Cancel() fails and Defer() returns nullptr. Such requests cannot be delayed
 * by a listener. Completed or invalidated requests safely reject late replies.
 * Requests are created by the event's producer, never by consumers.
 */
class EventRequest {
 public:
  ~EventRequest();

  EventRequest(const EventRequest&) = delete;
  EventRequest& operator=(const EventRequest&) = delete;
  EventRequest(EventRequest&&) = delete;
  EventRequest& operator=(EventRequest&&) = delete;

  /** @brief Whether the producer permits cancellation or deferred replies. */
  bool IsCancelable() const;
  /** @brief Whether any listener has vetoed this request. */
  bool IsCancelled() const;
  /** @brief Whether event delivery or a deferred vote is still outstanding. */
  bool IsPending() const;
  /** @brief Veto a pending request; false if required, cancelled or ended. */
  bool Cancel();
  /**
   * @brief Keep the request pending beyond the current callback.
   * @return An owned decision, or nullptr if required, cancelled or ended,
   *         or if allocation fails (which cancels the request).
   *
   * Keep the returned object until explicitly accepting or cancelling it.
   * Dropping the last reference before responding cancels the request.
   */
  std::shared_ptr<EventDecision> Defer();

 private:
  friend class detail::EventRequestDispatch;
  EventRequest();
  class Impl;
  std::unique_ptr<Impl> pimpl_;
};

}  // namespace nativeapi
