// Dispatches events only to an unshown, test-owned AppKit window. No system
// preferences are written and no input is injected into the user's desktop.
#include "nativeapi.h"
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <iostream>

static int failures;
static NSString* preference;
static NSUserDefaults* defaults;
static id TestStandardDefaults(id, SEL) { return defaults; }
static void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
@interface DoubleClickDefaults : NSUserDefaults
@end
@implementation DoubleClickDefaults
- (NSString*)stringForKey:(NSString*)key {
  if ([key isEqualToString:@"AppleActionOnDoubleClick"]) return preference;
  return [super stringForKey:key];
}
@end
@interface DoubleClickWindow : NSWindow {
 @public
  int zooms, miniaturizations, fills;
  BOOL fill_unavailable;
}
@end
@implementation DoubleClickWindow
- (void)zoom:(id)sender { ++zooms; }
- (void)_zoomFill:(id)sender { ++fills; }
- (BOOL)respondsToSelector:(SEL)selector {
  if (fill_unavailable && selector == NSSelectorFromString(@"_zoomFill:")) return NO;
  return [super respondsToSelector:selector];
}
- (void)miniaturize:(id)sender { ++miniaturizations; }
@end
@interface FlippedBackground : NSView
@end
@implementation FlippedBackground
- (BOOL)isFlipped { return YES; }
@end
@interface HostDispatcherWindow : DoubleClickWindow {
 @public
  int dispatched;
}
@end
@implementation HostDispatcherWindow
- (void)sendEvent:(NSEvent*)event { ++dispatched; }
@end
@interface CustomTitleBar : NSView {
 @public
  native_window_t handle;
  int accepted;
}
@end
@implementation CustomTitleBar
- (void)mouseUp:(NSEvent*)event {
  if (event.clickCount == 2 && native_window_perform_title_bar_double_click(handle)) ++accepted;
}
@end
static void SendRelease(NSWindow* window, NSPoint point, NSInteger count = 2) {
  [window sendEvent:[NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
      location:point modifierFlags:0 timestamp:0 windowNumber:window.windowNumber
      context:nil eventNumber:0 clickCount:count pressure:0]];
}
static void Click(CustomTitleBar* view, NSInteger count) {
  [view mouseUp:[NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
      location:NSMakePoint(80, 80) modifierFlags:0 timestamp:0
      windowNumber:view.window.windowNumber context:nil eventNumber:0 clickCount:count pressure:0]];
}
int main() {
 @autoreleasepool {
  [NSApplication sharedApplication];
  defaults = [[DoubleClickDefaults alloc] init];
  Method method = class_getClassMethod([NSUserDefaults class], @selector(standardUserDefaults));
  IMP original = method_setImplementation(method, reinterpret_cast<IMP>(TestStandardDefaults));
  auto* host = [[DoubleClickWindow alloc] initWithContentRect:NSMakeRect(100, 150, 400, 240)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
      backing:NSBackingStoreBuffered defer:NO];
  host.releasedWhenClosed = NO;
  auto* chrome = [[CustomTitleBar alloc] initWithFrame:NSMakeRect(0, 0, 400, 240)];
  host.contentView = chrome;
  {
    nativeapi::Window window(host);
    chrome->handle = native_window_create_with_native_window(host);
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    Check(!host.isVisible && !window.IsWindowControlButtonsVisible(), "hidden custom title bar without showing a window");
    preference = @"None";
    const NSRect original_frame = host.frame;
    Click(chrome, 1);
    Check(chrome->accepted == 0, "single release is not a double-click");
    Click(chrome, 2);
    Check(chrome->accepted == 1 && host->zooms == 0 && host->miniaturizations == 0 &&
          NSEqualRects(original_frame, host.frame), "None is handled without changing window state");
    preference = @"Minimize"; Click(chrome, 2);
    Check(chrome->accepted == 2 && host->miniaturizations == 1 && host->zooms == 0,
          "changing preference to Minimize affects the next custom gesture");
    window.SetMinimizable(false);
    Check(!window.PerformTitleBarDoubleClick() && host->miniaturizations == 1,
          "disabled minimize rejects the action");
    window.SetMinimizable(true);
    preference = @"Maximize"; Click(chrome, 2);
    Check(host->zooms == 1 && chrome->accepted == 3, "Maximize dispatches AppKit zoom with buttons hidden");
    preference = @"Zoom";
    Check(window.PerformTitleBarDoubleClick() && host->zooms == 2, "legacy Zoom accepted");
    preference = nil;
    Check(window.PerformTitleBarDoubleClick() && host->zooms == 3, "unset preference defaults to zoom");
    window.SetMaximizable(false);
    Check(!window.PerformTitleBarDoubleClick() && host->zooms == 3, "disabled zoom rejects the action");
    preference = @"None";
    Check(window.PerformTitleBarDoubleClick(), "None remains handled with maximize disabled");
    window.SetMaximizable(true);
    preference = @"Fill";
    Check(window.PerformTitleBarDoubleClick() && host->fills == 1 && host->zooms == 3,
          "Fill dispatches its own native action, not zoom");
    host->fill_unavailable = YES;
    Check(!window.PerformTitleBarDoubleClick() && host->fills == 1 && host->zooms == 3,
          "missing native Fill action rejects without fallback");
    host->fill_unavailable = NO;
    window.SetMaximizable(false);
    Check(!window.PerformTitleBarDoubleClick() && host->fills == 1, "disabled maximize rejects Fill");
    window.SetMaximizable(true);
    for (NSString* unsupported in @[@"Unknown"]) {
      preference = unsupported;
      Check(!window.PerformTitleBarDoubleClick() && host->zooms == 3 && host->miniaturizations == 1,
            "unsupported preference never falls back to zoom");
    }
    Check(!native_window_perform_title_bar_double_click(0), "invalid C ABI handle rejected");
    native_window_free(chrome->handle);
    Check(!host.isVisible, "all gestures leave test window unshown");
  }
  // Reproduce the original issue: no custom handler, just a hidden title bar.
  auto* automatic = [[DoubleClickWindow alloc] initWithContentRect:NSMakeRect(100, 150, 400, 240)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
      backing:NSBackingStoreBuffered defer:NO];
  automatic.releasedWhenClosed = NO;
  {
    nativeapi::Window window(automatic);
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    NSPoint caption = NSMakePoint(180, NSHeight(automatic.contentView.frame) - 8);
    preference = @"Maximize";
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    SendRelease(automatic, caption);
    Check(automatic->zooms == 1, "bare hidden title-bar background automatically zooms");
    SendRelease(automatic, NSMakePoint(180, 40));
    SendRelease(automatic, caption, 1);
    Check(automatic->zooms == 1, "ordinary content and single releases do not zoom");
    preference = @"Minimize"; SendRelease(automatic, caption);
    Check(automatic->miniaturizations == 1, "automatic background action follows live Minimize preference");
    preference = @"None"; SendRelease(automatic, caption);
    Check(automatic->miniaturizations == 1 && automatic->zooms == 1, "automatic None has no state change");
    preference = @"Maximize";
    auto* button = [[NSButton alloc] initWithFrame:NSMakeRect(100, caption.y - 6, 100, 12)];
    [automatic.contentView addSubview:button];
    SendRelease(automatic, caption);
    Check(automatic->zooms == 1, "title-bar controls retain their double-click gesture");
    [button removeFromSuperview];
#if !__has_feature(objc_arc)
    [button release];
#endif
    auto* handled = [[CustomTitleBar alloc] initWithFrame:automatic.contentView.bounds];
    handled->handle = native_window_create_with_native_window(automatic);
    automatic.contentView = handled;
    SendRelease(automatic, caption);
    Check(automatic->zooms == 1 + handled->accepted, "custom handler is not combined with automatic zoom");
    const int before_explicit = automatic->zooms;
    Click(handled, 2);
    Check(automatic->zooms == before_explicit + 1, "custom chrome can still perform its explicit action");
    native_window_free(handled->handle);
#if !__has_feature(objc_arc)
    [handled release];
#endif
    auto* flipped = [[FlippedBackground alloc] initWithFrame:automatic.contentView.bounds];
    automatic.contentView = flipped;
    const int before_flipped = automatic->zooms;
    SendRelease(automatic, caption);
    Check(automatic->zooms == before_flipped + 1, "flipped empty background uses the top title-bar band");
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Normal);
    SendRelease(automatic, NSMakePoint(180, NSHeight(automatic.contentView.frame) - 8));
    Check(automatic->zooms == before_flipped + 1, "normal style gives content gestures back to AppKit");
#if !__has_feature(objc_arc)
    [flipped release];
#endif
    Check(!automatic.isVisible, "automatic event regression leaves window unshown");
  }
  [automatic close];
#if !__has_feature(objc_arc)
  [automatic release];
#endif
  auto* unrelated = [[DoubleClickWindow alloc] initWithContentRect:NSMakeRect(100, 150, 400, 240)
      styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
  unrelated.releasedWhenClosed = NO;
  SendRelease(unrelated, NSMakePoint(180, 232));
  Check(unrelated->zooms == 0, "class override is inert on windows without the hidden policy");
  [unrelated close];
  auto* dispatcher = [[HostDispatcherWindow alloc] initWithContentRect:NSMakeRect(100, 150, 400, 240)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
  dispatcher.releasedWhenClosed = NO;
  {
    nativeapi::Window window(dispatcher);
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    SendRelease(dispatcher, NSMakePoint(180, NSHeight(dispatcher.contentView.frame) - 8));
    Check(dispatcher->dispatched == 1 && dispatcher->zooms == 0, "host window event dispatcher keeps its gesture");
  }
  [dispatcher close];
#if !__has_feature(objc_arc)
  [unrelated release]; [dispatcher release];
#endif
  [host close];
  // Also exercise AppKit's real zoom state rather than only recording dispatch.
  auto* native = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 150, 400, 240)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
  native.releasedWhenClosed = NO;
  {
    nativeapi::Window window(native);
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    preference = @"Maximize";
    SendRelease(native, NSMakePoint(180, NSHeight(native.contentView.frame) - 8));
    Check(window.IsMaximized(), "real AppKit automatic zoom works with hidden title bar");
    Check(window.PerformTitleBarDoubleClick() && !window.IsMaximized(), "second native action restores zoom");
    window.SetNonActivating(true);
    SendRelease(native, NSMakePoint(180, NSHeight(native.contentView.frame) - 8));
    Check(window.IsMaximized(), "automatic action survives switching to nonactivating panel");
    window.PerformTitleBarDoubleClick();
    window.SetNonActivating(false);
    SendRelease(native, NSMakePoint(180, NSHeight(native.contentView.frame) - 8));
    Check(window.IsMaximized(), "automatic action survives restoring original runtime class");
    window.PerformTitleBarDoubleClick();
    preference = @"Fill";
    const bool has_fill = [native respondsToSelector:NSSelectorFromString(@"_zoomFill:")];
    const NSRect before_fill = native.frame;
    Check(window.PerformTitleBarDoubleClick() == has_fill, "real AppKit Fill request follows runtime availability");
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:0.4];
    while (deadline.timeIntervalSinceNow > 0)
      [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
    if (has_fill && NSEqualRects(before_fill, native.frame))
      std::cout << "INFO native Fill needs the separate visible-window regression for geometry" << std::endl;
    Check(!native.isVisible, "native actions leave test window unshown");
  }
  [native close];
#if !__has_feature(objc_arc)
  [native release];
#endif
  method_setImplementation(method, original);
#if !__has_feature(objc_arc)
  [chrome release]; [host release]; [defaults release];
#endif
 }
 return failures ? 1 : 0;
}
