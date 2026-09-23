#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../../window.h"
#include "../../window_manager.h"
#include "../../window_registry.h"
#include "coordinate_utils_macos.h"

// Forward declaration for the delegate
@class NativeAPIWindowManagerDelegate;

// External declaration of kWindowIdKey (defined in window_macos.mm)
extern const void* kWindowIdKey;
// Attaches a window that became visible to its pending parent (window_macos.mm)
void NativeApiAttachPendingParentWindow(NSWindow* window);

namespace nativeapi {

// Private implementation to hide Objective-C details
class WindowManager::Impl {
 public:
  Impl(WindowManager* manager);
  ~Impl();
  void StartEventListening();
  void StopEventListening();
  void OnWindowEvent(NSWindow* window, const std::string& event_type);

 private:
  WindowManager* manager_;
  NativeAPIWindowManagerDelegate* delegate_;

  // Last zoom state reported per window, to turn resizes into maximized/restored
  std::unordered_map<WindowId, bool> maximized_;
  // Last top-left corner seen per window, to notice moves that come as resizes
  std::unordered_map<WindowId, Point> positions_;
  // Windows seen on screen, which is what WindowCreatedEvent and
  // WindowClosedEvent are about
  std::unordered_set<WindowId> shown_;

  // Optional pre-show/hide hooks
  std::optional<WindowManager::WindowWillShowHook> will_show_hook_;
  std::optional<WindowManager::WindowWillHideHook> will_hide_hook_;

  friend class WindowManager;
};

// The Objective-C notification delegate lives outside the class and cannot name
// the private WindowManager::Impl, so it dispatches through this trampoline.
// Impl::StartEventListening() installs it alongside the delegate.
using WindowEventTrampoline = void (*)(void* impl, NSWindow* window, const char* event_type);
static WindowEventTrampoline g_window_event_trampoline = nullptr;

}  // namespace nativeapi

// MARK: - NSWindow Swizzling

// Swizzled implementations call into WindowManager hooks, then forward to original implementations
@interface NSWindow (NativeAPISwizzle)
- (void)na_swizzled_makeKeyAndOrderFront:(id)sender;
- (void)na_swizzled_orderOut:(id)sender;
@end

@implementation NSWindow (NativeAPISwizzle)

- (void)na_swizzled_makeKeyAndOrderFront:(id)sender {
  // Resolve window id and handle hook if present
  if (nativeapi::WindowManager::GetInstance().HasWillShowHook()) {
    auto windows = nativeapi::WindowManager::GetInstance().GetAll();
    for (const auto& window : windows) {
      if (window->GetNativeObject() == (__bridge void*)self) {
        nativeapi::WindowManager::GetInstance().HandleWillShow(window->GetId());
        // Hook handles all logic; never call original here
        return;
      }
    }
  }
  // No window found in registry, call original implementation (swapped)
  [self na_swizzled_makeKeyAndOrderFront:sender];
}

- (void)na_swizzled_orderOut:(id)sender {
  // Resolve window id and handle hook if present
  if (nativeapi::WindowManager::GetInstance().HasWillHideHook()) {
    auto windows = nativeapi::WindowManager::GetInstance().GetAll();
    for (const auto& window : windows) {
      if (window->GetNativeObject() == (__bridge void*)self) {
        nativeapi::WindowManager::GetInstance().HandleWillHide(window->GetId());
        return;
      }
    }
  }
  // No window found in registry, call original implementation (swapped)
  [self na_swizzled_orderOut:sender];
}

@end

static void NativeAPIInstallNSWindowWillShowSwizzleOnce() {
  static dispatch_once_t onceTokenShow;
  dispatch_once(&onceTokenShow, ^{
    Class cls = [NSWindow class];
    SEL originalSel = @selector(makeKeyAndOrderFront:);
    SEL swizzledSel = @selector(na_swizzled_makeKeyAndOrderFront:);
    Method original = class_getInstanceMethod(cls, originalSel);
    Method swizzled = class_getInstanceMethod(cls, swizzledSel);
    if (original && swizzled) {
      method_exchangeImplementations(original, swizzled);
    }
  });
}

static void NativeAPIInstallNSWindowWillHideSwizzleOnce() {
  static dispatch_once_t onceTokenHide;
  dispatch_once(&onceTokenHide, ^{
    Class cls = [NSWindow class];
    SEL originalSel = @selector(orderOut:);
    SEL swizzledSel = @selector(na_swizzled_orderOut:);
    Method original = class_getInstanceMethod(cls, originalSel);
    Method swizzled = class_getInstanceMethod(cls, swizzledSel);
    if (original && swizzled) {
      method_exchangeImplementations(original, swizzled);
    }
  });
}

// Objective-C delegate class to handle NSWindow notifications
@interface NativeAPIWindowManagerDelegate : NSObject
@property(nonatomic, assign) void* impl;  // Use void* instead of private class
- (instancetype)initWithImpl:(void*)impl;
@end

@implementation NativeAPIWindowManagerDelegate

- (instancetype)initWithImpl:(void*)impl {
  if (self = [super init]) {
    _impl = impl;
  }
  return self;
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
  NSWindow* window = [notification object];
  if (_impl && window && nativeapi::g_window_event_trampoline) {
    nativeapi::g_window_event_trampoline(_impl, window, "focused");
  }
}

- (void)windowDidResignKey:(NSNotification*)notification {
  NSWindow* window = [notification object];
  if (_impl && window && nativeapi::g_window_event_trampoline) {
    nativeapi::g_window_event_trampoline(_impl, window, "blurred");
  }
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
  NSWindow* window = [notification object];
  if (_impl && window && nativeapi::g_window_event_trampoline) {
    nativeapi::g_window_event_trampoline(_impl, window, "minimized");
  }
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
  NSWindow* window = [notification object];
  if (_impl && window && nativeapi::g_window_event_trampoline) {
    nativeapi::g_window_event_trampoline(_impl, window, "restored");
  }
}

- (void)windowDidResize:(NSNotification*)notification {
  NSWindow* window = [notification object];
  if (_impl && window && nativeapi::g_window_event_trampoline) {
    nativeapi::g_window_event_trampoline(_impl, window, "resized");
  }
}

- (void)windowDidMove:(NSNotification*)notification {
  NSWindow* window = [notification object];
  if (_impl && window && nativeapi::g_window_event_trampoline) {
    nativeapi::g_window_event_trampoline(_impl, window, "moved");
  }
}

- (void)windowDidChangeOcclusionState:(NSNotification*)notification {
  // Posted when a window comes on screen, for which AppKit has no notification
  // of its own
  NSWindow* window = notification.object;
  if (_impl && window && nativeapi::g_window_event_trampoline) {
    nativeapi::g_window_event_trampoline(_impl, window, "occlusion");
  }
}

- (void)windowWillClose:(NSNotification*)notification {
  NSWindow* window = [notification object];
  if (_impl && window && nativeapi::g_window_event_trampoline) {
    nativeapi::g_window_event_trampoline(_impl, window, "closing");
  }
}

@end

namespace nativeapi {

// Resolve the WindowId of an NSWindow the same way Window's constructor does:
// reuse the id stored as an associated object, otherwise wrap the NSWindow so
// an id is allocated and attached. Newly seen windows are added to the registry
// so listeners can call WindowManager::Get() straight from the callback.
static WindowId ResolveWindowId(NSWindow* ns_window) {
  if (ns_window == nil || [ns_window isKindOfClass:NSClassFromString(@"NativeApiShadowWindow")]) {
    return IdAllocator::kInvalidId;
  }

  NSNumber* existing_id = objc_getAssociatedObject(ns_window, kWindowIdKey);
  if (existing_id) {
    WindowId window_id = [existing_id unsignedLongLongValue];
    if (WindowRegistry::GetInstance().Get(window_id)) {
      return window_id;
    }
  }

  auto window = std::make_shared<Window>((__bridge void*)ns_window);
  WindowId window_id = window->GetId();
  if (window_id != IdAllocator::kInvalidId && !WindowRegistry::GetInstance().Get(window_id)) {
    WindowRegistry::GetInstance().Add(window_id, window);
  }
  return window_id;
}

WindowManager::Impl::Impl(WindowManager* manager) : manager_(manager), delegate_(nullptr) {}

WindowManager::Impl::~Impl() {
  StopEventListening();
}

void WindowManager::Impl::StartEventListening() {
  if (!delegate_) {
    g_window_event_trampoline = [](void* impl, NSWindow* window, const char* event_type) {
      static_cast<Impl*>(impl)->OnWindowEvent(window, event_type);
    };
    delegate_ = [[NativeAPIWindowManagerDelegate alloc] initWithImpl:this];

    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center addObserver:delegate_
               selector:@selector(windowDidBecomeKey:)
                   name:NSWindowDidBecomeKeyNotification
                 object:nil];
    [center addObserver:delegate_
               selector:@selector(windowDidResignKey:)
                   name:NSWindowDidResignKeyNotification
                 object:nil];
    [center addObserver:delegate_
               selector:@selector(windowDidMiniaturize:)
                   name:NSWindowDidMiniaturizeNotification
                 object:nil];
    [center addObserver:delegate_
               selector:@selector(windowDidDeminiaturize:)
                   name:NSWindowDidDeminiaturizeNotification
                 object:nil];
    [center addObserver:delegate_
               selector:@selector(windowDidResize:)
                   name:NSWindowDidResizeNotification
                 object:nil];
    [center addObserver:delegate_
               selector:@selector(windowDidMove:)
                   name:NSWindowDidMoveNotification
                 object:nil];
    [center addObserver:delegate_
               selector:@selector(windowDidChangeOcclusionState:)
                   name:NSWindowDidChangeOcclusionStateNotification
                 object:nil];
    [center addObserver:delegate_
               selector:@selector(windowWillClose:)
                   name:NSWindowWillCloseNotification
                 object:nil];

    // Windows already on screen were not created under our eyes: they emit no
    // WindowCreatedEvent, only the WindowClosedEvent.
    for (NSWindow* window in [[NSApplication sharedApplication] windows]) {
      if ([window isVisible]) {
        WindowId window_id = ResolveWindowId(window);
        if (window_id != IdAllocator::kInvalidId) {
          shown_.insert(window_id);
        }
      }
    }
  }
}

void WindowManager::Impl::StopEventListening() {
  if (delegate_) {
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center removeObserver:delegate_];
    delegate_ = nil;
  }
}

void WindowManager::Impl::OnWindowEvent(NSWindow* window, const std::string& event_type) {
  if (event_type == "closing") {
    // A closing window must not be wrapped and registered just to be told
    // apart: a window that was shown already has its ID.
    NSNumber* existing_id = objc_getAssociatedObject(window, kWindowIdKey);
    if (existing_id) {
      WindowId closing_id = [existing_id unsignedLongLongValue];
      maximized_.erase(closing_id);
      positions_.erase(closing_id);
      if (shown_.erase(closing_id) > 0) {
        WindowClosedEvent event(closing_id);
        manager_->DispatchWindowEvent(event);
      }
    }
    return;
  }

  // The notifications are observed process-wide and AppKit posts them for its
  // own helper windows too (NSMenuBarReplicantWindow resizes on every launch).
  // Report only what WindowManager::GetAll() would return.
  if (![[[NSApplication sharedApplication] windows] containsObject:window]) {
    return;
  }

  WindowId window_id = ResolveWindowId(window);
  if (window_id == IdAllocator::kInvalidId) {
    return;
  }

  // A child window that someone else shows (the embedding framework) still has
  // to be attached to the parent it was given while hidden.
  NativeApiAttachPendingParentWindow(window);

  // Whichever notification arrives first for a window on screen announces it.
  if ([window isVisible] && shown_.insert(window_id).second) {
    WindowCreatedEvent created_event(window_id);
    manager_->DispatchWindowEvent(created_event);
  }

  if (event_type != "resized" && event_type != "moved") {
    // Seed the corner while the frame is still the old one; see "resized".
    CGPoint top_left = NSRectExt::topLeft([window frame]);
    positions_.try_emplace(window_id, Point{top_left.x, top_left.y});
  }

  if (event_type == "focused") {
    WindowFocusedEvent event(window_id);
    manager_->DispatchWindowEvent(event);
  } else if (event_type == "blurred") {
    WindowBlurredEvent event(window_id);
    manager_->DispatchWindowEvent(event);
  } else if (event_type == "minimized") {
    WindowMinimizedEvent event(window_id);
    manager_->DispatchWindowEvent(event);
  } else if (event_type == "restored") {
    WindowRestoredEvent event(window_id);
    manager_->DispatchWindowEvent(event);
  } else if (event_type == "resized") {
    // The frame size, which is what Window::GetSize() returns
    NSRect frame = [window frame];
    Size new_size = {frame.size.width, frame.size.height};
    WindowResizedEvent event(window_id, new_size);
    manager_->DispatchWindowEvent(event);

    // NSWindowDidMoveNotification is not posted when a frame change that
    // resizes also moves the top-left corner (zooming, resizing from the top or
    // left edge), so the move is derived here.
    CGPoint top_left = NSRectExt::topLeft(frame);
    auto position = positions_.find(window_id);
    if (position != positions_.end() &&
        (position->second.x != top_left.x || position->second.y != top_left.y)) {
      WindowMovedEvent moved_event(window_id, {top_left.x, top_left.y});
      manager_->DispatchWindowEvent(moved_event);
    }
    positions_[window_id] = {top_left.x, top_left.y};

    // AppKit has no zoom notification: a zoom is a resize that ends zoomed.
    // Windows being miniaturized or in full screen are not "maximized".
    bool maximized = [window isZoomed] && ![window isMiniaturized] &&
                     !([window styleMask] & NSWindowStyleMaskFullScreen);
    auto it = maximized_.find(window_id);
    bool was_maximized = it != maximized_.end() && it->second;
    if (maximized != was_maximized) {
      maximized_[window_id] = maximized;
      if (maximized) {
        WindowMaximizedEvent maximized_event(window_id);
        manager_->DispatchWindowEvent(maximized_event);
      } else {
        WindowRestoredEvent restored_event(window_id);
        manager_->DispatchWindowEvent(restored_event);
      }
    }
  } else if (event_type == "moved") {
    // Same top-left origin as Window::GetPosition()
    CGPoint top_left = NSRectExt::topLeft([window frame]);
    Point new_position = {top_left.x, top_left.y};
    positions_[window_id] = new_position;
    WindowMovedEvent event(window_id, new_position);
    manager_->DispatchWindowEvent(event);
  }
}

WindowManager::WindowManager() : pimpl_(std::make_unique<Impl>(this)) {
  StartEventListening();
}

WindowManager::~WindowManager() {
  StopEventListening();
}

std::shared_ptr<Window> WindowManager::Get(WindowId id) {
  // First check if it's already in the registry
  auto window = WindowRegistry::GetInstance().Get(id);
  if (window) {
    return window;
  }

  // If not found, ensure all NSWindows are registered and try again
  GetAll();
  return WindowRegistry::GetInstance().Get(id);
}

std::vector<std::shared_ptr<Window>> WindowManager::GetAll() {
  NSArray* ns_windows = [[NSApplication sharedApplication] windows];

  // First, ensure all NSWindows are registered
  for (NSWindow* ns_window in ns_windows) {
    if ([ns_window isKindOfClass:NSClassFromString(@"NativeApiShadowWindow")]) continue;
    // Create or get Window wrapper - this will handle ID assignment via associated object
    auto window = std::make_shared<Window>((__bridge void*)ns_window);
    WindowId window_id = window->GetId();

    // Add to registry if not already present
    if (!WindowRegistry::GetInstance().Get(window_id)) {
      WindowRegistry::GetInstance().Add(window_id, window);
    }
  }

  // Then return all windows from registry (which now includes all NSWindows)
  return WindowRegistry::GetInstance().GetAll();
}

std::shared_ptr<Window> WindowManager::GetCurrent() {
  NSApplication* app = [NSApplication sharedApplication];
  NSArray* ns_windows = [[NSApplication sharedApplication] windows];
  NSWindow* ns_window = [app mainWindow];
  if (ns_window == nil && [ns_windows count] > 0) {
    ns_window = [ns_windows objectAtIndex:0];
  }
  if (ns_window != nil) {
    // First, try to get the window ID from the associated object
    NSNumber* existingIdNumber = objc_getAssociatedObject(ns_window, kWindowIdKey);
    if (existingIdNumber) {
      WindowId window_id = [existingIdNumber unsignedLongLongValue];

      // Try to get the existing Window from registry
      auto existing_window = WindowRegistry::GetInstance().Get(window_id);
      if (existing_window) {
        return existing_window;
      }
    }

    // If not found in registry, create a new Window wrapper
    auto window = std::make_shared<Window>((__bridge void*)ns_window);
    WindowId window_id = window->GetId();

    // Add to registry (temporary solution)
    WindowRegistry::GetInstance().Add(window_id, window);
    return window;
  }
  return nullptr;
}

std::shared_ptr<Window> WindowManager::GetWindowAtPoint(Point point, WindowId excluded_window_id) {
  if ([NSScreen screens].count == 0) {
    return nullptr;
  }
  NSPoint location = NSPointExt::bottomLeft(CGPointMake(point.x, point.y));
  NSApplication* app = [NSApplication sharedApplication];

  // Walk the window server's stack downwards from the top. The walk only
  // continues past windows of this application that are excluded or
  // transparent to the query; anything else ends it.
  NSInteger below = 0;
  for (NSUInteger guard = 0; guard <= app.windows.count; ++guard) {
    NSInteger number = [NSWindow windowNumberAtPoint:location belowWindowWithWindowNumber:below];
    if (number <= 0) {
      return nullptr;
    }
    NSWindow* ns_window = [app windowWithWindowNumber:number];
    if (ns_window == nil) {
      // Another application's window is on top here.
      return nullptr;
    }
    WindowId window_id = ResolveWindowId(ns_window);
    bool skip = window_id == excluded_window_id || !ns_window.isVisible ||
                ns_window.ignoresMouseEvents || ns_window.alphaValue <= 0.0;
    if (!skip) {
      return Get(window_id);
    }
    below = number;
  }
  return nullptr;
}

void WindowManager::SetWillShowHook(std::optional<WindowWillShowHook> hook) {
  pimpl_->will_show_hook_ = std::move(hook);
  if (pimpl_->will_show_hook_) {
    NativeAPIInstallNSWindowWillShowSwizzleOnce();
  }
}

void WindowManager::SetWillHideHook(std::optional<WindowWillHideHook> hook) {
  pimpl_->will_hide_hook_ = std::move(hook);
  if (pimpl_->will_hide_hook_) {
    NativeAPIInstallNSWindowWillHideSwizzleOnce();
  }
}

bool WindowManager::HasWillShowHook() const {
  return pimpl_->will_show_hook_.has_value();
}

bool WindowManager::HasWillHideHook() const {
  return pimpl_->will_hide_hook_.has_value();
}

void WindowManager::HandleWillShow(WindowId id) {
  // The hook may replace or clear itself. Keep this invocation alive.
  auto hook = pimpl_->will_show_hook_;
  if (hook) {
    (*hook)(id);
  }
}

void WindowManager::HandleWillHide(WindowId id) {
  // The hook may replace or clear itself. Keep this invocation alive.
  auto hook = pimpl_->will_hide_hook_;
  if (hook) {
    (*hook)(id);
  }
}

bool WindowManager::CallOriginalShow(WindowId id) {
  auto window = Get(id);
  if (!window) {
    return false;
  }
  void* native = window->GetNativeObject();
  if (!native) {
    return false;
  }
  NSWindow* ns_window = (__bridge NSWindow*)native;
  [ns_window na_swizzled_makeKeyAndOrderFront:nil];
  return true;
}

bool WindowManager::CallOriginalHide(WindowId id) {
  auto window = Get(id);
  if (!window) {
    return false;
  }
  void* native = window->GetNativeObject();
  if (!native) {
    return false;
  }
  NSWindow* ns_window = (__bridge NSWindow*)native;
  [ns_window na_swizzled_orderOut:nil];
  return true;
}

void WindowManager::StartEventListening() {
  pimpl_->StartEventListening();
}

void WindowManager::StopEventListening() {
  pimpl_->StopEventListening();
}

void WindowManager::DispatchWindowEvent(const WindowEvent& event) {
  Emit(event);
}

}  // namespace nativeapi
