#pragma once

// Focus() / Blur() for an NSWindow, shared by every Window wrapper of it.
//
// Focus() and Show() remember where the keyboard focus was before they took
// it, and Blur() hands it back: to the previous window of this app when that
// one is still visible, otherwise to the previously active application. The
// target lives on the NSWindow as an associated dictionary so this file works
// under both ARC (Dart build hook) and manual reference counting (CMake). The
// same-app window is kept by window number, never retained, so a closed window
// is simply not found again.

static const void* kNativeApiFocusReturnTarget = &kNativeApiFocusReturnTarget;
static NSString* const kNativeApiFocusReturnWindow = @"window";
static NSString* const kNativeApiFocusReturnApplication = @"application";

// The application that was in front when any window of this app last took the
// focus from it: the fallback when a window's own target is gone.
static NSRunningApplication* g_native_api_last_other_application = nil;

static void NativeApiSetLastOtherApplication(NSRunningApplication* application) {
#if !__has_feature(objc_arc)
  [application retain];
  [g_native_api_last_other_application release];
#endif
  g_native_api_last_other_application = application;
}

static void NativeApiForgetFocusReturnTarget(NSWindow* window) {
  objc_setAssociatedObject(window, kNativeApiFocusReturnTarget, nil,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void NativeApiRememberFocusReturnTarget(NSWindow* window) {
  if (!window || ![window canBecomeKeyWindow]) return;
  NSApplication* app = [NSApplication sharedApplication];
  NSRunningApplication* current = [NSRunningApplication currentApplication];
  NSRunningApplication* foreground = [[NSWorkspace sharedWorkspace] frontmostApplication];
  NSDictionary* target = nil;
  if (foreground && foreground.processIdentifier != current.processIdentifier) {
    target = @{kNativeApiFocusReturnApplication : foreground};
    NativeApiSetLastOtherApplication(foreground);
  } else {
    NSWindow* key = [app keyWindow];
    // Already focused (a repeated Focus()/Show()): keep the earlier target.
    if (!key || key == window) return;
    target = @{kNativeApiFocusReturnWindow : @([key windowNumber])};
  }
  objc_setAssociatedObject(window, kNativeApiFocusReturnTarget, target,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void NativeApiActivateApplication() {
  NSApplication* app = [NSApplication sharedApplication];
  if (@available(macOS 14.0, *)) {
    [app activate];
  } else {
    [app activateIgnoringOtherApps:YES];
  }
}

static void NativeApiFocusWindow(NSWindow* window, bool non_activating) {
  if (!window || ![window canBecomeKeyWindow]) return;
  NativeApiRememberFocusReturnTarget(window);
  if ([window isMiniaturized]) [window deminiaturize:nil];
  // A nonactivating panel takes the keyboard without activating this app.
  if (!non_activating) NativeApiActivateApplication();
  [window makeKeyAndOrderFront:nil];
}

static bool NativeApiActivateOtherApplication(NSRunningApplication* other) {
  NSRunningApplication* current = [NSRunningApplication currentApplication];
  if (!other || other.terminated || other.processIdentifier == current.processIdentifier)
    return false;
  // macOS 14+ activation is cooperative: yield explicitly first, or the other
  // app's request may be declined. Do not bring all of its windows forward.
  if (@available(macOS 14.0, *)) {
    [[NSApplication sharedApplication] yieldActivationToApplication:other];
  }
  return [other activateWithOptions:0];
}

static void NativeApiBlurWindow(NSWindow* window) {
  // Blurring a window without the keyboard focus must not move focus at all.
  if (!window || ![window isKeyWindow]) return;
  NSApplication* app = [NSApplication sharedApplication];
  NSDictionary* target = objc_getAssociatedObject(window, kNativeApiFocusReturnTarget);
  NSNumber* number = target[kNativeApiFocusReturnWindow];
  NSRunningApplication* other = target[kNativeApiFocusReturnApplication];
  NativeApiForgetFocusReturnTarget(window);

  if (number) {
    NSWindow* previous = [app windowWithWindowNumber:[number integerValue]];
    if (previous && previous != window && [previous isVisible] && ![previous isMiniaturized] &&
        [previous canBecomeKeyWindow]) {
      [previous makeKeyAndOrderFront:nil];
      return;
    }
  }
  if (![app isActive]) {
    // A key nonactivating panel while another app is active: give the
    // keyboard back to that app.
    other = [[NSWorkspace sharedWorkspace] frontmostApplication];
  }
  if (NativeApiActivateOtherApplication(other)) return;
  // No usable target of this window: the app this one took the focus from.
  if (NativeApiActivateOtherApplication(g_native_api_last_other_application)) return;
  // Nothing known. A plain deactivate does not reliably activate another app
  // under macOS 14+ cooperative activation, but it is all that is left.
  if ([app isActive]) [app deactivate];
}
