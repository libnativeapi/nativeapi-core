#include <iostream>
#include <string>
#include "../../foundation/id_allocator.h"
#include "../../window.h"
#include "../../window_shape.h"
#include "../../window_manager.h"
#include "../../window_registry.h"
#include "coordinate_utils_macos.h"

// Import Cocoa headers
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

// Key for associated objects (used by both window_macos.mm and window_manager_macos.mm)
const void* kWindowIdKey = &kWindowIdKey;

// Window::SetFocusable() and Window::SetNonActivating() state lives on the NSWindow itself
// (associated objects + the object's runtime class) rather than in Window::Impl: the registry
// may hand out a fresh Window wrapper for the same NSWindow, and the class overrides below
// need to read the focusable flag without access to the Impl.
static const void* kWindowFocusableKey = &kWindowFocusableKey;
static const void* kWindowOriginalClassKey = &kWindowOriginalClassKey;
// Window::SetTitleBarStyle() / SetMovable() state, kept on the NSWindow for the same reason.
static const void* kWindowTitleBarHiddenKey = &kWindowTitleBarHiddenKey;
static const void* kWindowMovableKey = &kWindowMovableKey;
// A shaped window must not inherit AppKit's rounded titled frame. Keep this
// choice after clearing its polygon so the restored rectangle has square corners.
static const void* kWindowShapeFrameKey = &kWindowShapeFrameKey;
// The parent a hidden window is waiting for. AppKit orders a window in when it
// becomes the child of a visible one, so a hidden child is only attached once
// it is shown; see Window::SetParentWindow().
static const void* kWindowPendingParentKey = &kWindowPendingParentKey;
// Window::SetVisualEffect() state, on the NSWindow like the rest: the effect in force, the
// view that draws it, and the background the content view controller had before the effect
// made it clear.
// Window::SetContentUnderTitleBar() state, and the window control button
// visibility Window::SetTitleBarStyle() / SetWindowControlButtonsVisible() last asked
// for. Both on the NSWindow, for the same reason as the flags above.
static const void* kWindowContentUnderTitleBarKey = &kWindowContentUnderTitleBarKey;
static const void* kWindowButtonsVisibleKey = &kWindowButtonsVisibleKey;
static const void* kWindowVisualEffectKey = &kWindowVisualEffectKey;
static const void* kWindowVisualEffectViewKey = &kWindowVisualEffectViewKey;
static const void* kWindowAspectRatioKey = &kWindowAspectRatioKey;
static const void* kWindowContentBackgroundKey = &kWindowContentBackgroundKey;

// Also called by window_manager_macos.mm, for windows someone else shows.
void NativeApiAttachPendingParentWindow(NSWindow* window) {
  NSWindow* parent = objc_getAssociatedObject(window, kWindowPendingParentKey);
  if (!parent || ![window isVisible]) {
    return;
  }
  objc_setAssociatedObject(window, kWindowPendingParentKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  if ([window parentWindow] != parent) {
    [parent addChildWindow:window ordered:NSWindowAbove];
  }
}

// The other way round, before a child is ordered out: a child that stays
// attached is ordered back in with its parent.
static void NativeApiDetachFromParentWindow(NSWindow* window, bool keep_pending) {
  NSWindow* parent = [window parentWindow];
  if (parent) {
    [parent removeChildWindow:window];
  }
  if (!keep_pending) {
    parent = nil;
  } else if (!parent) {
    return;  // still waiting for whichever parent it had
  }
  objc_setAssociatedObject(window, kWindowPendingParentKey, parent,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

// The content may paint a backing of its own over the window's background. A view
// controller that has a background colour is the case that matters: a
// FlutterViewController is opaque black by default.
static NSViewController* NativeApiContentWithBackground(NSWindow* window) {
  NSViewController* content = [window contentViewController];
  if ([content respondsToSelector:@selector(setBackgroundColor:)] &&
      [content respondsToSelector:@selector(backgroundColor)]) {
    return content;
  }
  return nil;
}

static BOOL NativeApiWindowIsTitleBarHidden(NSWindow* window) {
  return [objc_getAssociatedObject(window, kWindowTitleBarHiddenKey) boolValue];
}

static BOOL NativeApiWindowHasContentUnderTitleBar(NSWindow* window) {
  return [objc_getAssociatedObject(window, kWindowContentUnderTitleBarKey) boolValue];
}

// A hidden title bar and a title bar the content has taken in are the same window: the
// content view covers the frame and the bar draws nothing. They differ only in whether
// the window buttons are left on it, which is applied separately.
static double NativeApiGetAspectRatio(NSWindow* window) {
  NSNumber* value = objc_getAssociatedObject(window, kWindowAspectRatioKey);
  return value ? value.doubleValue : 0.0;
}

// The public ratio is the content's. With a full-size content view the content
// covers the frame, so AppKit's frame ratio is the one to use; otherwise the
// content ratio. The choice follows the title bar style, so it is re-made
// whenever NSWindowStyleMaskFullSizeContentView changes.
static void NativeApiApplyAspectRatio(NSWindow* window) {
  // aspectRatio/resizeIncrements and contentAspectRatio/contentResizeIncrements
  // are each mutually exclusive; resetting both increments clears both ratios.
  window.resizeIncrements = NSMakeSize(1.0, 1.0);
  window.contentResizeIncrements = NSMakeSize(1.0, 1.0);
  const double aspect_ratio = NativeApiGetAspectRatio(window);
  if (aspect_ratio <= 0.0) {
    return;
  }
  const NSSize ratio = NSMakeSize(aspect_ratio, 1.0);
  if (window.styleMask & NSWindowStyleMaskFullSizeContentView) {
    window.aspectRatio = ratio;
  } else {
    window.contentAspectRatio = ratio;
  }
}

static void NativeApiApplyTitleBarAppearance(NSWindow* window) {
  if (!window) {
    return;
  }
  const BOOL full_size =
      NativeApiWindowIsTitleBarHidden(window) || NativeApiWindowHasContentUnderTitleBar(window);

  // Changing NSWindowStyleMaskFullSizeContentView keeps the content size and the
  // bottom-left origin, so the window would grow or shrink by the title bar height and
  // its top edge would jump. Keep the frame instead, as on Windows: the content takes
  // over, or gives back, the title bar area.
  const NSRect frame = window.frame;
  NSNumber* shape_frame = objc_getAssociatedObject(window, kWindowShapeFrameKey);
  auto mask = window.styleMask;
  if (shape_frame && NativeApiWindowIsTitleBarHidden(window)) {
    mask &= ~(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
              NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView);
  } else {
    if (full_size) mask |= NSWindowStyleMaskFullSizeContentView;
    else mask &= ~NSWindowStyleMaskFullSizeContentView;
    if (shape_frame) {
      mask |= [shape_frame unsignedLongLongValue] &
              (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable);
      objc_setAssociatedObject(window, kWindowShapeFrameKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
  }
  if (window.styleMask != mask) window.styleMask = mask;
  window.titleVisibility = full_size ? NSWindowTitleHidden : NSWindowTitleVisible;
  window.titlebarAppearsTransparent = full_size;
  [window setFrame:frame display:YES];
  NativeApiApplyAspectRatio(window);
}

// Whether the window control buttons were last asked for; a window starts with them.
static void NativeApiApplyWindowControlButtons(NSWindow* window) {
  NSNumber* value = objc_getAssociatedObject(window, kWindowButtonsVisibleKey);
  const BOOL hidden = value ? ![value boolValue] : NO;
  for (NSWindowButton button :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    [[window standardWindowButton:button] setHidden:hidden];
  }
}

// Whether the user may move the window, as requested through Window::SetMovable().
static BOOL NativeApiWindowIsMovable(NSWindow* window) {
  NSNumber* value = objc_getAssociatedObject(window, kWindowMovableKey);
  return value ? [value boolValue] : [window isMovable];
}

// With the title bar hidden, the content extends under the title bar band, but AppKit still
// moves the window for drags that start in that band, even over opaque content views that handle
// the mouse themselves (the window server decides from its own drag region, not from
// -mouseDownCanMoveWindow). The content owns the band then, so the system is kept from moving
// the window; Window::StartDragging() (-performWindowDragWithEvent:) and SetPosition() still
// work, and are how apps with a custom title bar move it.
static void NativeApiUpdateWindowMovable(NSWindow* window) {
  BOOL requested = NativeApiWindowIsMovable(window);
  objc_setAssociatedObject(window, kWindowMovableKey, @(requested),
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  [window setMovable:requested && !NativeApiWindowIsTitleBarHidden(window)];
}

static BOOL NativeApiWindowIsFocusable(NSWindow* window) {
  NSNumber* value = objc_getAssociatedObject(window, kWindowFocusableKey);
  return value ? [value boolValue] : YES;
}

// NSPanel subclass that an existing NSWindow is switched to by Window::SetNonActivating(true).
// NSWindow and NSPanel share the same instance layout, so object_setClass() is safe here.
// The panel can become key (receive keyboard input) but never main, and paired with
// NSWindowStyleMaskNonactivatingPanel it does not activate the application when shown.
@interface NativeApiNonActivatingPanel : NSPanel
@end

@implementation NativeApiNonActivatingPanel
- (BOOL)canBecomeKeyWindow {
  return NativeApiWindowIsFocusable(self);
}
- (BOOL)canBecomeMainWindow {
  return NO;
}
@end

static BOOL NativeApiWindowIsNonActivating(NSWindow* window) {
  return object_getClass(window) == [NativeApiNonActivatingPanel class];
}

// The class the NSWindow had before nativeapi first swapped it. Recorded lazily so windows
// that are never touched keep their class untouched.
static Class NativeApiWindowOriginalClass(NSWindow* window) {
  Class original = objc_getAssociatedObject(window, kWindowOriginalClassKey);
  if (!original) {
    original = object_getClass(window);
    objc_setAssociatedObject(window, kWindowOriginalClassKey, original,
                             OBJC_ASSOCIATION_ASSIGN);
  }
  return original;
}

// Add a per-window focus override without changing isa: AppKit uses KVO runtime
// classes for its frame, and replacing/subclassing those classes loses its
// observation bookkeeping. Other instances continue through the original method.
static void NativeApiInstallFocusOverride(NSWindow* window) {
  static const void* installed_key = &installed_key;
  Class cls = object_getClass(window);
  if (objc_getAssociatedObject(cls, installed_key)) return;
  SEL selector = @selector(canBecomeKeyWindow);
  Method method = class_getInstanceMethod(cls, selector);
  auto original = reinterpret_cast<BOOL (*)(id, SEL)>(method_getImplementation(method));
  IMP replacement = imp_implementationWithBlock(^BOOL(NSWindow* instance) {
    if (objc_getAssociatedObject(instance, kWindowFocusableKey) ||
        (NativeApiWindowIsTitleBarHidden(instance) &&
         objc_getAssociatedObject(instance, kWindowShapeFrameKey))) {
      return NativeApiWindowIsFocusable(instance);
    }
    return original(instance, selector);
  });
  if (!class_addMethod(cls, selector, replacement, method_getTypeEncoding(method))) {
    class_replaceMethod(cls, selector, replacement, method_getTypeEncoding(method));
  }
  objc_setAssociatedObject(cls, installed_key, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

// Non-activating windows use a panel; focus overrides preserve the live class.
static void NativeApiUpdateWindowClass(NSWindow* window, bool non_activating) {
  if (non_activating) {
    NativeApiWindowOriginalClass(window);
    if (object_getClass(window) != [NativeApiNonActivatingPanel class]) {
      object_setClass(window, [NativeApiNonActivatingPanel class]);
    }
    window.styleMask |= NSWindowStyleMaskNonactivatingPanel;
    NSPanel* panel = (NSPanel*)window;
    // NSPanel defaults to hiding when the app deactivates, which would pull a pinned
    // helper window away as soon as the user clicks into another app.
    panel.hidesOnDeactivate = NO;
    panel.becomesKeyOnlyIfNeeded = NO;
    return;
  }
  if (NativeApiWindowIsNonActivating(window)) {
    object_setClass(window, NativeApiWindowOriginalClass(window));
    window.styleMask &= ~NSWindowStyleMaskNonactivatingPanel;
  }
  if (objc_getAssociatedObject(window, kWindowFocusableKey) ||
      (NativeApiWindowIsTitleBarHidden(window) && objc_getAssociatedObject(window, kWindowShapeFrameKey))) {
    NativeApiInstallFocusOverride(window);
  }
}

#include "window_shadow_macos.h"

namespace nativeapi {

// Private implementation class
class Window::Impl {
 public:
  // Every wrapper keeps its NSWindow alive: a handle can outlive the window
  // being closed, and AppKit releases a closed window unless someone else
  // still holds it. Under ARC the strong member already does that.
  Impl(WindowId id, NSWindow* window) : id_(id), ns_window_(window) {
#if !__has_feature(objc_arc)
    [ns_window_ retain];
#endif
  }
  ~Impl() {
#if !__has_feature(objc_arc)
    [ns_window_ release];
#endif
  }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  WindowId id_;
  NSWindow* ns_window_;
};

Window::Window() : Window(nullptr) {}

Window::Window(void* native_window) {
  NSWindow* ns_window = nullptr;
  WindowId id;

  if (native_window == nullptr) {
    // Create new platform object
    id = IdAllocator::Allocate<Window>();
    ns_window = [[NSWindow alloc] init];
    ns_window.styleMask = NSWindowStyleMaskResizable | NSWindowStyleMaskTitled |
                          NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    // Store the ID as associated object
    objc_setAssociatedObject(ns_window, kWindowIdKey, [NSNumber numberWithUnsignedLongLong:id],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  } else {
    // Wrap existing platform object - check if it already has an ID
    ns_window = (__bridge NSWindow*)native_window;
    NSNumber* existingId = objc_getAssociatedObject(ns_window, kWindowIdKey);
    if (existingId) {
      // Use existing ID
      id = [existingId unsignedLongLongValue];
    } else {
      // Allocate new ID and store it
      id = IdAllocator::Allocate<Window>();
      objc_setAssociatedObject(ns_window, kWindowIdKey, [NSNumber numberWithUnsignedLongLong:id],
                               OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
  }

  // All initialization logic in one place
  pimpl_ = std::make_unique<Impl>(id, ns_window);
}

Window::~Window() {}

void Window::Focus() {
  [pimpl_->ns_window_ makeKeyAndOrderFront:nil];
}

void Window::Blur() {
  [pimpl_->ns_window_ orderBack:nil];
}

bool Window::IsFocused() const {
  return [pimpl_->ns_window_ isKeyWindow];
}

void Window::Show() {
  [pimpl_->ns_window_ setIsVisible:YES];
  // Panels receive key focus when shown but should not activate the app.
  if (![pimpl_->ns_window_ isKindOfClass:[NSPanel class]]) {
    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
  }
  [pimpl_->ns_window_ makeKeyAndOrderFront:nil];
  NativeApiAttachPendingParentWindow(pimpl_->ns_window_);
}

void Window::ShowInactive() {
  [pimpl_->ns_window_ setIsVisible:YES];
  [pimpl_->ns_window_ orderFrontRegardless];
  NativeApiAttachPendingParentWindow(pimpl_->ns_window_);
}

void Window::Hide() {
  NativeApiDetachFromParentWindow(pimpl_->ns_window_, true);
  [pimpl_->ns_window_ setIsVisible:NO];
  [pimpl_->ns_window_ orderOut:nil];
}

bool Window::IsVisible() const {
  return [pimpl_->ns_window_ isVisible];
}

void Window::Maximize() {
  if (!IsMaximized()) {
    [pimpl_->ns_window_ zoom:nil];
  }
}

void Window::Unmaximize() {
  if (IsMaximized()) {
    [pimpl_->ns_window_ zoom:nil];
  }
}

bool Window::IsMaximized() const {
  return [pimpl_->ns_window_ isZoomed];
}

void Window::Minimize() {
  if (!IsMinimized()) {
    [pimpl_->ns_window_ miniaturize:nil];
  }
}

void Window::Restore() {
  if (IsMinimized()) {
    [pimpl_->ns_window_ deminiaturize:nil];
  }
}

bool Window::IsMinimized() const {
  return [pimpl_->ns_window_ isMiniaturized];
}

void Window::SetFullScreen(bool is_full_screen) {
  if (is_full_screen) {
    if (!IsFullScreen()) {
      [pimpl_->ns_window_ toggleFullScreen:nil];
    }
  } else {
    if (IsFullScreen()) {
      [pimpl_->ns_window_ toggleFullScreen:nil];
    }
  }
}

bool Window::IsFullScreen() const {
  return [pimpl_->ns_window_ styleMask] & NSWindowStyleMaskFullScreen;
}

//// void Window::SetBackgroundColor(Color color);
//// Color Window::GetBackgroundColor() const;

void Window::SetBounds(Rectangle bounds) {
  // Convert from topLeft coordinate system to bottom-left (macOS default)
  NSRect topLeftRect = NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height);
  NSRect nsRect = NSRectExt::bottomLeft(topLeftRect);
  [pimpl_->ns_window_ setFrame:nsRect display:YES];
}

Rectangle Window::GetBounds() const {
  NSRect frame = [pimpl_->ns_window_ frame];
  // Convert from bottom-left (macOS default) to top-left coordinate system
  CGPoint topLeft = NSRectExt::topLeft(frame);
  Rectangle bounds = {topLeft.x, topLeft.y, static_cast<double>(frame.size.width),
                      static_cast<double>(frame.size.height)};
  return bounds;
}

void Window::SetSize(Size size, bool animate) {
  NSRect frame = [pimpl_->ns_window_ frame];
  frame.origin.y += (frame.size.height - size.height);
  frame.size.width = size.width;
  frame.size.height = size.height;
  if (animate) {
    [[pimpl_->ns_window_ animator] setFrame:frame display:YES animate:YES];
  } else {
    [pimpl_->ns_window_ setFrame:frame display:YES];
  }
}

Size Window::GetSize() const {
  NSRect frame = [pimpl_->ns_window_ frame];
  Size size = {static_cast<double>(frame.size.width), static_cast<double>(frame.size.height)};
  return size;
}

void Window::SetContentSize(Size size) {
  [pimpl_->ns_window_ setContentSize:NSMakeSize(size.width, size.height)];
}

Size Window::GetContentSize() const {
  NSRect frame = [pimpl_->ns_window_ contentRectForFrameRect:[pimpl_->ns_window_ frame]];
  Size size = {static_cast<double>(frame.size.width), static_cast<double>(frame.size.height)};
  return size;
}

void Window::SetContentBounds(Rectangle bounds) {
  // Convert from topLeft coordinate system to bottom-left (macOS default)
  NSRect topLeftRect = NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height);
  NSRect contentRect = NSRectExt::bottomLeft(topLeftRect);

  // Set the content view frame
  NSRect frameRect = [pimpl_->ns_window_ frameRectForContentRect:contentRect];
  [pimpl_->ns_window_ setFrame:frameRect display:YES];
}

Rectangle Window::GetContentBounds() const {
  NSRect contentRect = [pimpl_->ns_window_ contentRectForFrameRect:[pimpl_->ns_window_ frame]];
  // Convert from bottom-left (macOS default) to top-left coordinate system
  CGPoint topLeft = NSRectExt::topLeft(contentRect);
  Rectangle bounds = {topLeft.x, topLeft.y, static_cast<double>(contentRect.size.width),
                      static_cast<double>(contentRect.size.height)};
  return bounds;
}

void Window::SetMinimumSize(Size size) {
  [pimpl_->ns_window_ setMinSize:NSMakeSize(size.width, size.height)];
}

Size Window::GetMinimumSize() const {
  NSSize size = [pimpl_->ns_window_ minSize];
  return Size{static_cast<double>(size.width), static_cast<double>(size.height)};
}

void Window::SetMaximumSize(Size size) {
  [pimpl_->ns_window_ setMaxSize:NSMakeSize(size.width, size.height)];
}

void Window::SetAspectRatio(double aspect_ratio) {
  NSWindow* window = pimpl_->ns_window_;
  // Stored on the NSWindow, so every wrapper of it (and a later title bar style
  // change) sees the same ratio.
  objc_setAssociatedObject(window, kWindowAspectRatioKey,
                           aspect_ratio > 0.0 ? @(aspect_ratio) : nil,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  NativeApiApplyAspectRatio(window);
}

double Window::GetAspectRatio() const {
  return NativeApiGetAspectRatio(pimpl_->ns_window_);
}

Size Window::GetMaximumSize() const {
  NSSize size = [pimpl_->ns_window_ maxSize];
  return Size{static_cast<double>(size.width), static_cast<double>(size.height)};
}

void Window::SetResizable(bool is_resizable) {
  NSUInteger style_mask = [pimpl_->ns_window_ styleMask];
  if (is_resizable) {
    style_mask |= NSWindowStyleMaskResizable;
  } else {
    style_mask &= ~NSWindowStyleMaskResizable;
  }
  [pimpl_->ns_window_ setStyleMask:style_mask];
}

bool Window::IsResizable() const {
  return [pimpl_->ns_window_ styleMask] & NSWindowStyleMaskResizable;
}

void Window::SetMovable(bool is_movable) {
  NSWindow* window = pimpl_->ns_window_;
  if (!window) {
    return;
  }
  objc_setAssociatedObject(window, kWindowMovableKey, @(is_movable),
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  NativeApiUpdateWindowMovable(window);
}

bool Window::IsMovable() const {
  NSWindow* window = pimpl_->ns_window_;
  return window && NativeApiWindowIsMovable(window);
}

void Window::SetMinimizable(bool is_minimizable) {
  NSUInteger style_mask = [pimpl_->ns_window_ styleMask];
  if (is_minimizable) {
    style_mask |= NSWindowStyleMaskMiniaturizable;
  } else {
    style_mask &= ~NSWindowStyleMaskMiniaturizable;
  }
  [pimpl_->ns_window_ setStyleMask:style_mask];
}

bool Window::IsMinimizable() const {
  return [pimpl_->ns_window_ styleMask] & NSWindowStyleMaskMiniaturizable;
}

void Window::SetMaximizable(bool is_maximizable) {
  NSUInteger style_mask = [pimpl_->ns_window_ styleMask];
  if (is_maximizable) {
    style_mask |= NSWindowStyleMaskResizable;
  } else {
    style_mask &= ~NSWindowStyleMaskResizable;
  }
  [pimpl_->ns_window_ setStyleMask:style_mask];
}

bool Window::IsMaximizable() const {
  return [pimpl_->ns_window_ styleMask] & NSWindowStyleMaskResizable;
}

void Window::SetFullScreenable(bool is_full_screenable) {
  // TODO: Implement this
}

bool Window::IsFullScreenable() const {
  return [pimpl_->ns_window_ styleMask] & NSWindowStyleMaskFullScreen;
}

void Window::SetClosable(bool is_closable) {
  NSUInteger style_mask = [pimpl_->ns_window_ styleMask];
  if (is_closable) {
    style_mask |= NSWindowStyleMaskClosable;
  } else {
    style_mask &= ~NSWindowStyleMaskClosable;
  }
  [pimpl_->ns_window_ setStyleMask:style_mask];
}

bool Window::IsClosable() const {
  return [pimpl_->ns_window_ styleMask] & NSWindowStyleMaskClosable;
}

void Window::SetWindowControlButtonsVisible(bool is_visible) {
  objc_setAssociatedObject(pimpl_->ns_window_, kWindowButtonsVisibleKey, @(is_visible),
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  NativeApiApplyWindowControlButtons(pimpl_->ns_window_);
}

bool Window::IsWindowControlButtonsVisible() const {
  NSButton* closeButton = [pimpl_->ns_window_ standardWindowButton:NSWindowCloseButton];
  if (closeButton) {
    return ![closeButton isHidden];
  }
  return true;  // Default to visible if button not found
}

void Window::SetAlwaysOnTop(bool is_always_on_top) {
  [pimpl_->ns_window_ setLevel:is_always_on_top ? NSFloatingWindowLevel : NSNormalWindowLevel];
}

bool Window::IsAlwaysOnTop() const {
  return [pimpl_->ns_window_ level] == NSFloatingWindowLevel;
}

// One level below NSNormalWindowLevel: beneath every ordinary window, above the desktop.
static const NSInteger kAlwaysOnBottomWindowLevel = NSNormalWindowLevel - 1;

void Window::SetAlwaysOnBottom(bool is_always_on_bottom) {
  [pimpl_->ns_window_
      setLevel:is_always_on_bottom ? kAlwaysOnBottomWindowLevel : NSNormalWindowLevel];
}

bool Window::IsAlwaysOnBottom() const {
  return [pimpl_->ns_window_ level] == kAlwaysOnBottomWindowLevel;
}

bool Window::SetParentWindow(std::shared_ptr<Window> parent) {
  NSWindow* window = pimpl_->ns_window_;
  if (!window) {
    return false;
  }
  if (!parent) {
    NativeApiDetachFromParentWindow(window, false);
    return true;
  }
  NSWindow* parent_window = (__bridge NSWindow*)parent->GetNativeObject();
  if (!parent_window) {
    return false;
  }
  // Neither itself nor one of its own descendants
  for (NSWindow* ancestor = parent_window; ancestor; ancestor = [ancestor parentWindow]) {
    if (ancestor == window) {
      return false;
    }
  }
  NativeApiDetachFromParentWindow(window, false);
  objc_setAssociatedObject(window, kWindowPendingParentKey, parent_window,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  NativeApiAttachPendingParentWindow(window);
  return true;
}

std::shared_ptr<Window> Window::GetParentWindow() const {
  NSWindow* window = pimpl_->ns_window_;
  NSWindow* parent_window = [window parentWindow];
  if (!parent_window) {
    parent_window = objc_getAssociatedObject(window, kWindowPendingParentKey);
  }
  if (!parent_window) {
    return nullptr;
  }
  // The wrapper takes the ID the native window already carries, which is how
  // the registered Window for it, if there is one, is found.
  auto wrapper = std::make_shared<Window>((__bridge void*)parent_window);
  auto registered = WindowManager::GetInstance().Get(wrapper->GetId());
  return registered ? registered : wrapper;
}

void Window::SetNonActivating(bool is_non_activating) {
  NativeApiUpdateWindowClass(pimpl_->ns_window_, is_non_activating);
}

bool Window::IsNonActivating() const {
  return NativeApiWindowIsNonActivating(pimpl_->ns_window_);
}

void Window::SetPosition(Point point) {
  // Convert from topLeft coordinate system to bottom-left (macOS default)
  // We need the window height to correctly convert the top-left position
  NSRect frame = [pimpl_->ns_window_ frame];
  CGPoint topLeftPoint = {point.x, point.y};
  NSPoint bottomLeft = NSPointExt::bottomLeftForWindow(topLeftPoint, frame.size.height);
  [pimpl_->ns_window_ setFrameOrigin:bottomLeft];
}

Point Window::GetPosition() const {
  NSRect frame = [pimpl_->ns_window_ frame];
  // Convert from bottom-left (macOS default) to top-left coordinate system
  CGPoint topLeft = NSRectExt::topLeft(frame);
  Point point = {topLeft.x, topLeft.y};
  return point;
}

void Window::Center() {
  // Use NSWindow's center method which automatically centers on the main screen
  [pimpl_->ns_window_ center];
}

void Window::SetTitle(std::string title) {
  [pimpl_->ns_window_ setTitle:[NSString stringWithUTF8String:title.c_str()]];
}

std::string Window::GetTitle() const {
  NSString* title = [pimpl_->ns_window_ title];
  return title ? std::string([title UTF8String]) : std::string();
}

void Window::SetTitleBarStyle(TitleBarStyle style) {
  if (!pimpl_->ns_window_) {
    return;
  }
  const BOOL hidden = style == TitleBarStyle::Hidden;
  const BOOL has_shadow = HasShadow();
  // Record the movable preference before the hidden flag starts to affect -isMovable.
  NativeApiUpdateWindowMovable(pimpl_->ns_window_);
  objc_setAssociatedObject(pimpl_->ns_window_, kWindowTitleBarHiddenKey, @(hidden),
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  NativeApiUpdateWindowMovable(pimpl_->ns_window_);

  NativeApiApplyTitleBarAppearance(pimpl_->ns_window_);
  NativeApiUpdateWindowClass(pimpl_->ns_window_, NativeApiWindowIsNonActivating(pimpl_->ns_window_));

  // The style says what the buttons do by default; SetWindowControlButtonsVisible()
  // after this call overrides it.
  objc_setAssociatedObject(pimpl_->ns_window_, kWindowButtonsVisibleKey, @(!hidden),
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  NativeApiApplyWindowControlButtons(pimpl_->ns_window_);

  // Title-bar changes must preserve the caller's shadow preference.
  pimpl_->ns_window_.opaque = NO;
  SetHasShadow(has_shadow);
}

bool Window::SetContentUnderTitleBar(bool is_content_under_title_bar) {
  if (!pimpl_->ns_window_) {
    return false;
  }
  objc_setAssociatedObject(pimpl_->ns_window_, kWindowContentUnderTitleBarKey,
                           @(is_content_under_title_bar), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  NativeApiApplyTitleBarAppearance(pimpl_->ns_window_);
  return true;
}

bool Window::IsContentUnderTitleBar() const {
  return NativeApiWindowHasContentUnderTitleBar(pimpl_->ns_window_);
}

bool Window::IsContentUnderTitleBarSupported() {
  return true;
}

TitleBarStyle Window::GetTitleBarStyle() const {
  return NativeApiWindowIsTitleBarHidden(pimpl_->ns_window_) ? TitleBarStyle::Hidden
                                                             : TitleBarStyle::Normal;
}

void Window::SetHasShadow(bool has_shadow) {
  if (auto* custom = NativeApiGetCustomShadow(pimpl_->ns_window_)) {
    custom->enabled = has_shadow;
    [custom update];
  } else {
    [pimpl_->ns_window_ setHasShadow:has_shadow];
    [pimpl_->ns_window_ invalidateShadow];
  }
}

bool Window::HasShadow() const {
  if (auto* custom = NativeApiGetCustomShadow(pimpl_->ns_window_)) return custom->enabled;
  return [pimpl_->ns_window_ hasShadow];
}

bool Window::SetCustomShadow(std::shared_ptr<WindowShadow> shadow) {
  NSWindow* window = pimpl_->ns_window_;
  if (!window || (shadow && GetTitleBarStyle() != TitleBarStyle::Hidden)) return false;
  const bool enabled = HasShadow();
  auto* state = NativeApiGetCustomShadow(window);
  if (!shadow) {
    [state close];
    objc_setAssociatedObject(window, kNativeApiCustomShadowKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    window.hasShadow = enabled;
    [window invalidateShadow];
    return true;
  }
  if (!state) {
    state = [[NativeApiCustomShadow alloc] init];
    state->target = window;
    objc_setAssociatedObject(window, kNativeApiCustomShadowKey, state, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    for (NSNotificationName name in @[NSWindowDidResizeNotification, NSWindowDidChangeScreenNotification,
                                     NSWindowDidDeminiaturizeNotification,
                                     NSWindowWillCloseNotification])
      [[NSNotificationCenter defaultCenter] addObserver:state selector:@selector(changed:) name:name object:window];
#if !__has_feature(objc_arc)
    [state release];
#endif
  }
  state->options = std::make_shared<WindowShadow>(*shadow);
  state->enabled = enabled;
  [state update];
  return true;
}
std::shared_ptr<WindowShadow> Window::GetCustomShadow() const {
  auto* state = NativeApiGetCustomShadow(pimpl_->ns_window_);
  return state && state->options ? std::make_shared<WindowShadow>(*state->options) : nullptr;
}

void Window::SetOpacity(float opacity) {
  [pimpl_->ns_window_ setAlphaValue:opacity];
}

float Window::GetOpacity() const {
  return [pimpl_->ns_window_ alphaValue];
}

bool Window::SetVisualEffect(VisualEffect effect) {
  NSWindow* window = pimpl_->ns_window_;
  if (!window) {
    return false;
  }
  NSVisualEffectView* effect_view = objc_getAssociatedObject(window, kWindowVisualEffectViewKey);
  NSViewController* content = NativeApiContentWithBackground(window);

  if (effect == VisualEffect::None) {
    if (effect_view) {
      [effect_view removeFromSuperview];
      objc_setAssociatedObject(window, kWindowVisualEffectViewKey, nil,
                               OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    // The content gets back the backing it had, or the one it was given meanwhile.
    NSColor* content_background = objc_getAssociatedObject(window, kWindowContentBackgroundKey);
    if (content_background && content) {
      [content performSelector:@selector(setBackgroundColor:) withObject:content_background];
    }
    objc_setAssociatedObject(window, kWindowContentBackgroundKey, nil,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(window, kWindowVisualEffectKey, nil,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return true;
  }

  NSVisualEffectMaterial material;
  switch (effect) {
    case VisualEffect::Blur:
      material = NSVisualEffectMaterialSidebar;
      break;
    case VisualEffect::Acrylic:
      material = NSVisualEffectMaterialUnderWindowBackground;
      break;
    case VisualEffect::Mica:
      material = NSVisualEffectMaterialWindowBackground;
      break;
    case VisualEffect::MicaAlt:
      material = NSVisualEffectMaterialTitlebar;
      break;
    case VisualEffect::Hud:
      material = NSVisualEffectMaterialHUDWindow;
      break;
    case VisualEffect::Popover:
      material = NSVisualEffectMaterialPopover;
      break;
    case VisualEffect::Menu:
      material = NSVisualEffectMaterialMenu;
      break;
    default:
      return false;
  }

  // The view is the lowest thing in the content view. A content view that draws into
  // layers of its own - a FlutterView does, at z positions from 0 up - would still end
  // up underneath it, so the effect's layer is pushed below those. (Next to the content
  // view is not an option: a foreign subview makes the frame view draw an old-style
  // title bar.) The view blends with what is behind the window, which needs neither a
  // clear background nor a non-opaque window: both stay as SetBackgroundColor() left them.
  NSView* content_view = [window contentView];
  if (!content_view) {
    return false;
  }
  if (!effect_view) {
    effect_view = [[NSVisualEffectView alloc] initWithFrame:[content_view bounds]];
    [effect_view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [effect_view setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
    [effect_view setState:NSVisualEffectStateActive];
    [effect_view setWantsLayer:YES];
    objc_setAssociatedObject(window, kWindowVisualEffectViewKey, effect_view,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  if ([effect_view superview] != content_view) {
    // Also after the content view was replaced: the effect moves to the new one.
    [effect_view setFrame:[content_view bounds]];
    [content_view addSubview:effect_view positioned:NSWindowBelow relativeTo:nil];
    [[effect_view layer] setZPosition:-1];
  }
  [effect_view setMaterial:material];

  if (content && !objc_getAssociatedObject(window, kWindowContentBackgroundKey)) {
    NSColor* content_background = [content performSelector:@selector(backgroundColor)];
    objc_setAssociatedObject(window, kWindowContentBackgroundKey,
                             content_background ?: [NSColor blackColor],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  if (content) {
    [content performSelector:@selector(setBackgroundColor:) withObject:[NSColor clearColor]];
  }

  objc_setAssociatedObject(window, kWindowVisualEffectKey, @(static_cast<int>(effect)),
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  return true;
}

VisualEffect Window::GetVisualEffect() const {
  NSNumber* effect = objc_getAssociatedObject(pimpl_->ns_window_, kWindowVisualEffectKey);
  return effect ? static_cast<VisualEffect>([effect intValue]) : VisualEffect::None;
}

bool Window::IsVisualEffectSupported(VisualEffect effect) {
  return true;
}

void Window::SetBackgroundColor(const Color& color) {
  NSColor* nsColor = [NSColor colorWithRed:color.r / 255.0
                                     green:color.g / 255.0
                                      blue:color.b / 255.0
                                     alpha:color.a / 255.0];
  [pimpl_->ns_window_ setBackgroundColor:nsColor];
  // A translucent background only shows through a window that says it is not
  // opaque; without this AppKit composites it over black.
  [pimpl_->ns_window_ setOpaque:color.a == 255];

  // The content takes the same colour (a FlutterViewController is what makes this
  // necessary) - once the visual effect is gone, if there is one: until then the
  // content stays clear so that the effect shows.
  NSViewController* content = NativeApiContentWithBackground(pimpl_->ns_window_);
  if (!content) {
    return;
  }
  if (objc_getAssociatedObject(pimpl_->ns_window_, kWindowContentBackgroundKey)) {
    objc_setAssociatedObject(pimpl_->ns_window_, kWindowContentBackgroundKey, nsColor,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return;
  }
  [content performSelector:@selector(setBackgroundColor:) withObject:nsColor];
}

Color Window::GetBackgroundColor() const {
  NSColor* nsColor = [pimpl_->ns_window_ backgroundColor];

  // Convert NSColor to RGB color space if needed
  NSColor* rgbColor = [nsColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
  if (!rgbColor) {
    // Fallback if conversion fails
    return Color::White;
  }

  CGFloat r, g, b, a;
  [rgbColor getRed:&r green:&g blue:&b alpha:&a];

  return Color::FromRGBA(
    static_cast<unsigned char>(r * 255),
    static_cast<unsigned char>(g * 255),
    static_cast<unsigned char>(b * 255),
    static_cast<unsigned char>(a * 255)
  );
}

void Window::SetVisibleOnAllWorkspaces(bool is_visible_on_all_workspaces) {
  [pimpl_->ns_window_ setCollectionBehavior:is_visible_on_all_workspaces
                                                ? NSWindowCollectionBehaviorCanJoinAllSpaces
                                                : NSWindowCollectionBehaviorDefault];
}

bool Window::IsVisibleOnAllWorkspaces() const {
  return [pimpl_->ns_window_ collectionBehavior] & NSWindowCollectionBehaviorCanJoinAllSpaces;
}

void Window::SetVisibleInTaskbar(bool is_visible_in_taskbar) {
  // The Dock lists applications, not windows; the closest per-window list is the
  // application's Window menu.
  [pimpl_->ns_window_ setExcludedFromWindowsMenu:!is_visible_in_taskbar];
}

bool Window::IsVisibleInTaskbar() const {
  return ![pimpl_->ns_window_ isExcludedFromWindowsMenu];
}

void Window::SetIgnoreMouseEvents(bool is_ignore_mouse_events) {
  [pimpl_->ns_window_ setIgnoresMouseEvents:is_ignore_mouse_events];
}

bool Window::IsIgnoreMouseEvents() const {
  return [pimpl_->ns_window_ ignoresMouseEvents];
}

void Window::SetFocusable(bool is_focusable) {
  NSWindow* window = pimpl_->ns_window_;
  objc_setAssociatedObject(window, kWindowFocusableKey, @(is_focusable),
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  NativeApiUpdateWindowClass(window, NativeApiWindowIsNonActivating(window));
}

bool Window::IsFocusable() const {
  return [pimpl_->ns_window_ canBecomeKeyWindow];
}

// Ends the mouse gesture of the view that received the mouse-down. The native drag and resize
// loops below take the rest of the gesture, including the mouse-up; a view that never sees it
// keeps its button state down (Flutter then reports the next press as a move, and that whole
// gesture is lost).
static void NativeApiSendMouseUp(NSWindow* window, NSEvent* mouse_up) {
  if (!mouse_up) {
    mouse_up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                  location:[window mouseLocationOutsideOfEventStream]
                             modifierFlags:0
                                 timestamp:[NSProcessInfo processInfo].systemUptime
                              windowNumber:window.windowNumber
                                   context:nil
                               eventNumber:0
                                clickCount:1
                                  pressure:0];
  }
  [window sendEvent:mouse_up];
}

void Window::StartDragging() {
  NSWindow* window = pimpl_->ns_window_;
  NSEvent* event = window.currentEvent;
  // The current event is the press only when this runs while AppKit handles
  // it. A caller that reacts later — a web view's script, a binding whose
  // calls hop to the main thread — finds some other event there, often not
  // even this window's, and AppKit would then anchor the drag at a wrong
  // point. As long as the button is still down, start from where it is now.
  if (!(event.window == window && (event.type == NSEventTypeLeftMouseDown ||
                                   event.type == NSEventTypeLeftMouseDragged))) {
    if (([NSEvent pressedMouseButtons] & 1) == 0) {
      return;
    }
    NSPoint location = [window convertPointFromScreen:NSEvent.mouseLocation];
    event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                               location:location
                          modifierFlags:0
                              timestamp:NSProcessInfo.processInfo.systemUptime
                           windowNumber:window.windowNumber
                                context:nil
                            eventNumber:0
                             clickCount:1
                               pressure:1];
  }
  [window performWindowDragWithEvent:event];
  if (event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeLeftMouseDragged) {
    NativeApiSendMouseUp(window, nil);
  }
}

void Window::StartResizing(ResizeEdge edge) {
  NSWindow* window = pimpl_->ns_window_;
  if (!window) {
    return;
  }

  const bool moves_left = edge == ResizeEdge::Left || edge == ResizeEdge::TopLeft ||
                          edge == ResizeEdge::BottomLeft;
  const bool moves_right = edge == ResizeEdge::Right || edge == ResizeEdge::TopRight ||
                           edge == ResizeEdge::BottomRight;
  const bool moves_top = edge == ResizeEdge::Top || edge == ResizeEdge::TopLeft ||
                         edge == ResizeEdge::TopRight;
  const bool moves_bottom = edge == ResizeEdge::Bottom || edge == ResizeEdge::BottomLeft ||
                            edge == ResizeEdge::BottomRight;
  // Pure vertical edges derive width from height; everything else derives height from width.
  const bool height_driven = edge == ResizeEdge::Top || edge == ResizeEdge::Bottom;

  // AppKit has no API to hand a resize to the system frame, so track the mouse
  // ourselves until the button is released. Screen coordinates have a bottom-left
  // origin, so a positive dy means the mouse moved up.
  const NSPoint start_mouse = [NSEvent mouseLocation];
  const NSRect start_frame = [window frame];
  const NSSize min_size = [window minSize];
  const NSSize max_size = [window maxSize];
  const double aspect_ratio = NativeApiGetAspectRatio(window);

  const NSEventMask mask = NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp;
  while (true) {
    NSEvent* event = [NSApp nextEventMatchingMask:mask
                                        untilDate:[NSDate distantFuture]
                                           inMode:NSEventTrackingRunLoopMode
                                          dequeue:YES];
    if (!event || event.type == NSEventTypeLeftMouseUp) {
      NativeApiSendMouseUp(window, event);
      break;
    }

    const NSPoint mouse = [NSEvent mouseLocation];
    const CGFloat dx = mouse.x - start_mouse.x;
    const CGFloat dy = mouse.y - start_mouse.y;

    CGFloat width = start_frame.size.width;
    CGFloat height = start_frame.size.height;
    if (moves_right) {
      width += dx;
    } else if (moves_left) {
      width -= dx;
    }
    if (moves_top) {
      height += dy;
    } else if (moves_bottom) {
      height -= dy;
    }

    if (aspect_ratio > 0.0) {
      // The ratio applies to the content area; convert through the frame insets.
      NSRect content = [window contentRectForFrameRect:NSMakeRect(0, 0, width, height)];
      if (height_driven) {
        content.size.width = content.size.height * aspect_ratio;
      } else {
        content.size.height = content.size.width / aspect_ratio;
      }
      NSRect frame = [window frameRectForContentRect:content];
      width = frame.size.width;
      height = frame.size.height;
    }

    width = MAX(min_size.width, MIN(max_size.width, width));
    height = MAX(min_size.height, MIN(max_size.height, height));

    NSRect frame = start_frame;
    frame.size = NSMakeSize(width, height);
    // Anchor the edge opposite to the one being dragged.
    if (moves_left) {
      frame.origin.x = NSMaxX(start_frame) - width;
    }
    if (moves_bottom) {
      frame.origin.y = NSMaxY(start_frame) - height;
    }
    [window setFrame:frame display:YES];
  }
}

WindowId Window::GetId() const {
  return pimpl_->id_;
}

void* Window::GetNativeObjectInternal() const {
  return (__bridge void*)pimpl_->ns_window_;
}

bool Window::SetShape(std::shared_ptr<WindowShape> shape) {
  NSWindow* window = pimpl_->ns_window_;
  NSView* view = window.contentView;
  if (!window || !view) return false;
  static NSString* const maskName = @"nativeapi.windowShape";
  if (!shape) {
    if ([view.layer.mask.name isEqualToString:maskName]) view.layer.mask = nil;
    [window invalidateShadow];
    [NativeApiGetCustomShadow(window) update];
    return true;
  }
  if (shape->GetPointCount() < 3 || GetTitleBarStyle() != TitleBarStyle::Hidden ||
      window.isOpaque || GetBackgroundColor().a != 0 ||
      GetVisualEffect() != VisualEffect::None) return false;
  view.wantsLayer = YES;
  // Do not replace a mask installed by the embedding application.
  if (view.layer.mask && ![view.layer.mask.name isEqualToString:maskName]) return false;
  if (!objc_getAssociatedObject(window, kWindowShapeFrameKey)) {
    objc_setAssociatedObject(window, kWindowShapeFrameKey, @(window.styleMask),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    NativeApiApplyTitleBarAppearance(window);
    NativeApiUpdateWindowClass(window, NativeApiWindowIsNonActivating(window));
  }
  CGMutablePathRef path = CGPathCreateMutable();
  for (size_t i = 0; i < shape->GetPointCount(); ++i) {
    const auto p = shape->GetPointAt(i);
    const double y = view.isFlipped ? p.y : NSHeight(view.bounds) - p.y;
    if (i == 0) CGPathMoveToPoint(path, nullptr, p.x, y);
    else CGPathAddLineToPoint(path, nullptr, p.x, y);
  }
  CGPathCloseSubpath(path);
  CAShapeLayer* mask = [CAShapeLayer layer];
  mask.name = maskName;
  mask.frame = view.bounds;
  mask.fillRule = kCAFillRuleEvenOdd;
  mask.path = path;
  CGPathRelease(path);
  view.layer.mask = mask;
  [window invalidateShadow];
  [NativeApiGetCustomShadow(window) update];
  return true;
}

bool Window::IsShaped() const {
  return [pimpl_->ns_window_.contentView.layer.mask.name isEqualToString:@"nativeapi.windowShape"];
}

bool Window::IsShapeSupported() { return true; }

bool Window::SetInputShape(std::shared_ptr<WindowShape> shape) { return false; }
bool Window::IsInputShaped() const { return false; }
bool Window::IsInputShapeSupported() { return false; }

}  // namespace nativeapi

namespace nativeapi {
bool Window::SetTitleBarColors(const Color&, const Color&) { return false; }
bool Window::ResetTitleBarColors() { return false; }
}
