// AppKit responder delivery driven by an in-process pointer stub. No window shown
// and no system pointer moved; the public API's actual run-loop sampler is tested.
#include "nativeapi.h"
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <iostream>

static NSPoint pointer;
static NSPoint TestMouseLocation(id, SEL) { return pointer; }
static int failures = 0;
static bool disable_on_move = false;
static native_window_t test_handle = 0;
static void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
static void Settle() {
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:0.08];
  while (deadline.timeIntervalSinceNow > 0)
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
}
@interface TestForwardWindow : NSWindow
@end
@implementation TestForwardWindow
- (BOOL)isVisible { return YES; }  // No orderFront: only the sampler's visibility gate.
@end
@interface TestMouseView : NSView {
 @public
  int movements, entries, exits, clicks, scrolls;
  NSPoint latest;
}
@end
@implementation TestMouseView
- (void)mouseMoved:(NSEvent*)event {
  ++movements; latest = event.locationInWindow;
  if (disable_on_move) {
    disable_on_move = false;
    native_window_set_ignore_mouse_events(test_handle, false, false);
  }
}
- (void)mouseEntered:(NSEvent*)event { ++entries; }
- (void)mouseExited:(NSEvent*)event { ++exits; }
- (void)mouseDown:(NSEvent*)event { ++clicks; }
- (void)scrollWheel:(NSEvent*)event { ++scrolls; }
@end

int main() {
 @autoreleasepool {
  [NSApplication sharedApplication];
  auto* host = [[TestForwardWindow alloc] initWithContentRect:NSMakeRect(100, 150, 240, 120)
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
  host.releasedWhenClosed = NO;
  auto* content = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 240, 120)];
  auto* first = [[TestMouseView alloc] initWithFrame:NSMakeRect(0, 0, 120, 120)];
  auto* second = [[TestMouseView alloc] initWithFrame:NSMakeRect(120, 0, 120, 120)];
  [content addSubview:first]; [content addSubview:second]; host.contentView = content;
  Method method = class_getClassMethod([NSEvent class], @selector(mouseLocation));
  IMP original = method_setImplementation(method, reinterpret_cast<IMP>(TestMouseLocation));
  {
    nativeapi::Window window(host);
    test_handle = native_window_create_with_native_window(host);
    Check(nativeapi::Window::IsMouseMoveForwardingSupported() && native_window_is_mouse_move_forwarding_supported(),
          "forwarding capability in C++ and C ABI");
    pointer = [host convertPointToScreen:NSMakePoint(25, 35)];
    Check(window.SetIgnoreMouseEvents(true, true) && host.ignoresMouseEvents,
          "ignore with forwarding applies native pass-through");
    Settle();
    Check(first->entries == 1 && first->movements == 1 && second->movements == 0,
          "actual timer forwards enter and movement to nested content");
    Check(NSEqualPoints(first->latest, NSMakePoint(25, 35)), "logical window-local coordinates");
    Settle();
    Check(first->movements == 1, "stationary pointer emits no duplicate movement");
    pointer = [host convertPointToScreen:NSMakePoint(45, 50)]; Settle();
    Check(first->movements == 2, "movement while clicks pass through");
    pointer = [host convertPointToScreen:NSMakePoint(145, 50)]; Settle();
    Check(first->exits == 1 && second->entries == 1 && second->movements == 1,
          "content transition forwards exit and enter");
    pointer = [host convertPointToScreen:NSMakePoint(300, 50)]; Settle();
    Check(second->exits == 1, "leaving content forwards exit");
    Check(first->clicks == 0 && second->clicks == 0 && first->scrolls == 0 && second->scrolls == 0,
          "button and wheel events not forwarded");
    Check(native_window_is_ignore_mouse_events(test_handle) && native_window_is_mouse_move_forwarding_enabled(test_handle),
          "policy shared by C ABI wrapper");
    Check(native_window_set_ignore_mouse_events(test_handle, false, true) && !host.ignoresMouseEvents &&
          !window.IsMouseMoveForwardingEnabled(), "disabling pass-through disables forwarding");
    const int previous = first->movements;
    pointer = [host convertPointToScreen:NSMakePoint(35, 45)]; Settle();
    Check(first->movements == previous, "sampler stops after disabling");
    Check(window.SetIgnoreMouseEvents(true) && !window.IsMouseMoveForwardingEnabled(),
          "default forward=false retains ordinary pass-through");
    Settle(); Check(first->movements == previous, "plain pass-through emits no movement");
    Check(window.SetIgnoreMouseEvents(true, true), "enable forwarding again");
    disable_on_move = true; Settle();
    Check(!window.IsIgnoreMouseEvents() && !window.IsMouseMoveForwardingEnabled(),
          "content can disable pass-through during forwarded movement");
    Check(window.SetIgnoreMouseEvents(true, true), "enable before wrapper release");
    native_window_free(test_handle); test_handle = 0;
    pointer = [host convertPointToScreen:NSMakePoint(55, 65)]; Settle();
    Check(window.IsMouseMoveForwardingEnabled() && first->movements > previous,
          "native policy survives release of another wrapper");
    [host close]; Settle();
    Check(!window.IsMouseMoveForwardingEnabled(), "closing native window stops sampler");
  }
  method_setImplementation(method, original);
#if !__has_feature(objc_arc)
  [first release]; [second release]; [content release]; [host release];
#endif
  std::cout << (failures ? "FAILED" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
 }
}
