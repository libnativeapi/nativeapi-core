#include <atomic>

#include "application.h"
#include "application_quit_dispatch.h"
#include "foundation/dispatcher.h"
#include "foundation/event_request_dispatch.h"

namespace nativeapi {

struct detail::ApplicationQuitState {
  std::shared_ptr<EventRequest> request;
  std::atomic<bool> dispatch_failed{false};
  bool retain_for_host = false;
  bool has_loop_owner = false;
  std::weak_ptr<void> loop_owner;
};

Application& Application::GetInstance() {
  static Application instance;
  return instance;
}

void Application::RequestQuit(int exit_code,
                              std::function<void(int)> stop_loop,
                              std::shared_ptr<void> loop_owner,
                              std::function<void(bool)> native_confirmation) try {
  const bool has_loop_owner = !!loop_owner;
  auto dispatch = CreateGuardedCallback<>(
      std::function<void()>([this, exit_code, stop_loop = std::move(stop_loop), has_loop_owner,
                             weak_owner = std::weak_ptr<void>(loop_owner),
                             native_confirmation = std::move(native_confirmation)] {
        try {
          if (has_loop_owner && weak_owner.expired())
            return;
          exit_code_ = exit_code;
          if (quit_state_) {
            if (!quit_state_->dispatch_failed.load() &&
                !(quit_state_->has_loop_owner && quit_state_->loop_owner.expired()))
              return;
            InvalidateQuitRequest();
          }

          auto state = std::make_shared<detail::ApplicationQuitState>();
          state->retain_for_host = !!native_confirmation;
          state->has_loop_owner = has_loop_owner;
          state->loop_owner = weak_owner;
          std::weak_ptr<detail::ApplicationQuitState> weak_state = state;

          auto finish = CreateGuardedCallback<bool>(std::function<void(bool)>(
              [this, stop_loop, weak_state, native_confirmation](bool accepted) {
                // Keep the request installed until its completion reaches the UI thread.
                // Reentrant and worker requests cannot start a second confirmation.
                auto state = weak_state.lock();
                if (!state || quit_state_ != state)
                  return;
                if (!accepted || (state->has_loop_owner && state->loop_owner.expired())) {
                  quit_state_.reset();
                  if (native_confirmation)
                    native_confirmation(false);
                  return;
                }
                // Keep this attempt installed through its action too. An exiting
                // listener can call Quit(), but must not start recursive termination.
                const int code = exit_code_;
                try {
                  if (native_confirmation) {
                    native_confirmation(true);
                  } else if (stop_loop) {
                    // A notification failure cannot undo an already approved quit.
                    try {
                      Emit<ApplicationExitingEvent>(code);
                    } catch (...) {
                    }
                    stop_loop(code);
                  } else {
                    PerformQuit(code);
                  }
                } catch (...) {
                }
                if (quit_state_ == state && !state->retain_for_host)
                  quit_state_.reset();
              }));
          std::shared_ptr<EventRequest> request;
          try {
            request =
                detail::EventRequestDispatch::Create(true, [finish, weak_state](bool accepted) {
                  auto complete = [finish, accepted] { finish(accepted); };
                  if (IsMainThread())
                    complete();
                  else {
                    try {
                      if (RunOnMainThread(std::move(complete)))
                        return;
                    } catch (...) {
                    }
                    if (auto state = weak_state.lock())
                      state->dispatch_failed = true;
                  }
                });
          } catch (...) {
            // Allocation failure must not silently approve termination.
            return;
          }
          state->request = request;
          quit_state_ = state;
          try {
            Emit<ApplicationQuitRequestedEvent>(request);
          } catch (...) {
            request->Cancel();
          }
          detail::EventRequestDispatch::Finish(request);
        } catch (...) {
          // The UI scheduler invokes this after a worker Quit() has returned.
          // Failure here must also leave the application running.
        }
      }));
  if (IsMainThread())
    dispatch();
  else
    (void)RunOnMainThread(std::move(dispatch));
} catch (...) {
  // Public Quit() never throws, including failures to allocate/schedule work.
}

void Application::InvalidateQuitRequest() {
  if (quit_state_)
    detail::EventRequestDispatch::Invalidate(quit_state_->request);
  quit_state_.reset();
}

void detail::ApplicationQuitDispatch::RequestForLoop(int exit_code,
                                                     std::function<void(int)> stop_loop,
                                                     std::shared_ptr<void> loop_owner) {
  if (stop_loop)
    Application::GetInstance().RequestQuit(exit_code, std::move(stop_loop), std::move(loop_owner));
}

void detail::ApplicationQuitDispatch::CancelForLoop(std::weak_ptr<void> loop_owner) try {
  auto& app = Application::GetInstance();
  auto cancel = app.CreateGuardedCallback<>(std::function<void()>([&app, loop_owner] {
    const auto& state = app.quit_state_;
    if (state && state->has_loop_owner && !state->loop_owner.owner_before(loop_owner) &&
        !loop_owner.owner_before(state->loop_owner))
      app.InvalidateQuitRequest();
  }));
  if (IsMainThread())
    cancel();
  else
    (void)RunOnMainThread(std::move(cancel));
} catch (...) {
  // Expiring the binding's owner also fences later requests and completions,
  // including when scheduling this eager invalidation fails.
}

std::shared_ptr<EventRequest> detail::ApplicationQuitDispatch::CurrentRequest() {
  auto& app = Application::GetInstance();
  if (!IsMainThread())
    return nullptr;
  const auto& state = app.quit_state_;
  if (state &&
      (state->dispatch_failed.load() || (state->has_loop_owner && state->loop_owner.expired())))
    app.InvalidateQuitRequest();
  return app.quit_state_ ? app.quit_state_->request : nullptr;
}
void detail::ApplicationQuitDispatch::RequestFromNative(std::function<void(bool)> confirmation) {
  if (confirmation && IsMainThread())
    Application::GetInstance().RequestQuit(0, nullptr, nullptr, std::move(confirmation));
}
void detail::ApplicationQuitDispatch::HoldForHost(const std::shared_ptr<EventRequest>& request) {
  auto& app = Application::GetInstance();
  if (IsMainThread() && app.quit_state_ && app.quit_state_->request == request)
    app.quit_state_->retain_for_host = true;
}
void detail::ApplicationQuitDispatch::ReleaseFromHost(
    const std::shared_ptr<EventRequest>& request) {
  auto& app = Application::GetInstance();
  if (IsMainThread() && app.quit_state_ && app.quit_state_->request == request)
    app.InvalidateQuitRequest();
}
int detail::ApplicationQuitDispatch::ExitCode() {
  return Application::GetInstance().exit_code_;
}
void detail::ApplicationQuitDispatch::NotifyRequired() try {
  auto& app = Application::GetInstance();
  if (!IsMainThread())
    return;
  app.InvalidateQuitRequest();
  auto request = EventRequestDispatch::Create(false, nullptr);
  try {
    app.Emit<ApplicationQuitRequestedEvent>(request);
  } catch (...) {
  }
  EventRequestDispatch::Finish(request);
} catch (...) {
  // Mandatory native termination cannot be vetoed by a failed notification.
}

int RunApp(std::shared_ptr<Window> window) {
  return Application::GetInstance().Run(window);
}

}  // namespace nativeapi
