#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <iostream>
#include "../src/window.h"
#include "../src/window_manager.h"
#include "../src/window_shape.h"
#include "../src/window_shadow.h"

// Creates real AppKit objects but never orders a window in or sends input.
int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    NSWindow* native = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 200, 200)
                                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    nativeapi::Window window((__bridge void*)native);
    auto shape = std::make_shared<nativeapi::WindowShape>();
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
      std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
      if (!ok)
        ++failures;
    };
    check(nativeapi::Window::IsShapeSupported(), "supported");
    check(!window.SetShape(shape), "reject empty polygon");
    shape->AddPoint({0, 0});
    shape->AddPoint({200, 0});
    shape->AddPoint({100, 200});
    check(!window.SetShape(shape), "reject decorated window");
    window.SetHasShadow(false);
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
    check(!window.HasShadow(), "titlebar change preserves disabled shadow");
    window.SetHasShadow(true);
    check(window.HasShadow(), "enable shadow on rectangle");
    check(!window.SetShape(shape), "reject opaque background");
    window.SetBackgroundColor({0, 0, 0, 0});
    const NSRect original_frame = native.frame;
    check(window.SetShape(shape) && window.IsShaped(), "apply triangle");
    check(!(native.styleMask & NSWindowStyleMaskTitled), "shape has no system rounded frame");
    check(NSEqualRects(native.frame, original_frame), "borderless transition preserves frame");
    check(native.canBecomeKeyWindow, "borderless shape retains keyboard focus");
    CAShapeLayer* mask = (CAShapeLayer*)native.contentView.layer.mask;
    const bool flipped = native.contentView.isFlipped;
    auto contains = [&](double x, double y) {
      return CGPathContainsPoint(
          mask.path, nullptr,
          CGPointMake(x, flipped ? y : native.contentView.bounds.size.height - y), true);
    };
    check(contains(100, 70), "centre inside");
    check(!contains(10, 180), "bottom corner outside (coordinate orientation)");
    shape->Clear();
    check(contains(100, 70), "builder mutation does not change applied path");
    nativeapi::Window second((__bridge void*)native);
    check(second.IsShaped(), "state shared by wrappers");
    check(second.SetShape(nullptr) && !window.IsShaped(), "clear via another wrapper");
    check(window.HasShadow(), "clearing shape preserves rectangle shadow");
    check(!(native.styleMask & NSWindowStyleMaskTitled), "restored rectangle keeps square frame");
    window.SetFocusable(false);
    check(!native.canBecomeKeyWindow, "borderless respects disabled focus");
    window.SetFocusable(true);
    check(native.canBecomeKeyWindow, "borderless focus can be restored");
    NSWindow* untouched = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 100, 100)
                                                    styleMask:NSWindowStyleMaskBorderless
                                                      backing:NSBackingStoreBuffered defer:NO];
    check(!untouched.canBecomeKeyWindow, "focus override leaves other windows unchanged");
    [untouched release];
    second.SetHasShadow(false);
    check(!window.HasShadow(), "shadow state shared by wrappers");
    auto shadow = std::make_shared<nativeapi::WindowShadow>();
    shadow->SetBlurRadius(12);
    shadow->SetColor({20, 40, 200, 100});
    check(window.SetCustomShadow(shadow), "apply custom shadow");
    shadow->SetBlurRadius(3);
    auto applied_shadow = second.GetCustomShadow();
    check(applied_shadow && applied_shadow->GetBlurRadius() == 12, "custom shadow is copied and shared by wrappers");
    applied_shadow->SetBlurRadius(4);
    check(window.GetCustomShadow()->GetBlurRadius() == 12, "getter returns independent copy");
    check(!window.HasShadow(), "custom configuration preserves disabled switch");
    const auto window_count = nativeapi::WindowManager::GetInstance().GetAll().size();
    window.SetHasShadow(true);
    check(nativeapi::WindowManager::GetInstance().GetAll().size() == window_count,
          "shadow helper is excluded from managed windows");
    check(window.HasShadow() && !native.hasShadow, "custom layer replaces AppKit shadow");
    NSImageView* shadow_view = nil;
    for (NSWindow* candidate in NSApp.windows) {
      if ([candidate isKindOfClass:NSClassFromString(@"NativeApiShadowWindow")])
        shadow_view = (NSImageView*)candidate.contentView;
    }
    check(shadow_view && shadow_view.imageScaling == NSImageScaleNone &&
              shadow_view.imageAlignment == NSImageAlignTopLeft,
          "pending shadow pixels keep scale and top-left anchor during resize");
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:2];
    while (!shadow_view.image && deadline.timeIntervalSinceNow > 0)
      [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    check(shadow_view.image != nil, "initial asynchronous shadow published");
    shadow->SetBlurRadius(48);
    check(window.SetCustomShadow(shadow), "change shadow padding");
    bool matched = true;
    for (int i = 0; i < 30; ++i) {
      if (shadow_view.image && !NSEqualSizes(shadow_view.window.frame.size, shadow_view.image.size))
        matched = false;
      [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    check(matched, "shadow bitmap and padding change together without a displaced frame");
    window.SetHasShadow(false);
    check(shadow_view.image == nil, "disabled shadow discards stale visible bitmap");
    window.SetHasShadow(true);
    check(window.SetCustomShadow(nullptr) && !window.GetCustomShadow() && native.hasShadow,
          "clear custom configuration restores system shadow");
    // A queued shadow raster must not recreate a helper after configuration is
    // cleared, even when its completion arrives on a later run-loop iteration.
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
    check(native.childWindows.count == 0, "pending shadow completion cannot resurrect cleared helper");
    auto foreign = [CALayer layer];
    native.contentView.layer.mask = foreign;
    shape->AddPoint({0, 0});
    shape->AddPoint({200, 0});
    shape->AddPoint({100, 200});
    check(!window.SetShape(shape) && native.contentView.layer.mask == foreign,
          "preserve embedding application's mask");
    check(window.SetShape(nullptr) && native.contentView.layer.mask == foreign,
          "clear preserves foreign mask");
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Normal);
    check(native.styleMask & NSWindowStyleMaskTitled, "normal style restores titled frame");
    check(NSEqualRects(native.frame, original_frame), "normal style restores frame geometry");
    return failures ? 1 : 0;
  }
}
