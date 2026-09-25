#pragma once

// Shared between view_macos.mm and view_controls_macos.mm: the platform half of
// View::Impl and the Objective-C glue that turns AppKit callbacks into events.

#import <Cocoa/Cocoa.h>

#include <memory>

#include "../../image.h"
#include "../../view_impl.h"

namespace nativeapi {
class View;
}

/// Target / delegate for the controls: forwards actions and text-field
/// notifications to the owning View::Impl.
@interface NativeApiViewBridge : NSObject <NSTextFieldDelegate>
@property(nonatomic, assign) nativeapi::ViewInternal::Impl* impl;
@property(nonatomic, assign) BOOL multiline;
- (void)buttonClicked:(id)sender;
@end

/// Plain container with a top-left origin, so subview frames need no flipping.
@interface NativeApiContainerView : NSView
@end

/// Text fields that report focus themselves. The delegate's begin / end editing
/// callbacks only come once the user types; focus arrives earlier, when the
/// field becomes first responder, and leaves when its field editor ends.
@interface NativeApiTextField : NSTextField
@end
@interface NativeApiSecureTextField : NSSecureTextField
@end

namespace nativeapi {

struct View::Impl::Platform {
  Platform(Impl* impl, NSView* view);
  ~Platform();

  Impl* impl;
  NSView* view;
  NativeApiViewBridge* bridge = nil;
  NSObject* frame_observer = nil;
  bool listening = false;

  // Text controls remember what "default" was so a zero-alpha colour or a
  // zero size can restore it.
  Color text_color{0, 0, 0, 0};
  double font_size = 0.0;
  bool secure = false;
  bool multiline = false;
  std::shared_ptr<Image> image;

  /// Swaps the native control for `replacement`, keeping frame, superview,
  /// z-order and the listening state. Used when a text field turns secure.
  void ReplaceView(NSView* replacement);
  /// (Re)applies target / action / delegate to the current control.
  void HookControl();
  void UnhookControl();
};

/// Reads the CGFloat size AppKit reports as "no intrinsic metric" as 0.
inline double IntrinsicMetric(CGFloat value) {
  return value < 0 ? 0.0 : static_cast<double>(value);
}

inline NSColor* ToNSColor(Color color) {
  return [NSColor colorWithSRGBRed:color.r / 255.0
                             green:color.g / 255.0
                              blue:color.b / 255.0
                             alpha:color.a / 255.0];
}

inline NSTextAlignment ToNSTextAlignment(TextAlignment alignment) {
  switch (alignment) {
    case TextAlignment::Center:
      return NSTextAlignmentCenter;
    case TextAlignment::End:
      return NSTextAlignmentRight;
    case TextAlignment::Start:
    default:
      return NSTextAlignmentLeft;
  }
}

inline TextAlignment FromNSTextAlignment(NSTextAlignment alignment) {
  switch (alignment) {
    case NSTextAlignmentCenter:
      return TextAlignment::Center;
    case NSTextAlignmentRight:
      return TextAlignment::End;
    default:
      return TextAlignment::Start;
  }
}

}  // namespace nativeapi
