// Provider-independent lifecycle tests. No window, OS reservation or input.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../src/shortcut_manager.h"

namespace {
std::mutex dispatch_mutex;
std::vector<std::function<void()>> dispatch;
bool dispatcher_works = true;
void Drain() {
  std::vector<std::function<void()>> work;
  { std::lock_guard<std::mutex> lock(dispatch_mutex); work.swap(dispatch); }
  for (auto& fn : work) fn();
}
}

namespace nativeapi {
bool RunOnMainThread(std::function<void()> fn) {
  if (!dispatcher_works) return false;
  std::lock_guard<std::mutex> lock(dispatch_mutex);
  dispatch.push_back(std::move(fn));
  return true;
}

class ShortcutManagerImpl final : public ShortcutManager::Impl {
 public:
  struct Call {
    std::shared_ptr<Shortcut> shortcut;
    std::shared_ptr<std::atomic<bool>> cancelled;
    RegistrationCompletion completion;
  };
  static ShortcutManagerImpl* provider;
  static std::unique_ptr<ShortcutManager> Create() {
    return std::unique_ptr<ShortcutManager>(new ShortcutManager());
  }
  explicit ShortcutManagerImpl(ShortcutManager* value) : manager(value) { provider = this; }
  bool IsSupported() override { assert(!forbid_support_probe); return supported; }
  bool CanRegister(const std::string&) override { ++preflights; return allow; }
  bool RegisterShortcut(const std::shared_ptr<Shortcut>& shortcut) override {
    if (!allow) return false;
    std::lock_guard<std::mutex> lock(resource_mutex);
    owned.emplace(shortcut->GetId(), shortcut);
    return true;
  }
  bool UnregisterShortcut(const std::shared_ptr<Shortcut>& shortcut) override {
    std::lock_guard<std::mutex> lock(resource_mutex);
    ++removals;
    return owned.erase(shortcut->GetId()) != 0;
  }
  bool ShouldDeferRegistration(const std::shared_ptr<Shortcut>&) override { return deferred; }
  void RequestShortcutRegistration(const std::shared_ptr<Shortcut>& shortcut,
                                   std::shared_ptr<std::atomic<bool>> cancelled,
                                   RegistrationCompletion completion) override {
    // Both provider entry points may query/re-enter the manager.
    (void)manager->GetAll();
    if (cancel_before_start) assert(manager->CancelRegistration(shortcut->GetId()));
    if (cancelled->load()) {
      ++suppressed_starts;
      completion(false, "Cancelled before start");
      return;
    }
    calls.emplace(shortcut->GetId(), Call{shortcut, std::move(cancelled), std::move(completion)});
  }
  void CancelShortcutRegistration(ShortcutId id) override {
    (void)manager->GetAll();
    cancelled_ids.push_back(id);
  }
  void SetupEventMonitoring() override {}
  void CleanupEventMonitoring() override {}
  void Finish(ShortcutId id, bool success) {
    auto& call = calls.at(id);
    if (success) {
      std::lock_guard<std::mutex> lock(resource_mutex);
      owned[id] = call.shortcut;
    }
    call.completion(success, success ? "" : "Permission denied");
  }
  ShortcutManager* manager;
  bool supported = true, deferred = true, allow = true, cancel_before_start = false;
  bool forbid_support_probe = false;
  int suppressed_starts = 0, removals = 0, preflights = 0;
  std::unordered_map<ShortcutId, Call> calls;
  std::unordered_map<ShortcutId, std::shared_ptr<Shortcut>> owned;
  std::vector<ShortcutId> cancelled_ids;
  std::mutex resource_mutex;
};
ShortcutManagerImpl* ShortcutManagerImpl::provider = nullptr;
ShortcutManager::ShortcutManager()
    : pimpl_(std::make_unique<ShortcutManagerImpl>(this)), next_shortcut_id_(1), enabled_(true) {}
ShortcutManager::~ShortcutManager() { ShutdownEmitter(); UnregisterAll(); }
}

int main() {
  using namespace nativeapi;
  auto manager = ShortcutManagerImpl::Create();
  auto& provider = *ShortcutManagerImpl::provider;
  std::vector<ShortcutId> successes, failures;
  manager->AddListener<ShortcutRegisteredEvent>([&](const auto& event) {
    successes.push_back(event.GetShortcutId());
    assert(manager->Get(event.GetShortcutId()));
  });
  manager->AddListener<ShortcutRegistrationFailedEvent>([&](const auto& event) {
    failures.push_back(event.GetShortcutId());
    assert(!manager->IsRegistrationPending(event.GetShortcutId()));
  });
  ShortcutOptions options;
  options.accelerator = "Ctrl+F19";
  options.description = "Human-readable action";
  assert(!manager->RequestRegistration(options, ""));
  auto invalid = options; invalid.accelerator = "invalid";
  assert(!manager->RequestRegistration(invalid, "action"));
  provider.supported = false;
  provider.deferred = false;
  assert(!manager->RequestRegistration(options, "action"));
  provider.supported = true; provider.deferred = true;
  Drain(); assert(successes.empty() && failures.empty());

  provider.forbid_support_probe = true;
  const auto first = manager->RequestRegistration(options, "action");
  provider.forbid_support_probe = false;
  assert(first && manager->IsRegistrationPending(first));
  assert(!manager->Get(first) && manager->GetAll().empty());
  assert(!manager->IsAvailable(options.accelerator));
  assert(!manager->RequestRegistration(options, "another-key"));
  auto other = options; other.accelerator = "Ctrl+F20";
  assert(!manager->RequestRegistration(other, "action"));
  assert(!manager->Register(options)); // Its ID=0 failure cannot complete first.
  Drain(); assert(failures == std::vector<ShortcutId>{0}); failures.clear();
  provider.Finish(first, true);
  auto active = manager->Get(first);
  assert(active && active->GetRegistrationKey() == "action");
  assert(active->GetDescription() == "Human-readable action");
  assert(!manager->RequestRegistration(other, "action")); // A live action key is unique.
  assert(!manager->IsRegistrationPending(first) && !manager->CancelRegistration(first));
  provider.Finish(first, true); // Duplicate responses must not remove active state.
  Drain(); assert(successes == std::vector<ShortcutId>{first});
  assert(provider.owned.count(first) && provider.removals == 0);
  assert(manager->Unregister(first)); successes.clear();

  const auto denied = manager->RequestRegistration(options, "action");
  provider.Finish(denied, false); Drain();
  assert(failures == std::vector<ShortcutId>{denied} && !manager->Get(denied));
  failures.clear();
  const auto cancelled = manager->RequestRegistration(options, "action");
  assert(manager->CancelRegistration(cancelled));
  assert(provider.calls.at(cancelled).cancelled->load());
  assert(!manager->CancelRegistration(cancelled));
  const auto replacement = manager->RequestRegistration(options, "action");
  provider.Finish(replacement, true);
  provider.Finish(cancelled, true); // Late approval releases only the old ID.
  Drain();
  assert(failures == std::vector<ShortcutId>{cancelled});
  assert(!provider.owned.count(cancelled) && provider.owned.count(replacement));
  assert(manager->Get(replacement));
  successes.clear(); failures.clear();

  provider.cancel_before_start = true;
  const auto cancelled_early = manager->RequestRegistration(other, "early");
  assert(cancelled_early && provider.suppressed_starts == 1);
  assert(!manager->IsRegistrationPending(cancelled_early));
  provider.cancel_before_start = false;
  Drain(); assert(failures == std::vector<ShortcutId>{cancelled_early}); failures.clear();

  const auto pending = manager->RequestRegistration(other, "other");
  assert(manager->UnregisterAll() == 2); // One active plus one pending.
  assert(manager->GetAll().empty() && !manager->IsRegistrationPending(pending));
  provider.Finish(pending, false); Drain();
  assert(failures == std::vector<ShortcutId>{pending}); failures.clear();

  provider.deferred = false;
  dispatcher_works = false; // Synchronous fallback listeners can re-enter.
  const auto immediate = manager->RequestRegistration(options, "immediate");
  assert(immediate && manager->Get(immediate));
  assert(successes == std::vector<ShortcutId>{immediate});
  assert(manager->Unregister(immediate));
  provider.allow = false;
  const auto refused = manager->RequestRegistration(options, "refused");
  assert(refused && failures == std::vector<ShortcutId>{refused});
  provider.allow = true;
  provider.supported = false;
  auto local = options; local.scope = ShortcutScope::Application;
  const int probes = provider.preflights;
  const auto local_id = manager->RequestRegistration(local, "local");
  assert(local_id && manager->Get(local_id) && provider.preflights == probes);
  assert(manager->Unregister(local_id));
  provider.supported = true; provider.deferred = true; dispatcher_works = true;

  // An interpreter restart can reclaim its namespace without knowing old IDs.
  // Already active registrations and another module's pending work survive.
  const auto stale = manager->RequestRegistration(options, "owner:stale");
  const auto peer = manager->RequestRegistration(other, "peer:pending");
  auto third = options; third.accelerator = "Ctrl+F21";
  const auto owned_active = manager->RequestRegistration(third, "owner:active");
  provider.Finish(owned_active, true); Drain();
  assert(manager->CancelPendingRegistrations("") == 0);
  assert(manager->CancelPendingRegistrations("owner:") == 1);
  assert(!manager->IsRegistrationPending(stale));
  assert(manager->IsRegistrationPending(peer) && manager->Get(owned_active));
  provider.Finish(stale, true); Drain();
  assert(!provider.owned.count(stale));
  assert(manager->CancelPendingRegistrations("owner:") == 0);
  manager->UnregisterAll(); Drain();

  // Results are historical notifications: an approved shortcut may have been
  // explicitly removed before its queued Registered event is delivered.
  manager->RemoveAllListeners();
  for (int iteration = 0; iteration < 256; ++iteration) {
    const auto racing = manager->RequestRegistration(options, "racing");
    assert(racing);
    std::thread approval([&] { provider.Finish(racing, true); });
    manager->UnregisterAll();
    approval.join();
    assert(!manager->Get(racing) && !manager->IsRegistrationPending(racing));
    assert(provider.owned.empty());
  }
  Drain();

  const auto shutdown = manager->RequestRegistration(options, "shutdown");
  auto late = provider.calls.at(shutdown).completion;
  auto cancellation = provider.calls.at(shutdown).cancelled;
  manager.reset();
  assert(cancellation->load());
  late(true, ""); // The lifetime guard prevents access to the destroyed manager.
  Drain();
  std::cout << "registration request lifecycle checks passed\n";
}
