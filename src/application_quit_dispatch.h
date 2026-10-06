#pragma once

// Private binding/host protocol; not part of API_HEADERS or the umbrella.
#include <functional>
#include <memory>

namespace nativeapi {
class EventRequest;
}

namespace nativeapi::detail {

class ApplicationQuitDispatch {
 public:
  // A binding owns a non-blocking loop. Confirm using the same application
  // request as Quit(), then finish that loop rather than the host's process.
  static void RequestForLoop(int exit_code,
                             std::function<void(int)> stop_loop,
                             std::shared_ptr<void> loop_owner = nullptr);

  // Optional ownership fence for a cancellable binding loop. The binding
  // alone retains loop_owner; queued requests/completions retain it weakly.
  // Cancel only the matching attempt, even if a new loop has already started.
  static void CancelForLoop(std::weak_ptr<void> loop_owner);

  // Native UI producer and host confirmation continuation. These calls must
  // run on UI; the host retains the slot until its async decision completes.
  static std::shared_ptr<EventRequest> CurrentRequest();
  static void RequestFromNative(std::function<void(bool)> confirmation);
  static void HoldForHost(const std::shared_ptr<EventRequest>& request);
  static void ReleaseFromHost(const std::shared_ptr<EventRequest>& request);
  static void NotifyRequired();
  static int ExitCode();
};

}  // namespace nativeapi::detail
