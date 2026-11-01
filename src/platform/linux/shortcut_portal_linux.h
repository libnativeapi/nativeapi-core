#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "../../shortcut.h"

namespace nativeapi::detail {

// Private transport. All D-Bus work lives in its own GLib context. Each action
// owns a connection/session, so cancellation also revokes late server resources.
class ShortcutPortalLinux {
 public:
  enum class Signal { Activated, Released, Closed, BindingChanged };
  using Completion = std::function<void(bool, const std::string&)>;
  using Listener = std::function<void(ShortcutId, Signal, const std::string&)>;
  explicit ShortcutPortalLinux(Listener listener);
  ~ShortcutPortalLinux();
  static bool IsSupported();
  void Request(std::shared_ptr<Shortcut> shortcut, std::string trigger,
               std::shared_ptr<std::atomic<bool>> cancelled, Completion completion);
  void Cancel(ShortcutId id);
  bool Unregister(ShortcutId id);
  std::string TriggerDescription(ShortcutId id) const;
  void Shutdown();

 private:
  struct State;
  std::shared_ptr<State> state_;
};

}  // namespace nativeapi::detail
