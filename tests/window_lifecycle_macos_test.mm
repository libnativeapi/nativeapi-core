// Verifies macOS window lifecycle behaviors carried over in issue #80:
// - Hide() while in fullscreen exits fullscreen before ordering out (#379)
// - Always-on-top suspends NSFloatingWindowLevel across native fullscreen (#351)
// - SetFullScreenable() / IsFullScreenable() toggle collectionBehavior
// - SetResizable() round-trips cleanly (#462)
// - Hidden title bar leaves top-edge content buttons hit-testable (#565)
#include "nativeapi.h"

#import <Cocoa/Cocoa.h>
#include <iostream>

static int failures = 0;

static void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}

@interface FakeFullScreenWindow : NSWindow {
 @public
  BOOL fake_full_screen;
  int toggle_calls;
  int order_out_calls;
  int set_visible_false_calls;
}
@end

@implementation FakeFullScreenWindow
- (NSWindowStyleMask)styleMask {
  NSWindowStyleMask base = [super styleMask];
  if (fake_full_screen) return base | NSWindowStyleMaskFullScreen;
  return base & ~NSWindowStyleMaskFullScreen;
}

- (void)toggleFullScreen:(id)sender {
  (void)sender;
  ++toggle_calls;
}

- (void)orderOut:(id)sender {
  ++order_out_calls;
  [super orderOut:sender];
}

- (void)setIsVisible:(BOOL)flag {
  if (!flag) ++set_visible_false_calls;
  [super setIsVisible:flag];
}
@end

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];

    // 1. Hide() while in fullscreen exits fullscreen first, then orders out on exit (#379).
    {
      auto* host = [[FakeFullScreenWindow alloc]
          initWithContentRect:NSMakeRect(100, 100, 400, 300)
                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable |
                              NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                      backing:NSBackingStoreBuffered
                        defer:NO];
      host.releasedWhenClosed = NO;
      nativeapi::Window window((__bridge void*)host);

      host->fake_full_screen = YES;
      window.Hide();
      Check(host->toggle_calls == 1, "Hide() in fullscreen calls toggleFullScreen");
      Check(host->order_out_calls == 0 && host->set_visible_false_calls == 0,
            "Hide() in fullscreen defers orderOut until exit notification");

      host->fake_full_screen = NO;
      [[NSNotificationCenter defaultCenter]
          postNotificationName:NSWindowDidExitFullScreenNotification
                        object:host];
      Check(host->order_out_calls == 1 && host->set_visible_false_calls == 1,
            "DidExitFullScreen completes deferred hide");

      // Show() before exit cancels pending hide.
      host->fake_full_screen = YES;
      host->toggle_calls = 0;
      host->order_out_calls = 0;
      host->set_visible_false_calls = 0;
      window.Hide();
      Check(host->toggle_calls == 1, "second Hide() in fullscreen calls toggleFullScreen");
      window.ShowInactive();
      host->fake_full_screen = NO;
      [[NSNotificationCenter defaultCenter]
          postNotificationName:NSWindowDidExitFullScreenNotification
                        object:host];
      Check(host->order_out_calls == 0 && host->set_visible_false_calls == 0,
            "ShowInactive() cancels pending deferred hide");
      [host close];
    }

    // 2. Always-on-top + fullscreen drops window level during fullscreen and restores after (#351).
    {
      auto* host = [[FakeFullScreenWindow alloc]
          initWithContentRect:NSMakeRect(100, 100, 400, 300)
                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable |
                              NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                      backing:NSBackingStoreBuffered
                        defer:NO];
      host.releasedWhenClosed = NO;
      nativeapi::Window window((__bridge void*)host);

      window.SetAlwaysOnTop(true);
      Check(window.IsAlwaysOnTop() && host.level == NSFloatingWindowLevel,
            "SetAlwaysOnTop(true) raises window level outside fullscreen");

      window.SetFullScreen(true);
      Check(host->toggle_calls == 1, "SetFullScreen(true) calls toggleFullScreen");
      Check(host.level == NSNormalWindowLevel,
            "SetFullScreen(true) suspends floating level so menu bar/Dock hide");
      Check(window.IsAlwaysOnTop(),
            "IsAlwaysOnTop() stays true while floating level is suspended for fullscreen");

      host->fake_full_screen = YES;
      host->fake_full_screen = NO;
      [[NSNotificationCenter defaultCenter]
          postNotificationName:NSWindowDidExitFullScreenNotification
                        object:host];
      Check(host.level == NSFloatingWindowLevel,
            "DidExitFullScreen restores floating window level");

      // Calling SetAlwaysOnTop(true) while already in fullscreen also defers level raise.
      window.SetAlwaysOnTop(false);
      Check(!window.IsAlwaysOnTop() && host.level == NSNormalWindowLevel,
            "SetAlwaysOnTop(false) resets level");
      host->fake_full_screen = YES;
      window.SetAlwaysOnTop(true);
      Check(window.IsAlwaysOnTop() && host.level == NSNormalWindowLevel,
            "SetAlwaysOnTop(true) during fullscreen keeps NSNormalWindowLevel");
      host->fake_full_screen = NO;
      [[NSNotificationCenter defaultCenter]
          postNotificationName:NSWindowDidExitFullScreenNotification
                        object:host];
      Check(host.level == NSFloatingWindowLevel,
            "DidExitFullScreen applies always-on-top requested during fullscreen");

      // User-initiated fullscreen entry (green button) also suspends floating level.
      [[NSNotificationCenter defaultCenter]
          postNotificationName:NSWindowWillEnterFullScreenNotification
                        object:host];
      Check(host.level == NSNormalWindowLevel,
            "WillEnterFullScreen drops floating level for user-initiated fullscreen");
      [[NSNotificationCenter defaultCenter]
          postNotificationName:NSWindowDidExitFullScreenNotification
                        object:host];
      Check(host.level == NSFloatingWindowLevel,
            "DidExitFullScreen restores floating level after user-initiated fullscreen");
      [host close];
    }

    // 3. SetFullScreenable() / IsFullScreenable() and SetResizable() (#462).
    {
      auto* host = [[NSWindow alloc]
          initWithContentRect:NSMakeRect(100, 100, 400, 300)
                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable |
                              NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                      backing:NSBackingStoreBuffered
                        defer:NO];
      host.releasedWhenClosed = NO;
      nativeapi::Window window((__bridge void*)host);

      Check(window.IsFullScreenable(), "resizable window is full-screenable by default");
      window.SetFullScreenable(false);
      Check(!window.IsFullScreenable(), "SetFullScreenable(false) clears full-screenable");
      window.SetFullScreenable(true);
      Check(window.IsFullScreenable(), "SetFullScreenable(true) sets full-screenable");

      window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
      window.SetResizable(false);
      Check(!window.IsResizable(), "SetResizable(false) removes resizable mask");
      window.SetResizable(true);
      Check(window.IsResizable(), "SetResizable(true) restores resizable mask");

      // 4. Hidden title bar: button at the very top of contentView is hit-testable (#565).
      NSView* content = host.contentView;
      auto* top_button = [[NSButton alloc]
          initWithFrame:NSMakeRect(10, NSHeight(content.bounds) - 30, 80, 24)];
      [content addSubview:top_button];
      NSView* frame_view = content.superview;
      NSPoint button_center = [frame_view convertPoint:NSMakePoint(40, 12) fromView:top_button];
      NSView* hit = [frame_view hitTest:button_center];
      std::cout << "INFO top-button hitTest class: "
                << (hit ? NSStringFromClass([hit class]).UTF8String : "(nil)") << std::endl;
      Check(hit == top_button, "top-edge button is hit-testable with hidden title bar");
      [host close];
    }
  }
  return failures == 0 ? 0 : 1;
}
