#include "window_close_dispatch.h"
#include <algorithm>
#include <exception>
#include <unordered_map>
#include "foundation/event_request_dispatch.h"
#include "window_manager.h"

namespace nativeapi::detail {
struct WindowEventContext {
  std::shared_ptr<std::atomic<bool>> active;
  std::function<void(const WindowEvent&)> sink;
  std::function<bool()> wants_close;
};
WindowEventRegistration::~WindowEventRegistration() {
  *context->active = false;
}
struct WindowCloseState::Attempt {
  std::shared_ptr<EventRequest> request;
  std::function<void()> action;
  std::atomic<bool> dispatch_failed{false};
};
namespace {
struct States {
  std::mutex mutex;
  std::unordered_map<WindowId, std::weak_ptr<WindowCloseState>> items;
};
States& Registry() {
  // Native platform teardown may follow C++ singleton teardown.
  static auto* states = new States;
  return *states;
}
}  // namespace
WindowCloseState::WindowCloseState(WindowId id, Post post, std::function<bool()> valid)
    : id_(id), post_(std::move(post)), valid_(std::move(valid)) {}
std::shared_ptr<WindowCloseState> WindowCloseState::Create(WindowId id,
                                                           Post post,
                                                           std::function<bool()> valid) {
  auto state = std::shared_ptr<WindowCloseState>(
      new WindowCloseState(id, std::move(post), std::move(valid)));
  auto& registry = Registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  registry.items[id] = state;
  return state;
}
WindowCloseState::~WindowCloseState() {
  Invalidate();
  auto& registry = Registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  auto entry = registry.items.find(id_);
  if (entry != registry.items.end() && entry->second.expired())
    registry.items.erase(entry);
}
std::shared_ptr<WindowEventRegistration> WindowCloseState::Subscribe(
    std::function<void(const WindowEvent&)> sink,
    std::shared_ptr<std::atomic<bool>> active,
    std::function<bool()> wants_close) {
  auto context = std::make_shared<WindowEventContext>();
  context->sink = std::move(sink);
  context->wants_close = std::move(wants_close);
  context->active = active ? std::move(active) : std::make_shared<std::atomic<bool>>(true);
  auto registration = std::make_shared<WindowEventRegistration>();
  registration->context = context;
  std::lock_guard<std::mutex> lock(mutex_);
  observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                  [](const auto& entry) {
                                    auto live = entry.lock();
                                    return !live || !live->active->load();
                                  }),
                   observers_.end());
  observers_.push_back(context);
  return registration;
}
bool WindowCloseState::HasObservers() const {
  std::vector<std::shared_ptr<WindowEventContext>> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : observers_)
      if (auto context = entry.lock())
        targets.push_back(std::move(context));
  }
  for (auto& context : targets)
    if (context->active->load() && (!context->wants_close || context->wants_close()))
      return true;
  return false;
}
bool WindowCloseState::IsAlive() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return alive_;
}
bool WindowCloseState::IsPending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !!attempt_;
}
void WindowCloseState::EmitObserved(const WindowEvent& event) {
  std::vector<std::shared_ptr<WindowEventContext>> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : observers_)
      if (auto context = entry.lock())
        targets.push_back(std::move(context));
  }
  std::exception_ptr failure;
  for (auto& context : targets) {
    if (context->active->load()) {
      try {
        context->sink(event);
      } catch (...) {
        if (!failure)
          failure = std::current_exception();
      }
    }
  }
  if (failure)
    std::rethrow_exception(failure);
}
bool WindowCloseState::Request(std::function<void()> action) try {
  if (!IsAlive() || !valid_())
    return false;
  if (IsPending()) {
    std::shared_ptr<Attempt> previous;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      previous = attempt_;
    }
    if (previous && !previous->dispatch_failed.load())
      return true;
    Invalidate(false);
  }
  auto attempt = std::make_shared<Attempt>();
  attempt->action = std::move(action);
  std::weak_ptr<WindowCloseState> weak = shared_from_this();
  std::weak_ptr<Attempt> weak_attempt = attempt;
  attempt->request = EventRequestDispatch::Create(true, [weak, weak_attempt](bool accepted) {
    auto state = weak.lock();
    auto pending = weak_attempt.lock();
    if (!state || !pending)
      return;
    try {
      if (state->post_([weak, weak_attempt, accepted] {
            if (auto state = weak.lock())
              if (auto attempt = weak_attempt.lock())
                state->Complete(attempt, accepted);
          }))
        return;
    } catch (...) {
    }
    pending->dispatch_failed = true;
  });
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!alive_)
      return false;
    attempt_ = attempt;
  }
  try {
    EmitObserved(WindowCloseRequestedEvent(id_, attempt->request));
  } catch (...) {
    attempt->request->Cancel();
  }
  EventRequestDispatch::Finish(attempt->request);
  return true;
} catch (...) {
  return false;
}
bool WindowCloseState::DispatchTask(std::function<void()> task) try {
  return post_(std::move(task));
} catch (...) {
  return false;
}
void WindowCloseState::Complete(const std::shared_ptr<Attempt>& attempt, bool accepted) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!alive_ || attempt_ != attempt)
      return;
  }
  // Keep the slot installed through the host action and any reentrant events.
  try {
    if (accepted && valid_())
      attempt->action();
  } catch (...) {
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (attempt_ == attempt)
      attempt_.reset();
  }
}
void WindowCloseState::Invalidate(bool native_destroyed) {
  std::shared_ptr<Attempt> attempt;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (native_destroyed)
      alive_ = false;
    attempt.swap(attempt_);
  }
  if (attempt)
    EventRequestDispatch::Invalidate(attempt->request);
}
void WindowCloseState::NotifyRequired() {
  Invalidate(false);
  try {
    if (!HasObservers())
      return;
    auto request = EventRequestDispatch::Create(false, nullptr);
    try {
      EmitObserved(WindowCloseRequestedEvent(id_, request));
    } catch (...) {
    }
    EventRequestDispatch::Finish(request);
  } catch (...) {
  }  // Never prevent a mandatory native action.
}
void EmitObservedWindowEvent(const WindowEvent& event) {
  std::shared_ptr<WindowCloseState> state;
  {
    auto& registry = Registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto entry = registry.items.find(event.GetWindowId());
    if (entry != registry.items.end())
      state = entry->second.lock();
  }
  if (state) {
    try {
      state->EmitObserved(event);
    } catch (...) {
    }
  }
}
struct WindowEventSubscription {
  std::shared_ptr<std::atomic<bool>> active = std::make_shared<std::atomic<bool>>(true);
  std::shared_ptr<WindowCloseState> state;
  std::shared_ptr<WindowEventRegistration> registration;
  std::function<void()> stop_monitor;
  void Monitor(WindowManager& manager) {
    const auto id = manager.AddListener<WindowEvent>([](const auto&) {});
    try {
      stop_monitor = manager.CreateGuardedCallback<>(
          std::function<void()>([&manager, id] { manager.RemoveListener(id); }));
    } catch (...) {
      manager.RemoveListener(id);
      throw;
    }
  }
  ~WindowEventSubscription() {
    *active = false;
    registration.reset();
    if (stop_monitor) {
      try {
        if (state && state->DispatchTask(stop_monitor))
          return;
        if (IsMainThread())
          stop_monitor();
        else
          (void)RunOnMainThread(std::move(stop_monitor));
      } catch (...) {
      }
    }
  }
};
}  // namespace nativeapi::detail

namespace nativeapi {
bool Window::IsCloseSupported() {
  return detail::IsNativeWindowCloseSupported();
}
bool Window::Close() try {
  if (!IsCloseSupported())
    return false;
  auto result = std::make_shared<std::atomic<bool>>(true);
  auto close = CreateGuardedCallback<>(std::function<void()>(
      [this, result] { *result = detail::RequestNativeWindowClose(GetNativeObject(), GetId()); }));
  return DispatchWindowTask(std::move(close)) && result->load();
} catch (...) {
  return false;
}
void Window::StartEventListening() {
  auto subscription = std::make_shared<detail::WindowEventSubscription>();
  std::atomic_store(&event_subscription_, subscription);
  auto setup = CreateGuardedCallback<>(std::function<void()>([this, subscription] {
    if (!subscription->active->load())
      return;
    auto state = detail::GetNativeWindowCloseState(GetNativeObject(), GetId());
    if (!state)
      return;
    subscription->state = state;
    auto interest = CreateGuardedCallback<bool&>(std::function<void(bool&)>([this](bool& value) {
      value = GetListenerCount<WindowEvent>() != 0 ||
              GetListenerCount<WindowCloseRequestedEvent>() != 0;
    }));
    subscription->registration = state->Subscribe(
        CreateGuardedCallback<const WindowEvent&>(std::function<void(const WindowEvent&)>(
            [this](const WindowEvent& event) { Emit(event); })),
        subscription->active, [interest] {
          bool value = false;
          interest(value);
          return value;
        });
    // Reuse platform notification monitors for this object's observations.
    subscription->Monitor(WindowManager::GetInstance());
  }));
  if (!DispatchWindowTask(std::move(setup)))
    StopEventListening();
}
void Window::StopEventListening() {
  auto subscription = std::atomic_exchange(&event_subscription_,
                                           std::shared_ptr<detail::WindowEventSubscription>{});
  if (subscription)
    *subscription->active = false;
}
}  // namespace nativeapi
