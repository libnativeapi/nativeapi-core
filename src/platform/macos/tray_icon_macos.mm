#include <algorithm>
#include <optional>
#include "../../foundation/geometry.h"
#include "../../image.h"
#include "../../menu.h"
#include "../../positioning_strategy.h"
#include "../../tray_icon.h"
#include "../../view.h"
#include "../../view_impl.h"
#include "coordinate_utils_macos.h"
#include "view_internal_macos.h"

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

// Note: This file assumes ARC (Automatic Reference Counting) is enabled
// for proper memory management of Objective-C objects.

// Forward declarations
typedef void (^TrayIconClickedBlock)(void);
typedef void (^TrayIconRightClickedBlock)(void);
typedef void (^TrayIconDoubleClickedBlock)(void);

// Key for associated object to store tray icon ID
static const void* kTrayIconIdKey = &kTrayIconIdKey;

@interface NSStatusBarButtonTarget : NSObject
@property(nonatomic, copy) TrayIconClickedBlock left_clicked_callback_;
@property(nonatomic, copy) TrayIconRightClickedBlock right_clicked_callback_;
@property(nonatomic, copy) TrayIconDoubleClickedBlock double_clicked_callback_;
- (void)handleStatusItemEvent:(id)sender;
@end

// Holds the content view. It fills the status item's window, padding included,
// so the view covers the whole item and not just the button inside it.
// Top-left origin like every View container. Interactive controls get their own
// clicks; a click anywhere else is handed to the button's action, which turns
// it into tray icon events.
@interface NativeApiTrayContentHost : NSView
@property(nonatomic, assign) NSStatusBarButton* button;
// Called after the host changed size: the menu bar placed or re-sized the item.
@property(nonatomic, copy) void (^resized)(void);
@end

namespace nativeapi {

// Private implementation class
class TrayIcon::Impl {
 public:
  std::shared_ptr<Image> image_;
  bool icon_template_ = false;
  Size icon_size_ = Size{18, 18};
  TrayIconPosition icon_position_ = TrayIconPosition::Left;
  std::optional<std::string> title_;
  std::shared_ptr<View> content_view_;
  NativeApiTrayContentHost* content_host_ = nil;
  bool laying_out_content_ = false;

  Impl(NSStatusItem* status_item)
      : ns_status_item_(status_item),
        ns_status_bar_button_target_(nil),
        menu_closed_listener_id_(0),
        click_handler_setup_(false),
        context_menu_trigger_(ContextMenuTrigger::None) {
    if (status_item) {
      // Check if ID already exists in the associated object
      NSNumber* allocated_id = objc_getAssociatedObject(status_item, kTrayIconIdKey);
      if (allocated_id) {
        // Reuse allocated ID
        id_ = static_cast<TrayIconId>([allocated_id longValue]);
      } else {
        // Allocate new ID and store it
        id_ = IdAllocator::Allocate<TrayIcon>();
        objc_setAssociatedObject(status_item, kTrayIconIdKey, [NSNumber numberWithLong:id_],
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
      }
    }
  }

  ~Impl() {
    // Remove the menu closed listener before cleaning up
    if (context_menu_ && menu_closed_listener_id_ != 0) {
      context_menu_->RemoveListener(menu_closed_listener_id_);
      menu_closed_listener_id_ = 0;
    }

    // Clean up event handlers if they were set up
    if (click_handler_setup_) {
      CleanupEventHandlers();
    }

    // The content view's layout hook points at this Impl.
    DetachContentView();

    // Then clean up the status item
    if (ns_status_item_) {
      // Clear menu reference
      ns_status_item_.menu = nil;

      // Clean up associated object
      objc_setAssociatedObject(ns_status_item_, kTrayIconIdKey, nil,
                               OBJC_ASSOCIATION_RETAIN_NONATOMIC);

      [[NSStatusBar systemStatusBar] removeStatusItem:ns_status_item_];
      ns_status_item_ = nil;
    }

    // Finally, safely clean up context_menu_ after all UI references are cleared
    if (context_menu_) {
      context_menu_.reset();  // Explicitly reset shared_ptr
    }
  }

  // Puts image_ on the button the way the icon properties ask for. Works on a
  // copy: the NSImage belongs to the caller's Image, which may be in use
  // elsewhere (a menu item, another tray icon) at another size.
  void ApplyIcon() {
    if (!ns_status_item_ || !ns_status_item_.button) {
      return;
    }
    NSStatusBarButton* button = ns_status_item_.button;
    button.imagePosition = icon_position_ == TrayIconPosition::Right ? NSImageRight : NSImageLeft;

    // A content view replaces the image; image_ stays for when it is cleared.
    NSImage* source = image_ && !content_view_ ? (__bridge NSImage*)image_->GetNativeObject() : nil;
    if (!source) {
      [button setImage:nil];
      return;
    }

    NSImage* ns_image = [source copy];
    if (icon_size_.width > 0 && icon_size_.height > 0) {
      [ns_image setSize:NSMakeSize(icon_size_.width, icon_size_.height)];
    }
    [ns_image setTemplate:icon_template_ ? YES : NO];
    [button setImage:ns_image];
  }

  // Puts title_ on the button, or nothing while a content view is shown.
  void ApplyTitle() {
    if (!ns_status_item_ || !ns_status_item_.button) {
      return;
    }
    NSString* title = @"";
    if (title_.has_value() && !content_view_) {
      title = [NSString stringWithUTF8String:title_->c_str()];
    }
    [ns_status_item_.button setTitle:title ?: @""];
  }

  void AttachContentView(std::shared_ptr<View> view) {
    if (!ns_status_item_ || !ns_status_item_.button) {
      return;
    }
    NSView* native = (__bridge NSView*)view->GetNativeObject();
    if (!native) {
      return;
    }
    if (auto parent = view->GetParent()) {
      parent->RemoveSubview(view);
    }

    NSStatusBarButton* button = ns_status_item_.button;
    // The button sits inset in the item's window; the window's content view is
    // the whole item.
    NSView* container = button.window.contentView ?: button;
    content_host_ = [[NativeApiTrayContentHost alloc] initWithFrame:container.bounds];
    content_host_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    content_host_.button = button;
    [container addSubview:content_host_];
    [content_host_ addSubview:native];
    content_view_ = std::move(view);

    // Every layout pass of the view's tree (a label's text changing, a
    // subview added, a new preferred size) sizes the item first, and so does
    // the menu bar giving the item its final height once it is placed.
    auto& impl = ViewInternal::Of(*content_view_);
    impl.host_layout = [this] { LayoutContentView(); };
    content_host_.resized = ^{
      if (content_view_ && !laying_out_content_) {
        ViewInternal::Of(*content_view_).RelayoutTree();
      }
    };
    impl.RelayoutTree();
  }

  void DetachContentView() {
    if (!content_view_) {
      return;
    }
    auto& impl = ViewInternal::Of(*content_view_);
    impl.host_layout = nullptr;
    content_host_.resized = nil;
    NSView* native = (__bridge NSView*)content_view_->GetNativeObject();
    if (native && native.superview == content_host_) {
      [native removeFromSuperview];
    }
    [content_host_ removeFromSuperview];
#if !__has_feature(objc_arc)
    [content_host_ release];
#endif
    content_host_ = nil;
    content_view_.reset();
    if (ns_status_item_) {
      ns_status_item_.length = NSVariableStatusItemLength;
    }
  }

  // Sizes the item to the content view and centres the view in the item.
  void LayoutContentView() {
    if (!content_view_ || !content_host_ || !ns_status_item_ || !ns_status_item_.button) {
      return;
    }
    laying_out_content_ = true;
    auto& impl = ViewInternal::Of(*content_view_);
    const Size preferred = content_view_->GetPreferredSize();
    const Size intrinsic = impl.IntrinsicSize();
    const double width = preferred.width > 0 ? preferred.width : intrinsic.width;

    // The item is the status item's length plus the padding the menu bar puts
    // around the button (none before macOS 26); the length asked for leaves
    // room for it so the whole item is `width` wide.
    // The window follows the length synchronously, so the padding is measured
    // after the first assignment; before the item exists there is none to see.
    NSStatusBarButton* button = ns_status_item_.button;
    auto padding = [&] {
      return std::max<CGFloat>(0, content_host_.bounds.size.width - button.frame.size.width);
    };
    ns_status_item_.length = std::max<CGFloat>(0, width - padding());
    ns_status_item_.length = std::max<CGFloat>(0, width - padding());

    // Until the item is placed its window has no height yet.
    CGFloat bar = content_host_.bounds.size.height;
    if (bar <= 0) {
      bar = [[NSStatusBar systemStatusBar] thickness];
    }
    const double height = preferred.height > 0 ? preferred.height : bar;
    // Stretched to the item's real width: the menu bar rounds the length, and
    // an item may not be narrower than its padding. A sliver left uncovered at
    // either edge would show the menu bar through it.
    const double item_width = std::max<double>(width, content_host_.bounds.size.width);
    impl.SetNativeFrame(Rectangle{0, (bar - height) / 2, item_width, height});
    laying_out_content_ = false;
  }

  void SetupEventHandlers() {
    if (click_handler_setup_) {
      return;  // Already set up
    }

    if (!ns_status_item_ || !ns_status_item_.button) {
      return;
    }

    // Create and set up button target
    ns_status_bar_button_target_ = [[NSStatusBarButtonTarget alloc] init];

    // Set up event handlers
    [ns_status_item_.button setTarget:ns_status_bar_button_target_];
    [ns_status_item_.button setAction:@selector(handleStatusItemEvent:)];

    // Enable right-click handling
    [ns_status_item_.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];

    click_handler_setup_ = true;
  }

  void CleanupEventHandlers() {
    if (!click_handler_setup_) {
      return;  // Not set up
    }

    // Clean up blocks first
    if (ns_status_bar_button_target_) {
      ns_status_bar_button_target_.left_clicked_callback_ = nil;
      ns_status_bar_button_target_.right_clicked_callback_ = nil;
      ns_status_bar_button_target_.double_clicked_callback_ = nil;
      ns_status_bar_button_target_ = nil;
    }

    // Remove target and action to prevent callbacks after destruction
    if (ns_status_item_ && ns_status_item_.button) {
      [ns_status_item_.button setTarget:nil];
      [ns_status_item_.button setAction:nil];
    }

    click_handler_setup_ = false;
  }

  NSStatusItem* ns_status_item_;
  NSStatusBarButtonTarget* ns_status_bar_button_target_;

  TrayIconId id_;
  std::shared_ptr<Menu> context_menu_;
  size_t menu_closed_listener_id_;
  bool click_handler_setup_;
  ContextMenuTrigger context_menu_trigger_;
};

TrayIcon::TrayIcon() : TrayIcon(nullptr) {}

TrayIcon::TrayIcon(void* tray) {
  NSStatusItem* status_item = nullptr;

  if (tray == nullptr) {
    // Create platform-specific NSStatusItem
    NSStatusBar* status_bar = [NSStatusBar systemStatusBar];
    status_item = [status_bar statusItemWithLength:NSVariableStatusItemLength];
  } else {
    status_item = (__bridge NSStatusItem*)tray;
  }

  // Initialize the Impl with the status item
  pimpl_ = std::make_unique<Impl>(status_item);

  // Event handlers will be set up automatically when first listener is added
  // via StartEventListening() override
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::StartEventListening() {
  // Called automatically when first listener is added
  // Set up platform event monitoring
  pimpl_->SetupEventHandlers();

  // Set up click handler blocks
  if (pimpl_->ns_status_bar_button_target_) {
    pimpl_->ns_status_bar_button_target_.left_clicked_callback_ = ^{
      Emit<TrayIconClickedEvent>(pimpl_->id_);
      // Auto-trigger context menu if configured
      if (pimpl_->context_menu_trigger_ == ContextMenuTrigger::Clicked) {
        OpenContextMenu();
      }
    };

    pimpl_->ns_status_bar_button_target_.right_clicked_callback_ = ^{
      Emit<TrayIconRightClickedEvent>(pimpl_->id_);
      // Auto-trigger context menu if configured
      if (pimpl_->context_menu_trigger_ == ContextMenuTrigger::RightClicked) {
        OpenContextMenu();
      }
    };

    pimpl_->ns_status_bar_button_target_.double_clicked_callback_ = ^{
      Emit<TrayIconDoubleClickedEvent>(pimpl_->id_);
      // Auto-trigger context menu if configured
      if (pimpl_->context_menu_trigger_ == ContextMenuTrigger::DoubleClicked) {
        OpenContextMenu();
      }
    };
  }
}

void TrayIcon::StopEventListening() {
  // Called automatically when last listener is removed
  // Clean up platform event monitoring
  pimpl_->CleanupEventHandlers();
}

TrayIconId TrayIcon::GetId() {
  return pimpl_->id_;
}

void TrayIcon::SetIcon(std::shared_ptr<Image> image) {
  pimpl_->image_ = image;
  pimpl_->ApplyIcon();
}

std::shared_ptr<Image> TrayIcon::GetIcon() const {
  return pimpl_->image_;
}

void TrayIcon::SetIconTemplate(bool is_icon_template) {
  pimpl_->icon_template_ = is_icon_template;
  pimpl_->ApplyIcon();
}

bool TrayIcon::IsIconTemplate() const {
  return pimpl_->icon_template_;
}

void TrayIcon::SetIconSize(Size size) {
  pimpl_->icon_size_ = size;
  pimpl_->ApplyIcon();
}

Size TrayIcon::GetIconSize() const {
  return pimpl_->icon_size_;
}

void TrayIcon::SetIconPosition(TrayIconPosition position) {
  pimpl_->icon_position_ = position;
  pimpl_->ApplyIcon();
}

TrayIconPosition TrayIcon::GetIconPosition() const {
  return pimpl_->icon_position_;
}

void TrayIcon::SetTitle(std::optional<std::string> title) {
  pimpl_->title_ = std::move(title);
  pimpl_->ApplyTitle();
}

std::optional<std::string> TrayIcon::GetTitle() {
  if (pimpl_->title_.has_value() && !pimpl_->title_->empty()) {
    return pimpl_->title_;
  }
  return std::nullopt;
}

void TrayIcon::SetTooltip(std::optional<std::string> tooltip) {
  if (pimpl_->ns_status_item_ && pimpl_->ns_status_item_.button) {
    if (tooltip.has_value()) {
      NSString* tooltip_string = [NSString stringWithUTF8String:tooltip.value().c_str()];
      [pimpl_->ns_status_item_.button setToolTip:tooltip_string];
    } else {
      [pimpl_->ns_status_item_.button setToolTip:nil];
    }
  }
}

std::optional<std::string> TrayIcon::GetTooltip() {
  if (pimpl_->ns_status_item_ && pimpl_->ns_status_item_.button) {
    NSString* tooltip_string = [pimpl_->ns_status_item_.button toolTip];
    if (tooltip_string && [tooltip_string length] > 0) {
      return std::string([tooltip_string UTF8String]);
    }
  }
  return std::nullopt;
}

void TrayIcon::SetContentView(std::shared_ptr<View> view) {
  if (view == pimpl_->content_view_) {
    return;
  }
  pimpl_->DetachContentView();
  if (view) {
    pimpl_->AttachContentView(std::move(view));
  }
  pimpl_->ApplyIcon();
  pimpl_->ApplyTitle();
}

std::shared_ptr<View> TrayIcon::GetContentView() const {
  return pimpl_->content_view_;
}

void TrayIcon::SetContextMenu(std::shared_ptr<Menu> menu) {
  // Remove previous menu listener if it exists
  if (pimpl_->context_menu_ && pimpl_->menu_closed_listener_id_ != 0) {
    pimpl_->context_menu_->RemoveListener(pimpl_->menu_closed_listener_id_);
    pimpl_->menu_closed_listener_id_ = 0;
  }

  // Store the menu reference
  // Don't set the menu directly to the status item, as this would cause
  // macOS to take over click handling and prevent our custom click events
  // Instead, we'll show the menu manually in our click handler
  pimpl_->context_menu_ = menu;

  if (pimpl_->context_menu_) {
    auto pimpl_raw = pimpl_.get();
    pimpl_->menu_closed_listener_id_ = pimpl_->context_menu_->AddListener<MenuClosedEvent>(
        [pimpl_raw](const MenuClosedEvent& event) {
          if (pimpl_raw && pimpl_raw->ns_status_item_) {
            pimpl_raw->ns_status_item_.menu = nil;
          }
        });
  }
}

std::shared_ptr<Menu> TrayIcon::GetContextMenu() {
  return pimpl_->context_menu_;
}

Rectangle TrayIcon::GetBounds() {
  Rectangle bounds = {0, 0, 0, 0};

  if (pimpl_->ns_status_item_ && pimpl_->ns_status_item_.button && pimpl_->ns_status_item_.button.window) {
    // With a content view the whole item is what the user sees and clicks.
    NSView* item = pimpl_->content_host_ ? (NSView*)pimpl_->content_host_
                                         : (NSView*)pimpl_->ns_status_item_.button;
    NSRect window_rect = [item convertRect:item.bounds toView:nil];
    NSRect screen_rect = [item.window convertRectToScreen:window_rect];

    // Flip against the primary screen ([NSScreen screens][0]), matching every
    // other coordinate conversion in this library. Do NOT use mainScreen here:
    // it is the screen of the current key window, which makes the result depend
    // on where keyboard focus happens to be on multi-display setups.
    CGPoint top_left = NSRectExt::topLeft(screen_rect);

    bounds.x = top_left.x;
    bounds.y = top_left.y;
    bounds.width = screen_rect.size.width;
    bounds.height = screen_rect.size.height;
  }

  return bounds;
}

bool TrayIcon::SetVisible(bool visible) {
  if (!pimpl_->ns_status_item_) {
    return false;
  }

  [pimpl_->ns_status_item_ setVisible:visible ? YES : NO];
  return true;
}

bool TrayIcon::IsVisible() {
  if (pimpl_->ns_status_item_) {
    return [pimpl_->ns_status_item_ isVisible] == YES;
  }
  return false;
}

bool TrayIcon::OpenContextMenu() {
  if (!pimpl_->context_menu_ || !pimpl_->ns_status_item_ || !pimpl_->ns_status_item_.button) {
    return false;
  }

  // Use the Swift approach: set menu to status item and simulate click
  // Get the native NSMenu object from our Menu wrapper
  NSMenu* native_menu = (__bridge NSMenu*)pimpl_->context_menu_->GetNativeObject();
  if (!native_menu) {
    return false;
  }

  //  // Set our menu delegate to handle menu close events
  //  [nativeMenu setDelegate:pimpl_->menu_delegate_];

  // Set the menu to the status item (like Swift version)
  pimpl_->ns_status_item_.menu = native_menu;

  // Simulate a click to show the menu (like Swift version)
  [pimpl_->ns_status_item_.button performClick:nil];

  return true;
}

bool TrayIcon::CloseContextMenu() {
  if (!pimpl_->context_menu_) {
    return true;  // No menu to close, consider success
  }

  // Close the context menu
  return pimpl_->context_menu_->Close();
}

void TrayIcon::SetContextMenuTrigger(ContextMenuTrigger trigger) {
  pimpl_->context_menu_trigger_ = trigger;
}

ContextMenuTrigger TrayIcon::GetContextMenuTrigger() {
  return pimpl_->context_menu_trigger_;
}

void* TrayIcon::GetNativeObjectInternal() const {
  return (__bridge void*)pimpl_->ns_status_item_;
}

}  // namespace nativeapi

// Implementation of NSStatusBarButtonTarget
@implementation NSStatusBarButtonTarget

- (void)handleStatusItemEvent:(id)sender {
  NSEvent* event = [NSApp currentEvent];
  if (!event)
    return;

  // Check the type of click and call appropriate block
  if (event.type == NSEventTypeRightMouseUp ||
      (event.type == NSEventTypeLeftMouseUp &&
       (event.modifierFlags & NSEventModifierFlagControl))) {
    // Right click or Ctrl+Left click
    if (_right_clicked_callback_) {
      _right_clicked_callback_();
    }
  } else if (event.type == NSEventTypeLeftMouseUp) {
    // Check for double click
    if (event.clickCount == 2) {
      if (_double_clicked_callback_) {
        _double_clicked_callback_();
      }
    } else {
      if (_left_clicked_callback_) {
        _left_clicked_callback_();
      }
    }
  }
}

@end

@implementation NativeApiTrayContentHost

- (BOOL)isFlipped {
  return YES;
}

- (void)setFrameSize:(NSSize)size {
  const NSSize old_size = self.frame.size;
  [super setFrameSize:size];
  if (!NSEqualSizes(old_size, size) && self.resized) {
    self.resized();
  }
}

// Containers, labels and image views take no clicks of their own: the host
// takes them instead and passes them on to the button.
- (NSView*)hitTest:(NSPoint)point {
  NSView* hit = [super hitTest:point];
  if (!hit || [hit isKindOfClass:[NativeApiContainerView class]] ||
      [hit isKindOfClass:[NSImageView class]]) {
    return hit ? self : nil;
  }
  if ([hit isKindOfClass:[NSTextField class]]) {
    NSTextField* field = (NSTextField*)hit;
    if (!field.isEditable && !field.isSelectable) {
      return self;
    }
  }
  return hit;
}

// No highlight: the content view draws the whole item, and the system's
// highlight would only show around its edges.
- (void)mouseDown:(NSEvent*)event {
}

- (void)rightMouseDown:(NSEvent*)event {
}

- (void)mouseUp:(NSEvent*)event {
  [self sendButtonActionFor:event];
}

- (void)rightMouseUp:(NSEvent*)event {
  [self sendButtonActionFor:event];
}

// The button's action reads the current event to tell left, right and double
// clicks apart, as it does for a click on the button itself.
- (void)sendButtonActionFor:(NSEvent*)event {
  NSStatusBarButton* button = self.button;
  NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  if (button && button.action && NSPointInRect(point, self.bounds)) {
    [NSApp sendAction:button.action to:button.target from:button];
  }
}

@end
