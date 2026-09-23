// Callbacks must keep their captured objects alive while replacing or clearing
// themselves. Unregistering a shortcut must release its callback even if the
// caller still owns the shortcut.
//
// The registration case needs a desktop session and skips if the platform cannot
// register the accelerator. The self-replacement cases run without registration.

#include <cstdlib>
#include <iostream>
#include <memory>

#include "../src/shortcut.h"
#include "../src/shortcut_manager.h"
#include "../src/window_manager.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    ++g_failures;
  }
}

void TestShortcutSelfReplacement() {
  nativeapi::Shortcut shortcut(1, "Ctrl+A", nullptr);
  auto captured = std::make_shared<int>(42);
  std::weak_ptr<int> weak = captured;
  int replacements = 0;
  shortcut.SetCallback([&, captured] {
    shortcut.SetCallback([&] { ++replacements; });
    Check(!weak.expired(), "active shortcut callback was destroyed during replacement");
  });
  captured.reset();
  shortcut.Invoke();
  Check(weak.expired(), "replaced shortcut callback was retained");
  shortcut.Invoke();
  Check(replacements == 1, "replacement callback was not installed");
  shortcut.SetCallback(nullptr);
}

void TestHookSelfRemoval() {
  auto& manager = nativeapi::WindowManager::GetInstance();
  for (bool show : {true, false}) {
    auto captured = std::make_shared<int>(42);
    std::weak_ptr<int> weak = captured;
    auto callback = [&, captured](nativeapi::WindowId) {
      if (show) {
        manager.SetWillShowHook(std::nullopt);
      } else {
        manager.SetWillHideHook(std::nullopt);
      }
      Check(!weak.expired(), "active window hook was destroyed during removal");
    };
    if (show) {
      manager.SetWillShowHook(std::move(callback));
    } else {
      manager.SetWillHideHook(std::move(callback));
    }
    captured.reset();
    if (show) {
      manager.HandleWillShow(0);
    } else {
      manager.HandleWillHide(0);
    }
    Check(weak.expired(), "removed window hook was retained");
  }
}

void TestUnregisterClearsRetainedShortcut() {
  auto& manager = nativeapi::ShortcutManager::GetInstance();
  int calls = 0;
  auto captured = std::make_shared<int>(42);
  std::weak_ptr<int> weak = captured;
  auto shortcut = manager.Register("Ctrl+Alt+Shift+F12", [&, captured] { ++calls; });
  captured.reset();
  if (!shortcut) {
    std::cout << "SKIP: platform could not register a global shortcut" << std::endl;
    return;
  }
  shortcut->Invoke();
  Check(calls == 1, "registered shortcut callback did not run");
  Check(manager.Unregister(shortcut->GetId()), "shortcut unregister failed");
  shortcut->Invoke();
  Check(calls == 1, "retained shortcut invoked after unregister");
  Check(weak.expired(), "unregistered shortcut retained its captured state");
}
}  // namespace

int main() {
  std::cout << "callback_lifetime_test" << std::endl;

  TestShortcutSelfReplacement();
  TestHookSelfRemoval();
  TestUnregisterClearsRetainedShortcut();
  if (g_failures > 0) {
    std::cerr << g_failures << " check(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "All checks passed." << std::endl;
  return EXIT_SUCCESS;
}
