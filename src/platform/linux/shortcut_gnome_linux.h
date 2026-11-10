#pragma once

#include <memory>
#include <optional>
#include "shortcut_portal_linux.h"

namespace nativeapi::detail {

// Unsandboxed GNOME fallback for sessions without GlobalShortcuts. GNOME's
// media-keys daemon owns the actual compositor binding; activation returns over
// our private session-bus connection. No XWayland grabs or shell extensions.
class ShortcutGnomeLinux {
 public:
  using Completion = ShortcutPortalLinux::Completion;
  using Listener = ShortcutPortalLinux::Listener;
  explicit ShortcutGnomeLinux(Listener listener);
  ~ShortcutGnomeLinux();
  static bool IsSupported();
  void Request(std::shared_ptr<Shortcut> shortcut,
               std::string trigger,
               std::shared_ptr<std::atomic<bool>> cancelled,
               Completion completion);
  void Cancel(ShortcutId id);
  bool Unregister(ShortcutId id);
  std::optional<std::string> TriggerDescription(ShortcutId id) const;
  void Shutdown();

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace nativeapi::detail
