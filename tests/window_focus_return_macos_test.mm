// Focus() / Blur() against the real window server: Focus() activates this
// process, Blur() hands the keyboard back to the previous window of this app or
// to the previously active application. Needs a logged-on desktop with another
// application in front; it changes the active app briefly and sends no input.
// Not registered with CTest for that reason. Exit 77 = skipped.
#import <Cocoa/Cocoa.h>
#include <functional>
#include <iostream>
#include "../src/window.h"

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}

// Pumps AppKit until |done| holds or |seconds| pass; activation is async.
bool WaitFor(const std::function<bool()>& done, double seconds = 3) {
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
  while (!done()) {
    if ([deadline timeIntervalSinceNow] <= 0) return false;
    NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    if (event) [NSApp sendEvent:event];
  }
  return true;
}

pid_t FrontmostPid() {
  return [[NSWorkspace sharedWorkspace] frontmostApplication].processIdentifier;
}

NSWindow* MakeWindow(CGFloat x) {
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(x, 80, 160, 90)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window.releasedWhenClosed = NO;
  window.title = @"nativeapi focus test";
  return window;
}
}  // namespace

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    const pid_t self = getpid();
    NSRunningApplication* previous = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (!previous || previous.processIdentifier == self) {
      std::cout << "SKIP no other application is in front" << std::endl;
      return 77;
    }
    const pid_t previous_pid = previous.processIdentifier;
    std::cout << "previous app: " << [previous.localizedName UTF8String] << std::endl;

    NSWindow* native_a = MakeWindow(80);
    NSWindow* native_b = MakeWindow(280);
    NSWindow* native_c = MakeWindow(480);
    nativeapi::Window a((__bridge void*)native_a);
    nativeapi::Window b((__bridge void*)native_b);
    nativeapi::Window c((__bridge void*)native_c);

    a.Focus();
    Check(WaitFor([&] { return [NSApp isActive] && a.IsFocused(); }),
          "Focus() activates the app and keys the window");
    Check(FrontmostPid() == self, "this app is frontmost after Focus()");

    b.Focus();
    Check(WaitFor([&] { return b.IsFocused(); }), "Focus() moves focus between own windows");
    b.Focus();  // Repeat: must keep A as B's return target.
    b.Blur();
    Check(WaitFor([&] { return a.IsFocused(); }), "Blur() returns focus to the previous own window");
    Check([NSApp isActive], "returning to an own window keeps the app active");

    c.SetFocusable(false);
    c.Focus();
    WaitFor([] { return false; }, 0.3);
    Check(!c.IsFocused() && a.IsFocused(), "Focus() on a non-focusable window does nothing");
    c.SetFocusable(true);

    b.Blur();
    WaitFor([] { return false; }, 0.3);
    Check(a.IsFocused() && FrontmostPid() == self, "Blur() of an unfocused window moves nothing");

    a.Blur();
    Check(WaitFor([&] { return FrontmostPid() == previous_pid; }),
          "Blur() returns focus to the previously active application");
    Check(!a.IsFocused(), "the blurred window has no keyboard focus");

    a.Blur();
    WaitFor([] { return false; }, 0.3);
    Check(FrontmostPid() == previous_pid, "a second Blur() leaves the other app alone");

    // The previous own window is closed before Blur(): fall back to deactivating,
    // which lets the system activate the app that was in front before.
    a.Focus();
    Check(WaitFor([&] { return [NSApp isActive] && a.IsFocused(); }), "Focus() again from the background");
    c.Focus();
    Check(WaitFor([&] { return c.IsFocused(); }), "focus a third window");
    [native_a close];
    WaitFor([] { return false; }, 0.2);
    c.Blur();
    Check(WaitFor([&] { return FrontmostPid() != self; }),
          "Blur() with a closed previous window gives up activation");
    Check(FrontmostPid() == previous_pid, "the previously active application is in front again");

    [native_b close];
    [native_c close];
  }
  std::cout << (failures ? "FAILED" : "OK") << std::endl;
  return failures ? 1 : 0;
}
