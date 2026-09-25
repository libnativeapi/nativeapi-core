// The C ABI's release contract: every function taking a callback releases the
// user_data passed with it exactly once, after the last time the core can call
// the callback, on the main thread and never inside the call that let it go.
//
// A fake main-thread dispatcher collects the posted releases so the test can
// tell "posted" from "run inline" and drain them deterministically.

#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <vector>

#include "../src/capi/shortcut_c.h"
#include "../src/capi/shortcut_manager_c.h"
#include "../src/capi/window_manager_c.h"
#include "../src/foundation/dispatcher.h"

namespace {

int g_failures = 0;
std::vector<std::function<void()>> g_posted;
std::map<void*, int> g_released;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    ++g_failures;
  }
}

void Release(void* user_data) {
  ++g_released[user_data];
}

void Drain() {
  while (!g_posted.empty()) {
    auto work = std::move(g_posted);
    g_posted.clear();
    for (auto& fn : work) {
      fn();
    }
  }
}

int Released(void* user_data) {
  auto it = g_released.find(user_data);
  return it == g_released.end() ? 0 : it->second;
}

void OnShortcutEvent(const native_shortcut_event_t*, void*) {}
void OnShortcut(void*) {}
void OnHook(unsigned int, void*) {}

// Distinct addresses to pass as user_data.
char g_tokens[16];
void* Token(int i) {
  return &g_tokens[i];
}

void TestListener() {
  void* data = Token(0);
  auto id = native_shortcut_manager_add_listener(OnShortcutEvent, data, Release);
  Check(id != NATIVE_INVALID_LISTENER_ID, "add_listener failed");
  Drain();
  Check(Released(data) == 0, "a live listener was released");
  Check(native_shortcut_manager_remove_listener(id), "remove_listener failed");
  Check(Released(data) == 0, "release ran inside remove_listener");
  Drain();
  Check(Released(data) == 1, "removed listener not released exactly once");
  native_shortcut_manager_remove_listener(id);
  Drain();
  Check(Released(data) == 1, "second removal released again");
}

void TestFailuresRelease() {
  void* null_callback = Token(1);
  auto id = native_shortcut_manager_add_listener(nullptr, null_callback, Release);
  Check(id == NATIVE_INVALID_LISTENER_ID, "NULL callback registered");
  Drain();
  Check(Released(null_callback) == 1, "NULL callback: user_data not released");

  void* bad_handle = Token(2);
  native_shortcut_set_callback(0, OnShortcut, bad_handle, Release);
  Drain();
  Check(Released(bad_handle) == 1, "invalid handle: user_data not released");

  void* bad_accelerator = Token(3);
  auto shortcut = native_shortcut_manager_register_with_accelerator_and_callback(
      "Not+An+Accelerator", OnShortcut, bad_accelerator, Release);
  Check(shortcut == 0, "invalid accelerator registered");
  Drain();
  Check(Released(bad_accelerator) == 1, "failed registration: user_data not released");

  // NULL release: nothing to call, nothing to crash.
  native_shortcut_manager_add_listener(nullptr, Token(4), nullptr);
  Drain();
}

void TestReplacementAndOwnerDestruction() {
  void* first = Token(5);
  void* second = Token(6);
  auto shortcut = native_shortcut_create_with_id_and_accelerator_and_callback(
      900001, "Ctrl+A", OnShortcut, first, Release);
  Check(shortcut != 0, "create failed");
  native_shortcut_set_callback(shortcut, OnShortcut, second, Release);
  Drain();
  Check(Released(first) == 1, "replaced callback not released");
  Check(Released(second) == 0, "installed callback released");
  native_shortcut_free(shortcut);  // last reference: the Shortcut goes away
  Drain();
  Check(Released(second) == 1, "callback not released with its owner");
}

void TestHookSlot() {
  void* hook = Token(7);
  native_window_manager_set_will_show_hook(OnHook, hook, Release);
  Drain();
  Check(Released(hook) == 0, "installed hook released");
  void* cleared = Token(8);
  native_window_manager_set_will_show_hook(nullptr, cleared, Release);
  Drain();
  Check(Released(hook) == 1, "cleared hook not released");
  Check(Released(cleared) == 1, "NULL hook's user_data not released");
}

void TestOptionsStruct() {
  void* data = Token(9);
  native_shortcut_options_t options = {};
  options.accelerator = "Ctrl+B";
  options.callback = OnShortcut;
  options.callback_user_data = data;
  options.callback_release_user_data = Release;
  options.enabled = true;
  auto shortcut = native_shortcut_create_with_id_and_options(900002, options);
  Check(shortcut != 0, "create with options failed");
  Drain();
  Check(Released(data) == 0, "options callback released while installed");
  native_shortcut_free(shortcut);
  Drain();
  Check(Released(data) == 1, "options callback not released with its owner");
}

void TestRegistration() {
  void* data = Token(10);
  auto shortcut = native_shortcut_manager_register_with_accelerator_and_callback(
      "Ctrl+Alt+Shift+F8", OnShortcut, data, Release);
  if (shortcut == 0) {
    std::cout << "SKIP: platform could not register a global shortcut" << std::endl;
    Drain();
    return;
  }
  auto id = native_shortcut_get_id(shortcut);
  native_shortcut_free(shortcut);  // the manager still holds it
  Drain();
  Check(Released(data) == 0, "registered callback released with the handle");
  Check(native_shortcut_manager_unregister_with_id(id), "unregister failed");
  Drain();
  Check(Released(data) == 1, "unregistered callback not released");
}

}  // namespace

int main() {
  std::cout << "user_data_release_test" << std::endl;
  nativeapi::SetMainThreadDispatcher(
      [](std::function<void()> fn) {
        g_posted.push_back(std::move(fn));
        return true;
      },
      [] { return true; });

  TestListener();
  TestFailuresRelease();
  TestReplacementAndOwnerDestruction();
  TestHookSlot();
  TestOptionsStruct();
  TestRegistration();

  if (g_failures > 0) {
    std::cerr << g_failures << " check(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "All checks passed." << std::endl;
  return EXIT_SUCCESS;
}
