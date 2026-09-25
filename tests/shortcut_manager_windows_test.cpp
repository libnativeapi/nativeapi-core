// ShortcutManager must actually register global hotkeys with the OS on Windows.
// RegisterHotKey only accepts a window owned by the calling thread, and the
// backend's window lives on its own hotkey thread; registering from the caller's
// thread failed every time with ERROR_WINDOW_OF_OTHER_THREAD.
//
// Probe: while nativeapi holds a hotkey, registering the same combination
// directly must fail with ERROR_HOTKEY_ALREADY_REGISTERED; after Unregister it
// must succeed.

#include <windows.h>

#include <cstdlib>
#include <iostream>

#include "../src/shortcut_manager.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    ++g_failures;
  }
}

constexpr UINT kModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
constexpr UINT kKey = VK_F9;

// Whether the OS reports the combination as taken. Leaves it unregistered.
bool IsHotKeyTaken(HWND hwnd) {
  if (RegisterHotKey(hwnd, 1, kModifiers, kKey)) {
    UnregisterHotKey(hwnd, 1);
    return false;
  }
  return GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED;
}

}  // namespace

int main() {
  std::cout << "shortcut_manager_windows_test" << std::endl;

  HWND probe = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                               nullptr, nullptr);
  if (!probe || IsHotKeyTaken(probe)) {
    std::cout << "SKIP: Ctrl+Alt+Shift+F9 is held by another application" << std::endl;
    return EXIT_SUCCESS;
  }

  auto& manager = nativeapi::ShortcutManager::GetInstance();
  for (int round = 0; round < 3; ++round) {
    auto shortcut = manager.Register("Ctrl+Alt+Shift+F9", [] {});
    Check(shortcut != nullptr, "Register failed");
    if (!shortcut) {
      break;
    }
    Check(IsHotKeyTaken(probe), "registered hotkey is not held by the OS");
    Check(manager.Unregister(shortcut->GetId()), "Unregister failed");
    Check(!IsHotKeyTaken(probe), "unregistered hotkey is still held by the OS");
  }

  DestroyWindow(probe);
  if (g_failures > 0) {
    std::cerr << g_failures << " check(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "All checks passed." << std::endl;
  return EXIT_SUCCESS;
}
