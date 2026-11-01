#include "shortcut_manager.h"

#include <regex>
#include <algorithm>
#include <cctype>

#include "platform/shared/shortcut_native_key.h"

namespace nativeapi {

// Singleton instance
ShortcutManager& ShortcutManager::GetInstance() {
  static ShortcutManager instance;
  return instance;
}

// Check if global shortcuts are supported
bool ShortcutManager::IsSupported() {
  return pimpl_->IsSupported();
}

// Register a new keyboard shortcut with callback
std::shared_ptr<Shortcut> ShortcutManager::Register(const std::string& accelerator,
                                                    std::function<void()> callback) {
  ShortcutOptions options;
  options.accelerator = accelerator;
  options.callback = callback;
  return Register(options);
}

// Register a new keyboard shortcut with options
std::shared_ptr<Shortcut> ShortcutManager::Register(const ShortcutOptions& options) {
  std::unique_lock<std::mutex> lock(mutex_);

  // Validate accelerator format
  if (!IsValidAccelerator(options.accelerator)) {
    lock.unlock();
    // Emit failure event
    EmitAsync<ShortcutRegistrationFailedEvent>(0, options.accelerator,
                                               "Invalid accelerator format");
    return nullptr;
  }

  // Keep duplicate checking atomic with insertion. A local registration must
  // not probe or reserve the global chord: another app may own that chord.
  // The platform registration still validates scope, aliases and local support.
  if (shortcuts_by_accelerator_.find(options.accelerator) != shortcuts_by_accelerator_.end() ||
      HasPendingAcceleratorUnlocked(options.accelerator) ||
      (options.scope == ShortcutScope::Global && !pimpl_->CanRegister(options.accelerator))) {
    lock.unlock();
    // Emit failure event
    EmitAsync<ShortcutRegistrationFailedEvent>(0, options.accelerator,
                                               "Accelerator unavailable or already registered");
    return nullptr;
  }

  // Allocate new ID
  ShortcutId id = next_shortcut_id_++;

  // Create shortcut instance
  auto shortcut = std::make_shared<Shortcut>(id, options);

  // Register with platform
  if (!pimpl_->RegisterShortcut(shortcut)) {
    lock.unlock();
    // Platform registration failed
    EmitAsync<ShortcutRegistrationFailedEvent>(id, options.accelerator,
                                               "Platform registration failed");
    return nullptr;
  }

  // Store in registries
  shortcuts_by_id_[id] = shortcut;
  shortcuts_by_accelerator_[options.accelerator] = shortcut;

  // Emit success event
  EmitAsync<ShortcutRegisteredEvent>(id, options.accelerator);

  return shortcut;
}

bool ShortcutManager::HasPendingAcceleratorUnlocked(const std::string& accelerator) const {
  return std::any_of(pending_registrations_.begin(), pending_registrations_.end(),
                     [&](const auto& item) {
                       return item.second->shortcut->GetAccelerator() == accelerator;
                     });
}

ShortcutId ShortcutManager::RequestRegistration(const ShortcutOptions& options,
                                                const std::string& registration_key) {
  if (registration_key.empty() || !IsValidAccelerator(options.accelerator)) return 0;

  auto shortcut = std::make_shared<Shortcut>(0, options);
  const bool deferred = pimpl_->ShouldDeferRegistration(shortcut);
  // A deferred provider checks capability on its own event context. Even a
  // stalled or absent desktop service must not block the caller's UI isolate.
  if (!deferred && options.scope == ShortcutScope::Global && !pimpl_->IsSupported()) return 0;

  std::shared_ptr<PendingRegistration> request;
  bool immediate_success = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (next_shortcut_id_ == 0 || shortcuts_by_accelerator_.count(options.accelerator) ||
        HasPendingAcceleratorUnlocked(options.accelerator)) return 0;
    for (const auto& item : pending_registrations_) {
      if (item.second->shortcut->GetRegistrationKey() == registration_key) return 0;
    }
    for (const auto& item : shortcuts_by_id_) {
      if (item.second->GetRegistrationKey() == registration_key) return 0;
    }
    shortcut->id_ = next_shortcut_id_++;
    shortcut->registration_key_ = registration_key;
    request = std::make_shared<PendingRegistration>(std::move(shortcut));
    pending_registrations_.emplace(request->shortcut->GetId(), request);
    if (!deferred) {
      // Keep legacy platform maps serialized with Register/Unregister. No
      // completion or user callback runs under this registry lock.
      immediate_success =
          (options.scope != ShortcutScope::Global || pimpl_->CanRegister(options.accelerator)) &&
          pimpl_->RegisterShortcut(request->shortcut);
      request->completed.store(true);
      pending_registrations_.erase(request->shortcut->GetId());
      if (immediate_success) {
        shortcuts_by_id_.emplace(request->shortcut->GetId(), request->shortcut);
        shortcuts_by_accelerator_.emplace(options.accelerator, request->shortcut);
      }
    }
  }

  if (!deferred) {
    if (immediate_success) {
      EmitAsync<ShortcutRegisteredEvent>(request->shortcut->GetId(), options.accelerator);
    } else {
      request->shortcut->SetCallback(nullptr);
      EmitAsync<ShortcutRegistrationFailedEvent>(request->shortcut->GetId(), options.accelerator,
                                                "Platform registration failed");
    }
    return request->shortcut->GetId();
  }
  auto completion = CreateGuardedCallback<bool, const std::string&>(
      Impl::RegistrationCompletion([this, request](bool success, const std::string& reason) {
        CompleteRegistration(request, success, reason);
      }));
  pimpl_->RequestShortcutRegistration(request->shortcut, request->cancelled, std::move(completion));
  return request->shortcut->GetId();
}

void ShortcutManager::CompleteRegistration(const std::shared_ptr<PendingRegistration>& request,
                                           bool success, const std::string& reason) {
  if (request->completed.exchange(true)) return;
  const auto& shortcut = request->shortcut;
  const auto trigger = success ? pimpl_->GetShortcutTriggerDescription(shortcut->GetId()) : std::nullopt;
  bool discarded = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_registrations_.find(shortcut->GetId());
    discarded = it == pending_registrations_.end() || it->second != request;
    if (!discarded) {
      pending_registrations_.erase(it);
      if (success) {
        shortcut->trigger_description_ = trigger; // Not publicly visible until insertion.
        shortcuts_by_id_.emplace(shortcut->GetId(), shortcut);
        shortcuts_by_accelerator_.emplace(shortcut->GetAccelerator(), shortcut);
      }
    }
  }
  // A provider can finish after cancellation. Release that exact registration,
  // never a newer request for the same accelerator or application key.
  if (discarded) {
    if (success) pimpl_->UnregisterShortcut(shortcut);
    return;
  }
  if (success) {
    EmitAsync<ShortcutRegisteredEvent>(shortcut->GetId(), shortcut->GetAccelerator());
  } else {
    shortcut->SetCallback(nullptr);
    EmitAsync<ShortcutRegistrationFailedEvent>(shortcut->GetId(), shortcut->GetAccelerator(), reason);
  }
}

bool ShortcutManager::CancelRegistration(ShortcutId request_id) {
  std::shared_ptr<PendingRegistration> request;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_registrations_.find(request_id);
    if (it == pending_registrations_.end()) return false;
    request = it->second;
    request->cancelled->store(true);
    pending_registrations_.erase(it);
  }
  pimpl_->CancelShortcutRegistration(request_id);
  request->shortcut->SetCallback(nullptr);
  EmitAsync<ShortcutRegistrationFailedEvent>(request_id, request->shortcut->GetAccelerator(),
                                            "Registration cancelled");
  return true;
}

bool ShortcutManager::IsRegistrationPending(ShortcutId request_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_registrations_.count(request_id) != 0;
}

size_t ShortcutManager::CancelPendingRegistrations(const std::string& registration_key_prefix) {
  if (registration_key_prefix.empty()) return 0;
  std::vector<ShortcutId> requests;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& item : pending_registrations_) {
      if (item.second->shortcut->GetRegistrationKey().compare(
              0, registration_key_prefix.size(), registration_key_prefix) == 0) {
        requests.push_back(item.first);
      }
    }
  }
  size_t count = 0;
  for (const auto id : requests) {
    if (CancelRegistration(id) || Unregister(id)) ++count;
  }
  return count;
}

void ShortcutManager::UpdateTriggerDescription(ShortcutId id, const std::string& description) {
  auto shortcut = Get(id);
  if (!shortcut) return;
  std::lock_guard<std::mutex> lock(shortcut->binding_mutex_);
  shortcut->trigger_description_ = description;
}

// Unregister a shortcut by ID
bool ShortcutManager::Unregister(ShortcutId id) {
  std::unique_lock<std::mutex> lock(mutex_);

  auto it = shortcuts_by_id_.find(id);
  if (it == shortcuts_by_id_.end()) {
    return false;
  }

  auto shortcut = it->second;
  std::string accelerator = shortcut->GetAccelerator();

  // Unregister from platform
  if (!pimpl_->UnregisterShortcut(shortcut)) return false;

  // Remove from registries
  shortcuts_by_id_.erase(it);
  shortcuts_by_accelerator_.erase(accelerator);

  // Destroying captured objects may re-enter the manager. Do it outside the lock.
  lock.unlock();
  // Retained handles must not invoke a callback after its registration ends.
  shortcut->SetCallback(nullptr);

  // Emit event
  EmitAsync<ShortcutUnregisteredEvent>(id, accelerator);

  return true;
}

// Unregister a shortcut by accelerator
bool ShortcutManager::Unregister(const std::string& accelerator) {
  std::unique_lock<std::mutex> lock(mutex_);

  auto it = shortcuts_by_accelerator_.find(accelerator);
  if (it == shortcuts_by_accelerator_.end()) {
    return false;
  }

  ShortcutId id = it->second->GetId();

  lock.unlock();
  bool result = Unregister(id);
  lock.lock();

  return result;
}

// Unregister all shortcuts
int ShortcutManager::UnregisterAll() {
  std::unique_lock<std::mutex> lock(mutex_);

  int count = 0;

  // Create a copy of IDs to avoid iterator invalidation
  std::vector<ShortcutId> ids;
  ids.reserve(shortcuts_by_id_.size());
  for (const auto& [id, shortcut] : shortcuts_by_id_) {
    ids.push_back(id);
  }
  std::vector<ShortcutId> requests;
  requests.reserve(pending_registrations_.size());
  for (const auto& item : pending_registrations_) requests.push_back(item.first);

  lock.unlock();
  for (auto id : requests) {
    // Approval may race with this snapshot. If it already became active,
    // unregister the same ID instead of leaving the newly approved grab alive.
    if (CancelRegistration(id) || Unregister(id)) ++count;
  }
  for (ShortcutId id : ids) {
    if (Unregister(id)) {
      count++;
    }
  }
  lock.lock();

  return count;
}

// Get a shortcut by ID
std::shared_ptr<Shortcut> ShortcutManager::Get(ShortcutId id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = shortcuts_by_id_.find(id);
  return (it != shortcuts_by_id_.end()) ? it->second : nullptr;
}

// Get a shortcut by accelerator
std::shared_ptr<Shortcut> ShortcutManager::Get(const std::string& accelerator) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = shortcuts_by_accelerator_.find(accelerator);
  return (it != shortcuts_by_accelerator_.end()) ? it->second : nullptr;
}

// Get all shortcuts
std::vector<std::shared_ptr<Shortcut>> ShortcutManager::GetAll() {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::shared_ptr<Shortcut>> result;
  result.reserve(shortcuts_by_id_.size());

  for (const auto& [id, shortcut] : shortcuts_by_id_) {
    result.push_back(shortcut);
  }

  return result;
}

// Get shortcuts by scope
std::vector<std::shared_ptr<Shortcut>> ShortcutManager::GetByScope(ShortcutScope scope) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::shared_ptr<Shortcut>> result;

  for (const auto& [id, shortcut] : shortcuts_by_id_) {
    if (shortcut->GetScope() == scope) {
      result.push_back(shortcut);
    }
  }

  return result;
}

// Check if an accelerator is available
bool ShortcutManager::IsAvailable(const std::string& accelerator) {
  std::lock_guard<std::mutex> lock(mutex_);
  return IsAvailableUnlocked(accelerator);
}

// Availability check for callers that already hold mutex_.
bool ShortcutManager::IsAvailableUnlocked(const std::string& accelerator) const {
  return shortcuts_by_accelerator_.find(accelerator) == shortcuts_by_accelerator_.end() &&
         !HasPendingAcceleratorUnlocked(accelerator) &&
         pimpl_->CanRegister(accelerator);
}

// Validate accelerator format
bool ShortcutManager::IsValidAccelerator(const std::string& accelerator) {
  if (accelerator.empty()) {
    return false;
  }

  // Basic validation using regex.
  // Format: [Modifier+]*Key
  //
  // Modifiers: Ctrl/Control, Alt/Option, Shift, Cmd/Command, Super, Meta,
  //            CmdOrCtrl/CommandOrControl
  // Keys:      A-Z, 0-9, F1-F24, named keys (Space, Enter, PageUp, Comma, ...),
  //            the keypad (Num0-Num9, NumAdd, ...), and the literal punctuation
  //            characters those names stand for (",", ".", "/", ...).
  //
  // This has to stay in step with the per-platform token tables in
  // src/platform/*/shortcut_manager_*. Anything accepted here but unknown to a
  // platform parser degrades to a ShortcutRegistrationFailedEvent rather than a
  // silent no-op, but the two lists are meant to agree.
  static const std::regex accelerator_regex(
      R"(^(?:(?:Ctrl|Control|Alt|Option|Shift|Cmd|Command|Super|Meta|CmdOrCtrl|CommandOrControl|CapsLock|Fn)\+)*)"
      R"((?:F1[0-9]|F2[0-4]|F[1-9]|Num[0-9]|NumDec|NumAdd|NumSub|NumMult|NumDiv|NumEnter)"
      R"(|Space|Tab|Enter|Return|Escape|Esc|Backspace|ForwardDelete|Delete|Insert|Help)"
      R"(|Home|End|PageUp|PageDown|Up|Down|Left|Right)"
      R"(|Plus|Minus|Equal|Comma|Period|Slash|Backslash|Semicolon|Quote)"
      R"(|LeftBracket|RightBracket|Grave|Backquote|NativeKey[0-9]{1,10})"
      R"(|[A-Za-z0-9]|[,./\\;'\[\]`=\-])$)",
      std::regex::icase);

  if (!std::regex_match(accelerator, accelerator_regex)) return false;
  auto key = accelerator.substr(accelerator.find_last_of('+') + 1);
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (key.compare(0, 9, "nativekey") == 0) {
    uint32_t keycode = 0;
    return detail::ParseNativeShortcutKey(key, keycode);
  }
  return true;
}

// Enable or disable shortcut processing
void ShortcutManager::SetEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_ = enabled;
}

// Check if shortcut processing is enabled
bool ShortcutManager::IsEnabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return enabled_;
}

void ShortcutManager::EmitShortcutActivated(ShortcutId id, const std::string& accelerator) {
  EmitAsync<ShortcutActivatedEvent>(id, accelerator);
}

void ShortcutManager::EmitShortcutReleased(ShortcutId id, const std::string& accelerator) {
  EmitAsync<ShortcutReleasedEvent>(id, accelerator);
}

// Start event listening (called when first listener is added)
void ShortcutManager::StartEventListening() {
  pimpl_->SetupEventMonitoring();
}

// Stop event listening (called when last listener is removed)
void ShortcutManager::StopEventListening() {
  pimpl_->CleanupEventMonitoring();
}

}  // namespace nativeapi
