#include "shortcut.h"

namespace nativeapi {

namespace {

std::shared_ptr<std::function<void()>> MakeCallback(std::function<void()> callback) {
  if (!callback) {
    return nullptr;
  }
  return std::make_shared<std::function<void()>>(std::move(callback));
}

}  // namespace

Shortcut::Shortcut(ShortcutId id, const ShortcutOptions& options)
    : id_(id),
      accelerator_(options.accelerator),
      description_(options.description),
      scope_(options.scope),
      enabled_(options.enabled),
      callback_(MakeCallback(options.callback)) {}

Shortcut::Shortcut(ShortcutId id, const std::string& accelerator, std::function<void()> callback)
    : id_(id),
      accelerator_(accelerator),
      description_(""),
      scope_(ShortcutScope::Global),
      enabled_(true),
      callback_(MakeCallback(std::move(callback))) {}

Shortcut::~Shortcut() = default;

ShortcutId Shortcut::GetId() const {
  return id_;
}

std::string Shortcut::GetAccelerator() const {
  return accelerator_;
}

std::string Shortcut::GetDescription() const {
  return description_;
}

void Shortcut::SetDescription(const std::string& description) {
  description_ = description;
}

ShortcutScope Shortcut::GetScope() const {
  return scope_;
}

void Shortcut::SetEnabled(bool enabled) {
  enabled_ = enabled;
}

bool Shortcut::IsEnabled() const {
  return enabled_;
}

void Shortcut::Invoke() {
  if (!enabled_) {
    return;
  }
  // Keep the running callback alive: it may replace or clear itself.
  std::shared_ptr<std::function<void()>> callback;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback = callback_;
  }
  if (callback) {
    (*callback)();
  }
}

void Shortcut::SetCallback(std::function<void()> callback) {
  auto replacement = MakeCallback(std::move(callback));
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_.swap(replacement);
  }
  // The previous callback is released here, outside the lock: destroying its
  // captures may call back into this shortcut.
}

std::function<void()> Shortcut::GetCallback() const {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  return callback_ ? *callback_ : std::function<void()>();
}

}  // namespace nativeapi
