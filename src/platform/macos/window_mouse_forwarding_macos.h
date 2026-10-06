#pragma once

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

static const void* kNativeApiMouseForwardingKey = &kNativeApiMouseForwardingKey;

// Owned by the native window, independent of temporary Window wrappers. Sampling
// avoids a global event monitor and its Accessibility/Input Monitoring permission.
@interface NativeApiMouseForwarding : NSObject {
 @public
  __unsafe_unretained NSWindow* window;
  NSTimer* timer;
  NSView* tracked;
  NSPoint last;
}
- (void)start;
- (void)stop;
- (void)tick;
- (void)closed:(NSNotification*)notification;
@end

@implementation NativeApiMouseForwarding
- (void)stop {
  [timer invalidate];
#if !__has_feature(objc_arc)
  [timer release];
  [tracked release];
#endif
  timer = nil;
  tracked = nil;
}
- (void)closed:(NSNotification*)notification { [self stop]; }
- (void)dealloc {
  [self stop];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
#if !__has_feature(objc_arc)
  [super dealloc];
#endif
}
- (void)start {
  if (timer) return;
  __unsafe_unretained NativeApiMouseForwarding* owner = self;
  timer = [NSTimer timerWithTimeInterval:0.016 repeats:YES block:^(NSTimer*) {
    NativeApiMouseForwarding* active = owner;
#if !__has_feature(objc_arc)
    [active retain];
#endif
    [active tick];
#if !__has_feature(objc_arc)
    [active release];
#endif
  }];
#if !__has_feature(objc_arc)
  [timer retain];
#endif
  [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
}
- (void)tick {
  NSWindow* active_window = window;
#if !__has_feature(objc_arc)
  [[active_window retain] autorelease];
#endif
  if (!active_window.ignoresMouseEvents) { [self stop]; return; }
  NSPoint local = [active_window convertPointFromScreen:NSEvent.mouseLocation];
  NSView* content = active_window.contentView;
  const NSPoint in_content = [content convertPoint:local fromView:nil];
  NSView* receiver = active_window.visible && !active_window.miniaturized &&
                     NSPointInRect(in_content, content.bounds) ? [content hitTest:in_content] : nil;
#if !__has_feature(objc_arc)
  // A handler can close the window, replace its content or disable forwarding.
  [[receiver retain] autorelease];
#endif
  if (receiver != tracked) {
    NSEvent* exit = [NSEvent enterExitEventWithType:NSEventTypeMouseExited location:local
                                   modifierFlags:NSEvent.modifierFlags
                                       timestamp:NSProcessInfo.processInfo.systemUptime
                                    windowNumber:active_window.windowNumber context:nil eventNumber:0
                                  trackingNumber:0 userData:nullptr];
    [tracked mouseExited:exit];
    if (!timer || !active_window.ignoresMouseEvents) return;
#if !__has_feature(objc_arc)
    [receiver retain];
    [tracked release];
#endif
    tracked = receiver;
    NSEvent* enter = [NSEvent enterExitEventWithType:NSEventTypeMouseEntered location:local
                                    modifierFlags:NSEvent.modifierFlags
                                        timestamp:NSProcessInfo.processInfo.systemUptime
                                     windowNumber:active_window.windowNumber context:nil eventNumber:0
                                   trackingNumber:0 userData:nullptr];
    [receiver mouseEntered:enter];
    if (!timer || !active_window.ignoresMouseEvents) return;
    last = NSMakePoint(CGFLOAT_MAX, CGFLOAT_MAX);
  }
  if (receiver && !NSEqualPoints(local, last)) {
    NSEvent* movement = [NSEvent mouseEventWithType:NSEventTypeMouseMoved location:local
                                    modifierFlags:NSEvent.modifierFlags
                                        timestamp:NSProcessInfo.processInfo.systemUptime
                                     windowNumber:active_window.windowNumber context:nil eventNumber:0
                                       clickCount:0 pressure:0];
    [receiver mouseMoved:movement];
  }
  last = local;
}
@end

static NativeApiMouseForwarding* NativeApiMouseForwarder(NSWindow* window, bool create) {
  auto* forwarder = (NativeApiMouseForwarding*)objc_getAssociatedObject(window, kNativeApiMouseForwardingKey);
  if (!forwarder && create) {
    forwarder = [NativeApiMouseForwarding new];
    forwarder->window = window;
    objc_setAssociatedObject(window, kNativeApiMouseForwardingKey, forwarder, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [[NSNotificationCenter defaultCenter] addObserver:forwarder selector:@selector(closed:)
                                                 name:NSWindowWillCloseNotification object:window];
#if !__has_feature(objc_arc)
    [forwarder release];
#endif
  }
  return forwarder;
}
