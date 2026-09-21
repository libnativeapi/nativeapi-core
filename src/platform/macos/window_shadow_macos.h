#pragma once

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include "../window_shadow_blur.h"

static const void* kNativeApiCustomShadowKey = &kNativeApiCustomShadowKey;

@interface NativeApiShadowWindow : NSWindow
@end
@implementation NativeApiShadowWindow
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

@interface NativeApiCustomShadow : NSObject {
 @public
  std::shared_ptr<nativeapi::WindowShadow> options;
  BOOL enabled;
  BOOL renderInFlight;
  BOOL needsRender;
  double displayedPadding;
  NSUInteger visibilityGeneration;
  NSWindow* layer;
  __unsafe_unretained NSWindow* target;
}
- (void)update;
- (void)changed:(NSNotification*)notification;
- (void)close;
@end

@implementation NativeApiCustomShadow
- (void)close {
  target = nil;
  needsRender = NO;
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  if (layer) {
    [layer.parentWindow removeChildWindow:layer];
    [layer orderOut:nil];
    [layer close];
#if !__has_feature(objc_arc)
    [layer release];
#endif
    layer = nil;
  }
}
- (void)dealloc {
  [self close];
#if !__has_feature(objc_arc)
  [super dealloc];
#endif
}
- (void)changed:(NSNotification*)notification {
  if ([notification.name isEqualToString:NSWindowWillCloseNotification]) {
    [self close];
    target = nil;
  } else [self update];
}
- (void)update {
  if (!target || !options) return;
  if (!NativeApiWindowIsTitleBarHidden(target)) {
    [layer.parentWindow removeChildWindow:layer];
    [layer orderOut:nil];
    target.hasShadow = enabled;
    return;
  }
  target.hasShadow = NO;
  if (!enabled) {
    ++visibilityGeneration;
    [layer.parentWindow removeChildWindow:layer];
    [layer orderOut:nil];
    ((NSImageView*)layer.contentView).image = nil;
    return;
  }
  const NSRect frame = target.frame;
  const double scale = target.backingScaleFactor;
  const int width = static_cast<int>(std::ceil(frame.size.width * scale));
  const int height = static_cast<int>(std::ceil(frame.size.height * scale));
  nativeapi::WindowShape polygon;
  auto* mask = target.contentView.layer.mask;
  if ([mask isKindOfClass:[CAShapeLayer class]] &&
      [mask.name isEqualToString:@"nativeapi.windowShape"]) {
    struct PathContext { nativeapi::WindowShape* polygon; bool flipped; double height; };
    PathContext context{&polygon, bool(target.contentView.isFlipped), target.contentView.bounds.size.height};
    CGPathApply(((CAShapeLayer*)mask).path, &context, [](void* info, const CGPathElement* element) {
      auto* context = static_cast<PathContext*>(info);
      if (element->type == kCGPathElementMoveToPoint || element->type == kCGPathElementAddLineToPoint) {
        auto p = element->points[0];
        context->polygon->AddPoint({p.x, context->flipped ? p.y : context->height - p.y});
      }
    });
  }
  const int margin = nativeapi::window_shadow::Margin(*options, scale);
  if (!layer) {
    layer = [[NativeApiShadowWindow alloc] initWithContentRect:NSZeroRect styleMask:NSWindowStyleMaskBorderless
                                         backing:NSBackingStoreBuffered defer:NO];
    layer.releasedWhenClosed = NO;
    layer.opaque = NO;
    layer.backgroundColor = NSColor.clearColor;
    layer.hasShadow = NO;
    layer.ignoresMouseEvents = YES;
    NSImageView* view = [[NSImageView alloc] initWithFrame:NSZeroRect];
    // An in-flight bitmap can still use the previous surface dimensions. Keep
    // its logical pixel size and top-left anchor until its replacement arrives;
    // stretching it to new bounds makes the shadow jump at resize completion.
    view.imageScaling = NSImageScaleNone;
    view.imageAlignment = NSImageAlignTopLeft;
    layer.contentView = view;
#if !__has_feature(objc_arc)
    [view release];
#endif
    [target addChildWindow:layer ordered:NSWindowBelow];
  }
  if (layer.parentWindow != target) [target addChildWindow:layer ordered:NSWindowBelow];
  // Keep the published bitmap and its padding together while the next blur is
  // in flight. A new margin with the old image visibly shifts the shadow.
  if (((NSImageView*)layer.contentView).image) {
    [layer setFrame:NSInsetRect(frame, -displayedPadding, -displayedPadding) display:NO];
  }
  if (target.visible && !target.miniaturized) [layer orderWindow:NSWindowBelow relativeTo:target.windowNumber];
  else [layer orderOut:nil];

  // Resizing and replacing a contour can request several updates in one frame.
  // Keep at most one raster job in flight, then render the newest state. AppKit
  // and the Flutter platform thread must never wait for the CPU blur.
  if (renderInFlight) { needsRender = YES; return; }
  renderInFlight = YES;
  needsRender = NO;
  const auto snapshot = *options;
  const NSUInteger generation = visibilityGeneration;
  // Retain the owner through the queued completion, including non-ARC builds.
  // The associated state otherwise keeps only an unsafe target pointer.
  NSWindow* owner = target;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    auto pixels = nativeapi::window_shadow::Render(width, height, polygon, snapshot, scale);
    CGImageRef image = nullptr;
    const int w = width + margin * 2, h = height + margin * 2;
    if (!pixels.empty()) {
      auto space = CGColorSpaceCreateDeviceRGB();
      auto bitmap = CGBitmapContextCreate(pixels.data(), w, h, 8, w * 4, space,
                                         kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
      CGColorSpaceRelease(space);
      if (bitmap) {
        image = CGBitmapContextCreateImage(bitmap);
        CGContextRelease(bitmap);
      }
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      if (self->target == owner && self->layer && self->enabled &&
          self->visibilityGeneration == generation && image) {
        NSImage* nativeImage = [[NSImage alloc] initWithCGImage:image size:NSMakeSize(w / scale, h / scale)];
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        self->displayedPadding = margin / scale;
        [self->layer setFrame:NSInsetRect(owner.frame, -self->displayedPadding,
                                         -self->displayedPadding) display:NO];
        ((NSImageView*)self->layer.contentView).image = nativeImage;
        [self->layer.contentView displayIfNeeded];
        [CATransaction commit];
#if !__has_feature(objc_arc)
        [nativeImage release];
#endif
      }
      if (image) CGImageRelease(image);
      self->renderInFlight = NO;
      if (self->needsRender && self->target) [self update];
    });
  });
}
@end

static NativeApiCustomShadow* NativeApiGetCustomShadow(NSWindow* window) {
  return objc_getAssociatedObject(window, kNativeApiCustomShadowKey);
}
