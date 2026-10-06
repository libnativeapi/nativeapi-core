// Native policy and C ABI regression; no window is shown and no input is sent.
#include "nativeapi.h"
#include <iostream>
#ifdef __APPLE__
#import <AppKit/AppKit.h>
#else
#include <gtk/gtk.h>
#endif

int main(int argc, char** argv) {
#ifndef __APPLE__
  if (!gtk_init_check(&argc, &argv)) return 77;
#endif
  using namespace nativeapi;
  Application::GetInstance();
  Window window;
#ifdef __APPLE__
  constexpr bool supported = true;
  NSWindow* native = static_cast<NSWindow*>(window.GetNativeObject());
#else
  constexpr bool supported = false;
#endif
  if (Window::IsContentProtectionSupported() != supported ||
      native_window_is_content_protection_supported() != supported) return 1;
  if (window.IsContentProtected()) return 1;
  for (bool enabled : {true, false, true}) {
    if (window.SetContentProtection(enabled) != supported ||
        window.IsContentProtected() != (supported && enabled)) return 1;
#ifdef __APPLE__
    if (native.sharingType != (enabled ? NSWindowSharingNone : NSWindowSharingReadOnly)) return 1;
    Window wrapper(native);
    if (wrapper.IsContentProtected() != enabled || !wrapper.SetContentProtection(!enabled) ||
        window.IsContentProtected() != !enabled) return 1;
    // Native changes are immediately visible through both wrappers.
    native.sharingType = NSWindowSharingReadOnly;
    if (window.IsContentProtected() || wrapper.IsContentProtected()) return 1;
#endif
  }
  const auto handle = native_window_create_with_native_window(window.GetNativeObject());
  if (!handle || native_window_set_content_protection(handle, true) != supported ||
      native_window_is_content_protected(handle) != supported ||
      window.IsContentProtected() != supported) return 1;
  if (native_window_set_content_protection(handle, false) != supported ||
      native_window_is_content_protected(handle) || window.IsContentProtected()) return 1;
  native_window_free(handle);
  if (native_window_set_content_protection(0, true) || native_window_is_content_protected(0)) return 1;
  std::cout << "PASS native sharing policy, shared wrappers, C ABI and unsupported contract\n";
}
