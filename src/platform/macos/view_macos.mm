#include "../../view.h"

#include <string>

#include "../../foundation/id_allocator.h"
#include "../../window.h"
#include "view_internal_macos.h"

#import <QuartzCore/QuartzCore.h>

// ---------------------------------------------------------------------------
// Objective-C glue
// ---------------------------------------------------------------------------

@implementation NativeApiContainerView

- (BOOL)isFlipped {
  return YES;
}

@end

namespace {

// The bridge is the delegate only while the view listens (HookControl), so a
// field that nobody listens to reports nothing.
NativeApiViewBridge* ListeningBridge(NSTextField* field) {
  id delegate = field.delegate;
  if (![delegate isKindOfClass:[NativeApiViewBridge class]]) {
    return nil;
  }
  NativeApiViewBridge* bridge = delegate;
  return bridge.impl ? bridge : nil;
}

void ReportFocus(NSTextField* field, bool focused) {
  NativeApiViewBridge* bridge = ListeningBridge(field);
  if (!bridge) {
    return;
  }
  if (focused) {
    bridge.impl->Emit(nativeapi::ViewFocusedEvent(bridge.impl->id));
  } else {
    bridge.impl->Emit(nativeapi::ViewBlurredEvent(bridge.impl->id));
  }
}

}  // namespace

@implementation NativeApiTextField

- (BOOL)becomeFirstResponder {
  BOOL became = [super becomeFirstResponder];
  if (became) {
    ReportFocus(self, true);
  }
  return became;
}

- (void)textDidEndEditing:(NSNotification*)notification {
  [super textDidEndEditing:notification];
  ReportFocus(self, false);
}

@end

@implementation NativeApiSecureTextField

- (BOOL)becomeFirstResponder {
  BOOL became = [super becomeFirstResponder];
  if (became) {
    ReportFocus(self, true);
  }
  return became;
}

- (void)textDidEndEditing:(NSNotification*)notification {
  [super textDidEndEditing:notification];
  ReportFocus(self, false);
}

@end

@implementation NativeApiViewBridge

- (void)buttonClicked:(id)sender {
  if (self.impl) {
    self.impl->Emit(nativeapi::ButtonClickedEvent(self.impl->id));
  }
}

- (void)controlTextDidChange:(NSNotification*)notification {
  if (!self.impl) {
    return;
  }
  NSTextField* field = notification.object;
  self.impl->Emit(nativeapi::TextFieldChangedEvent(
      self.impl->id, std::string([field.stringValue UTF8String] ?: "")));
}

- (BOOL)control:(NSControl*)control
               textView:(NSTextView*)textView
    doCommandBySelector:(SEL)commandSelector {
  if (commandSelector != @selector(insertNewline:)) {
    return NO;
  }
  if (self.multiline) {
    [textView insertNewlineIgnoringFieldEditor:nil];
    return YES;
  }
  if (self.impl) {
    self.impl->Emit(nativeapi::TextFieldSubmittedEvent(self.impl->id));
  }
  return NO;
}

@end

namespace nativeapi {

// ---------------------------------------------------------------------------
// Platform state
// ---------------------------------------------------------------------------

View::Impl::Platform::Platform(Impl* impl, NSView* view) : impl(impl), view(view) {
#if !__has_feature(objc_arc)
  [view retain];
#endif
}

View::Impl::Platform::~Platform() {
  UnhookControl();
  if (frame_observer) {
    [[NSNotificationCenter defaultCenter] removeObserver:frame_observer];
    frame_observer = nil;
  }
#if !__has_feature(objc_arc)
  [bridge release];
  [view release];
#endif
  bridge = nil;
  view = nil;
}

void View::Impl::Platform::HookControl() {
  if (!listening) {
    return;
  }
  if (!bridge) {
    bridge = [[NativeApiViewBridge alloc] init];
  }
  bridge.impl = impl;
  bridge.multiline = multiline;
  if ([view isKindOfClass:[NSButton class]]) {
    NSButton* button = (NSButton*)view;
    button.target = bridge;
    button.action = @selector(buttonClicked:);
  } else if ([view isKindOfClass:[NSTextField class]]) {
    ((NSTextField*)view).delegate = bridge;
  }
}

void View::Impl::Platform::UnhookControl() {
  bridge.impl = nullptr;
  if ([view isKindOfClass:[NSButton class]]) {
    NSButton* button = (NSButton*)view;
    if (button.target == bridge) {
      button.target = nil;
      button.action = nil;
    }
  } else if ([view isKindOfClass:[NSTextField class]]) {
    NSTextField* field = (NSTextField*)view;
    if (field.delegate == bridge) {
      field.delegate = nil;
    }
  }
}

void View::Impl::Platform::ReplaceView(NSView* replacement) {
  if (!replacement || replacement == view) {
    return;
  }
  UnhookControl();
  replacement.frame = view.frame;
  replacement.hidden = view.hidden;
  replacement.toolTip = view.toolTip;
  NSView* superview = view.superview;
  if (superview) {
    [superview addSubview:replacement positioned:NSWindowAbove relativeTo:view];
    [view removeFromSuperview];
  }
#if !__has_feature(objc_arc)
  [replacement retain];
  [view release];
#endif
  view = replacement;
  impl->NativeReplaced((__bridge void*)replacement);
  HookControl();
}

// ---------------------------------------------------------------------------
// Impl: construction and the platform seam
// ---------------------------------------------------------------------------

View::Impl::Impl(View* owner, void* native, bool owned)
    : owner(owner), native(native), owned(owned), id(IdAllocator::Allocate<View>()) {
  if (!this->native && owned) {
    this->native = CreateNativeContainer();
  }
  platform = std::make_unique<Platform>(this, (__bridge NSView*)this->native);
}

View::Impl::~Impl() {
  if (owned && platform && platform->view) {
    [platform->view removeFromSuperview];
  }
  platform.reset();
  native = nullptr;
}

void* View::Impl::CreateNativeContainer() {
  NativeApiContainerView* view = [[NativeApiContainerView alloc] initWithFrame:NSZeroRect];
  view.autoresizingMask = NSViewNotSizable;
#if !__has_feature(objc_arc)
  [view autorelease];
#endif
  return (__bridge void*)view;
}

void View::Impl::SetNativeFrame(Rectangle frame) {
  NSView* view = platform->view;
  if (!view || is_root) {
    return;
  }
  NSRect rect = NSMakeRect(frame.x, frame.y, frame.width, frame.height);
  NSView* superview = view.superview;
  if (superview && !superview.isFlipped) {
    rect.origin.y = superview.bounds.size.height - frame.y - frame.height;
  }
  view.frame = rect;
}

Rectangle View::Impl::GetNativeFrame() const {
  NSView* view = platform->view;
  if (!view) {
    return Rectangle{0, 0, 0, 0};
  }
  if (is_root) {
    NSRect bounds = view.bounds;
    return Rectangle{0, 0, bounds.size.width, bounds.size.height};
  }
  NSRect rect = view.frame;
  NSView* superview = view.superview;
  double y = rect.origin.y;
  if (superview && !superview.isFlipped) {
    y = superview.bounds.size.height - rect.origin.y - rect.size.height;
  }
  return Rectangle{rect.origin.x, y, rect.size.width, rect.size.height};
}

Size View::Impl::GetNativeIntrinsicSize() const {
  NSView* view = platform->view;
  if (!view || [view isKindOfClass:[NativeApiContainerView class]]) {
    return Size{0, 0};
  }
  NSSize size = view.intrinsicContentSize;
  if ([view isKindOfClass:[NSControl class]]) {
    // Labels and buttons know their width; an editable field reports none.
    NSSize fitting = [(NSControl*)view sizeThatFits:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)];
    if (size.width < 0) {
      size.width = [view isKindOfClass:[NSTextField class]] && ((NSTextField*)view).isEditable
                       ? -1
                       : fitting.width;
    }
    if (size.height < 0) {
      size.height = fitting.height;
    }
  }
  return Size{IntrinsicMetric(size.width), IntrinsicMetric(size.height)};
}

void View::Impl::AddNativeSubview(Impl& child, size_t index) {
  NSView* view = platform->view;
  NSView* child_view = child.platform->view;
  if (!view || !child_view) {
    return;
  }
  NSView* below = nil;
  if (index > 0 && index - 1 < subviews.size()) {
    below = subviews[index - 1]->pimpl_->platform->view;
  }
  if (below) {
    [view addSubview:child_view positioned:NSWindowAbove relativeTo:below];
  } else if (index == 0 && subviews.size() > 1) {
    [view addSubview:child_view positioned:NSWindowBelow relativeTo:subviews[1]->pimpl_->platform->view];
  } else {
    [view addSubview:child_view];
  }
}

void View::Impl::RemoveNativeSubview(Impl& child) {
  NSView* child_view = child.platform->view;
  if (child_view && child_view.superview == platform->view) {
    [child_view removeFromSuperview];
  }
}

void View::Impl::SetNativeVisible(bool is_visible) {
  if (is_root) {
    return;
  }
  platform->view.hidden = !is_visible;
}

void View::Impl::SetNativeEnabled(bool is_enabled) {
  if (is_root) {
    return;
  }
  NSView* view = platform->view;
  if ([view isKindOfClass:[NSControl class]]) {
    ((NSControl*)view).enabled = is_enabled;
  }
}

bool View::Impl::IsNativeEnabled() const {
  NSView* view = platform->view;
  if ([view isKindOfClass:[NSControl class]]) {
    return ((NSControl*)view).isEnabled;
  }
  return true;
}

void View::Impl::SetNativeBackgroundColor(Color color) {
  NSView* view = platform->view;
  if (!view) {
    return;
  }
  if ([view isKindOfClass:[NSTextField class]]) {
    NSTextField* field = (NSTextField*)view;
    if (color.a == 0) {
      field.drawsBackground = field.isEditable;
      field.backgroundColor = [NSColor textBackgroundColor];
    } else {
      field.drawsBackground = YES;
      field.backgroundColor = ToNSColor(color);
    }
    return;
  }
  view.wantsLayer = YES;
  view.layer.backgroundColor = color.a == 0 ? nil : ToNSColor(color).CGColor;
}

void View::Impl::SetNativeTooltip(const std::optional<std::string>& tooltip) {
  platform->view.toolTip = tooltip ? [NSString stringWithUTF8String:tooltip->c_str()] : nil;
}

void View::Impl::NativeFocus() {
  NSView* view = platform->view;
  if (view.window && view.acceptsFirstResponder) {
    [view.window makeFirstResponder:view];
  }
}

void View::Impl::NativeBlur() {
  NSView* view = platform->view;
  if (view.window && IsNativeFocused()) {
    [view.window makeFirstResponder:nil];
  }
}

bool View::Impl::IsNativeFocused() const {
  NSView* view = platform->view;
  NSResponder* responder = view.window.firstResponder;
  if (!responder) {
    return false;
  }
  if (responder == view) {
    return true;
  }
  // An editing text field hands first responder to the shared field editor.
  if ([responder isKindOfClass:[NSTextView class]]) {
    NSObject* delegate = (NSObject*)((NSTextView*)responder).delegate;
    return delegate == view;
  }
  return false;
}

void View::Impl::StartNativeListening() {
  platform->listening = true;
  platform->HookControl();
}

void View::Impl::StopNativeListening() {
  platform->listening = false;
  platform->UnhookControl();
}

void View::Impl::ObserveNativeResize(bool observe) {
  NSView* view = platform->view;
  if (!view) {
    return;
  }
  if (observe && !platform->frame_observer) {
    view.postsFrameChangedNotifications = YES;
    Impl* self = this;
    platform->frame_observer = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSViewFrameDidChangeNotification
                    object:view
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                  self->OnNativeResized();
                }];
#if !__has_feature(objc_arc)
    [platform->frame_observer retain];
#endif
  } else if (!observe && platform->frame_observer) {
    [[NSNotificationCenter defaultCenter] removeObserver:platform->frame_observer];
#if !__has_feature(objc_arc)
    [platform->frame_observer release];
#endif
    platform->frame_observer = nil;
  }
}

bool View::IsSupported() {
  return true;
}

bool View::IsBackendSupported(ViewBackend backend) {
  return backend == ViewBackend::Native;
}

bool View::SetDefaultBackend(ViewBackend backend) {
  return backend == ViewBackend::Native;
}

ViewBackend View::GetDefaultBackend() {
  return ViewBackend::Native;
}

// ---------------------------------------------------------------------------
// Window::GetContentView
// ---------------------------------------------------------------------------

std::shared_ptr<View> Window::GetContentView() const {
  NSWindow* ns_window = (__bridge NSWindow*)GetNativeObject();
  if (!ns_window || !ns_window.contentView) {
    return nullptr;
  }
  return ContentViewFor((__bridge void*)ns_window.contentView);
}

}  // namespace nativeapi
