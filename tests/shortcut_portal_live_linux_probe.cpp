// Manual, no-input probe against the logged-on desktop's real Portal service.
// May display a desktop permission dialog. Never approves it or sends input.
#include <gio/gio.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "../src/shortcut_manager.h"

int main() {
  using namespace nativeapi;
  auto* application = g_application_new("org.libnativeapi.ShortcutProbe", G_APPLICATION_NON_UNIQUE);
  g_application_set_default(application);
  auto& manager = ShortcutManager::GetInstance();
  if (!manager.IsSupported()) {
    std::cout << "UNSUPPORTED: no responding GlobalShortcuts Portal provider\n";
    g_application_set_default(nullptr);
    g_object_unref(application);
    return 77;
  }
  std::atomic<bool> completed{false}, success{false}, received{false};
  const bool wait_for_event = std::getenv("NATIVEAPI_PROBE_WAIT_FOR_EVENT") != nullptr;
  const auto listener = manager.AddListener<ShortcutEvent>([&](const ShortcutEvent& event) {
    if (auto* registered = dynamic_cast<const ShortcutRegisteredEvent*>(&event)) {
      const auto shortcut = manager.Get(registered->GetShortcutId());
      if (!shortcut) return;
      std::cout << "REGISTERED key=" << shortcut->GetRegistrationKey()
                << " trigger=" << shortcut->GetTriggerDescription() << std::endl;
      completed = true; success = true;
    } else if (auto* failed = dynamic_cast<const ShortcutRegistrationFailedEvent*>(&event)) {
      std::cout << "FAILED: " << failed->GetErrorMessage() << std::endl;
      completed = true;
    } else if (dynamic_cast<const ShortcutActivatedEvent*>(&event)) {
      std::cout << "ACTIVATED: real compositor/Portal delivery" << std::endl;
      received = true;
    } else if (dynamic_cast<const ShortcutReleasedEvent*>(&event)) {
      std::cout << "RELEASED: real compositor/Portal delivery" << std::endl;
      received = true;
    }
  });
  ShortcutOptions options;
  options.accelerator = "Ctrl+Shift+F19";
  options.description = "NativeAPI shortcut verification";
  const auto id = manager.RequestRegistration(options, "nativeapi-shortcut-probe");
  if (!id) {
    std::cout << "REJECTED: request not accepted\n";
  } else {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!completed || (success && wait_for_event && !received)) &&
           std::chrono::steady_clock::now() < deadline) {
      while (g_main_context_iteration(nullptr, false)) {}
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!completed) std::cout << "PENDING: approval not supplied; cancelling this probe\n";
    if (!manager.CancelRegistration(id)) manager.Unregister(id);
    // Drain notifications before removing a listener holding stack references.
    while (g_main_context_iteration(nullptr, false)) {}
  }
  manager.RemoveListener(listener);
  g_application_set_default(nullptr);
  g_object_unref(application);
  return success && (!wait_for_event || received) ? 0 : 1;
}
