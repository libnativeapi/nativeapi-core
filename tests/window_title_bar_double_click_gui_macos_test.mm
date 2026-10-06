// Interactive fixture for tools/gui/core_window_title_bar_double_click_macos.py.
// Preference override lives only in this process's volatile argument domain.
#include "nativeapi.h"
#import <Cocoa/Cocoa.h>
#include <iostream>

int main(int argc, char** argv) {
 @autoreleasepool {
  if (argc != 2) return 77;
  NSString* action = [NSString stringWithUTF8String:argv[1]];
  [NSUserDefaults.standardUserDefaults setVolatileDomain:@{@"AppleActionOnDoubleClick":action}
                                                forName:NSArgumentDomain];
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  nativeapi::Application::GetInstance();
  nativeapi::Window window;
  window.SetTitle("NativeAPI Title Bar Double Click");
  window.SetBounds({200, 180, 600, 400});
  window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
  auto* host = (__bridge NSWindow*)window.GetNativeObject();
  std::cout << "PREFERENCE " << action.UTF8String << " FILL_SUPPORTED "
            << [host respondsToSelector:NSSelectorFromString(@"_zoomFill:")] << std::endl;
  window.Show();
  NSTimer* timer = [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer*) {
    NSRect frame = host.frame;
    std::cout << "STATE max=" << host.isZoomed << " min=" << host.isMiniaturized
              << " fullscreen=" << ((host.styleMask & NSWindowStyleMaskFullScreen) != 0)
              << " x=" << frame.origin.x << " y=" << frame.origin.y
              << " w=" << frame.size.width << " h=" << frame.size.height << std::endl;
  }];
  [NSApp run];
  [timer invalidate];
 }
 return 0;
}
