#include <iostream>
#include "../src/foundation/dispatcher.h"
#include "nativeapi.h"

extern "C" int wasm_c_smoke_test(void);

int main() {
  using namespace nativeapi;
  auto check = [](bool condition, const char* message) {
    if (!condition)
      std::cerr << message << '\n';
    return condition;
  };
  bool ok = check(wasm_c_smoke_test(), "C ABI smoke test failed");
  Window window;
  Window wrapped(reinterpret_cast<void*>(1));
  ok &= check(!window.GetNativeObject() && !wrapped.GetNativeObject() &&
                  window.GetId() == IdAllocator::kInvalidId && !window.Close() &&
                  !window.IsVisible() && !Window::IsDockingSupported(),
              "Desktop windows must remain unavailable");
  ok &= check(WindowManager::GetInstance().GetAll().empty() &&
                  DisplayManager::GetInstance().GetAll().empty(),
              "No synthetic windows or displays should be reported");
  ok &= check(!View::IsSupported() && !FileDialog::IsSupported() && !SecureStorage::IsAvailable() &&
                  !LaunchAtLogin::IsSupported() && !NotificationManager::IsSupported() &&
                  !TrayManager::GetInstance().IsSupported(),
              "Desktop services must report unavailable");
  bool called = false;
  Clipboard::GetInstance().ReadText(
      [&](bool success, std::optional<std::string> text) { called = !success && !text; });
  ok &= check(called, "Unsupported clipboard reads must complete with failure");
  ok &= check(!IsMainThreadDispatchSupported() && !RunMainThreadLoopFor(1),
              "No native event loop should be advertised");
  bool dispatched = false;
  SetMainThreadDispatcher(
      [&](std::function<void()> fn) {
        fn();
        return true;
      },
      [] { return true; });
  ok &= check(RunOnMainThread([&] { dispatched = true; }) && dispatched,
              "Embedder dispatcher must remain usable");
  SetMainThreadDispatcher({}, {});
  return ok ? 0 : 1;
}
