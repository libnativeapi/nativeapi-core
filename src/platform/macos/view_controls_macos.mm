// Label, Button, TextField and ImageView on AppKit.

#include "../../view.h"

#include <algorithm>
#include <string>

#include "view_internal_macos.h"

namespace nativeapi {

namespace {

NSString* ToNSString(const std::string& text) {
  return [NSString stringWithUTF8String:text.c_str()] ?: @"";
}

std::string FromNSString(NSString* text) {
  return std::string(text.UTF8String ?: "");
}

/// A control the view will own: alloc'd here, adopted by View(NativeControl).
ViewInternal::NativeControl Adopt(NSView* view) {
  view.autoresizingMask = NSViewNotSizable;
#if !__has_feature(objc_arc)
  [view autorelease];
#endif
  return ViewInternal::NativeControl{(__bridge void*)view};
}

NSTextField* MakeLabel(const std::string& text) {
  NSTextField* field = [[NSTextField alloc] initWithFrame:NSZeroRect];
  field.stringValue = ToNSString(text);
  field.editable = NO;
  field.selectable = NO;
  field.bezeled = NO;
  field.bordered = NO;
  field.drawsBackground = NO;
  field.lineBreakMode = NSLineBreakByWordWrapping;
  field.cell.wraps = YES;
  field.cell.scrollable = NO;
  field.maximumNumberOfLines = 0;
  return field;
}

NSTextField* MakeTextField(bool secure, bool bordered) {
  NSTextField* field = secure ? [[NativeApiSecureTextField alloc] initWithFrame:NSZeroRect]
                              : [[NativeApiTextField alloc] initWithFrame:NSZeroRect];
  field.editable = YES;
  field.selectable = YES;
  field.bezeled = bordered;
  if (bordered) {
    field.bezelStyle = NSTextFieldSquareBezel;
  }
  field.bordered = bordered;
  field.drawsBackground = bordered;
  field.focusRingType = bordered ? NSFocusRingTypeDefault : NSFocusRingTypeNone;
  return field;
}

void ApplyMultiline(NSTextField* field, bool multiline) {
  field.cell.wraps = multiline;
  field.cell.scrollable = !multiline;
  field.usesSingleLineMode = !multiline;
  field.maximumNumberOfLines = multiline ? 0 : 1;
  field.lineBreakMode = multiline ? NSLineBreakByWordWrapping : NSLineBreakByClipping;
}

NSFontWeight ToNSFontWeight(int weight) {
  if (weight <= 150) return NSFontWeightUltraLight;
  if (weight <= 250) return NSFontWeightThin;
  if (weight <= 350) return NSFontWeightLight;
  if (weight <= 450) return NSFontWeightRegular;
  if (weight <= 550) return NSFontWeightMedium;
  if (weight <= 650) return NSFontWeightSemibold;
  if (weight <= 750) return NSFontWeightBold;
  if (weight <= 850) return NSFontWeightHeavy;
  return NSFontWeightBlack;
}

void ApplyControlFont(ViewInternal::Impl& impl) {
  NSView* view = impl.platform->view;
  if (![view isKindOfClass:[NSControl class]]) {
    return;
  }
  NSControl* control = (NSControl*)view;
  auto& p = *impl.platform;
  const CGFloat default_size =
      [view isKindOfClass:[NSButton class]]
          ? [NSFont systemFontSizeForControlSize:control.controlSize]
          : [NSFont systemFontSize];
  const CGFloat size = p.font_size > 0.0 ? static_cast<CGFloat>(p.font_size) : default_size;
  const bool has_family =
      p.font_family.has_value() && !p.font_family->empty() && *p.font_family != "system-ui";
  const NSFontWeight ns_weight =
      p.font_weight > 0 ? ToNSFontWeight(p.font_weight) : NSFontWeightRegular;

  NSFont* font = nil;
  if (!has_family) {
    font = p.font_weight > 0 ? [NSFont systemFontOfSize:size weight:ns_weight]
                             : [NSFont systemFontOfSize:size];
  } else if (*p.font_family == "monospace" || *p.font_family == "ui-monospace") {
    font = [NSFont monospacedSystemFontOfSize:size weight:ns_weight];
  } else {
    NSString* family_name = ToNSString(*p.font_family);
    NSDictionary* traits = p.font_weight > 0 ? @{NSFontWeightTrait : @(ns_weight)} : @{};
    NSFontDescriptor* desc = [NSFontDescriptor fontDescriptorWithFontAttributes:@{
      NSFontFamilyAttribute : family_name,
      NSFontTraitsAttribute : traits,
    }];
    font = [NSFont fontWithDescriptor:desc size:size];
    if (!font) {
      font = p.font_weight > 0 ? [NSFont systemFontOfSize:size weight:ns_weight]
                               : [NSFont systemFontOfSize:size];
    }
  }
  control.font = font;
  impl.InvalidateIntrinsicSize();
}

// The text and font properties shared by Label, Button and TextField.

void SetFieldTextColor(ViewInternal::Impl& impl, Color color) {
  NSTextField* field = (NSTextField*)impl.platform->view;
  impl.platform->text_color = color;
  field.textColor = color.a == 0 ? [NSColor textColor] : ToNSColor(color);
}

void SetControlFontSize(ViewInternal::Impl& impl, double size) {
  impl.platform->font_size = size < 0 ? 0 : size;
  ApplyControlFont(impl);
}

void SetControlFontFamily(ViewInternal::Impl& impl, const std::optional<std::string>& family) {
  impl.platform->font_family = family;
  ApplyControlFont(impl);
}

void SetControlFontWeight(ViewInternal::Impl& impl, int weight) {
  impl.platform->font_weight = weight <= 0 ? 0 : std::clamp(weight, 100, 900);
  ApplyControlFont(impl);
}

}  // namespace

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------

Label::Label(const std::string& text) : View(Adopt(MakeLabel(text))) {}

void Label::SetText(const std::string& text) {
  ((NSTextField*)pimpl_->platform->view).stringValue = ToNSString(text);
  pimpl_->InvalidateIntrinsicSize();
}

std::string Label::GetText() const {
  return FromNSString(((NSTextField*)pimpl_->platform->view).stringValue);
}

void Label::SetTextColor(Color color) {
  SetFieldTextColor(*pimpl_, color);
}

Color Label::GetTextColor() const {
  return pimpl_->platform->text_color;
}

void Label::SetFontSize(double size) {
  SetControlFontSize(*pimpl_, size);
}

double Label::GetFontSize() const {
  return pimpl_->platform->font_size;
}

void Label::SetFontFamily(const std::optional<std::string>& family) {
  SetControlFontFamily(*pimpl_, family);
}

std::optional<std::string> Label::GetFontFamily() const {
  return pimpl_->platform->font_family;
}

void Label::SetFontWeight(int weight) {
  SetControlFontWeight(*pimpl_, weight);
}

int Label::GetFontWeight() const {
  return pimpl_->platform->font_weight;
}

void Label::SetTextAlignment(TextAlignment alignment) {
  ((NSTextField*)pimpl_->platform->view).alignment = ToNSTextAlignment(alignment);
}

TextAlignment Label::GetTextAlignment() const {
  return FromNSTextAlignment(((NSTextField*)pimpl_->platform->view).alignment);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

Button::Button(const std::string& text) : View(Adopt([&] {
  NSButton* button = [[NSButton alloc] initWithFrame:NSZeroRect];
  button.title = ToNSString(text);
  button.bezelStyle = NSBezelStyleRounded;
  [button setButtonType:NSButtonTypeMomentaryPushIn];
  return button;
}())) {}

void Button::SetText(const std::string& text) {
  ((NSButton*)pimpl_->platform->view).title = ToNSString(text);
  pimpl_->InvalidateIntrinsicSize();
}

std::string Button::GetText() const {
  return FromNSString(((NSButton*)pimpl_->platform->view).title);
}

void Button::SetFontSize(double size) {
  SetControlFontSize(*pimpl_, size);
}

double Button::GetFontSize() const {
  return pimpl_->platform->font_size;
}

void Button::SetFontFamily(const std::optional<std::string>& family) {
  SetControlFontFamily(*pimpl_, family);
}

std::optional<std::string> Button::GetFontFamily() const {
  return pimpl_->platform->font_family;
}

void Button::SetFontWeight(int weight) {
  SetControlFontWeight(*pimpl_, weight);
}

int Button::GetFontWeight() const {
  return pimpl_->platform->font_weight;
}

// ---------------------------------------------------------------------------
// TextField
// ---------------------------------------------------------------------------

TextField::TextField(const std::string& text) : View(Adopt([&] {
  NSTextField* field = MakeTextField(false, true);
  field.stringValue = ToNSString(text);
  ApplyMultiline(field, false);
  return field;
}())) {}

void TextField::SetText(const std::string& text) {
  NSTextField* field = (NSTextField*)pimpl_->platform->view;
  if (FromNSString(field.stringValue) == text) {
    return;
  }
  field.stringValue = ToNSString(text);
}

std::string TextField::GetText() const {
  return FromNSString(((NSTextField*)pimpl_->platform->view).stringValue);
}

void TextField::SetTextColor(Color color) {
  SetFieldTextColor(*pimpl_, color);
}

Color TextField::GetTextColor() const {
  return pimpl_->platform->text_color;
}

void TextField::SetFontSize(double size) {
  SetControlFontSize(*pimpl_, size);
}

double TextField::GetFontSize() const {
  return pimpl_->platform->font_size;
}

void TextField::SetFontFamily(const std::optional<std::string>& family) {
  SetControlFontFamily(*pimpl_, family);
}

std::optional<std::string> TextField::GetFontFamily() const {
  return pimpl_->platform->font_family;
}

void TextField::SetFontWeight(int weight) {
  SetControlFontWeight(*pimpl_, weight);
}

int TextField::GetFontWeight() const {
  return pimpl_->platform->font_weight;
}

void TextField::SetTextAlignment(TextAlignment alignment) {
  ((NSTextField*)pimpl_->platform->view).alignment = ToNSTextAlignment(alignment);
}

TextAlignment TextField::GetTextAlignment() const {
  return FromNSTextAlignment(((NSTextField*)pimpl_->platform->view).alignment);
}

void TextField::SetPlaceholder(const std::optional<std::string>& placeholder) {
  ((NSTextField*)pimpl_->platform->view).placeholderString =
      placeholder ? ToNSString(*placeholder) : nil;
}

std::optional<std::string> TextField::GetPlaceholder() const {
  NSString* placeholder = ((NSTextField*)pimpl_->platform->view).placeholderString;
  if (!placeholder) {
    return std::nullopt;
  }
  return FromNSString(placeholder);
}

void TextField::SetEditable(bool is_editable) {
  ((NSTextField*)pimpl_->platform->view).editable = is_editable;
}

bool TextField::IsEditable() const {
  return ((NSTextField*)pimpl_->platform->view).isEditable;
}

void TextField::SetSecure(bool is_secure) {
  auto& platform = *pimpl_->platform;
  if (platform.secure == is_secure) {
    return;
  }
  // NSSecureTextField is a distinct class: rebuild the control and carry the
  // state over.
  NSTextField* old_field = (NSTextField*)platform.view;
  NSTextField* field = MakeTextField(is_secure, platform.bordered);
  field.stringValue = old_field.stringValue;
  field.placeholderString = old_field.placeholderString;
  field.editable = old_field.isEditable;
  field.enabled = old_field.isEnabled;
  field.alignment = old_field.alignment;
  field.font = old_field.font;
  field.textColor = old_field.textColor;
  field.drawsBackground = old_field.drawsBackground;
  field.backgroundColor = old_field.backgroundColor;
  ApplyMultiline(field, platform.multiline);
  platform.secure = is_secure;
  platform.ReplaceView(field);
#if !__has_feature(objc_arc)
  [field release];
#endif
}

bool TextField::IsSecure() const {
  return pimpl_->platform->secure;
}

void TextField::SetMultiline(bool is_multiline) {
  auto& platform = *pimpl_->platform;
  platform.multiline = is_multiline;
  ApplyMultiline((NSTextField*)platform.view, is_multiline);
  if (platform.bridge) {
    platform.bridge.multiline = is_multiline;
  }
  pimpl_->InvalidateIntrinsicSize();
}

bool TextField::IsMultiline() const {
  return pimpl_->platform->multiline;
}

void TextField::SetBordered(bool is_bordered) {
  auto& platform = *pimpl_->platform;
  if (platform.bordered == is_bordered) {
    return;
  }
  platform.bordered = is_bordered;
  NSTextField* field = (NSTextField*)platform.view;
  field.bezeled = is_bordered;
  if (is_bordered) {
    field.bezelStyle = NSTextFieldSquareBezel;
  }
  field.bordered = is_bordered;
  field.focusRingType = is_bordered ? NSFocusRingTypeDefault : NSFocusRingTypeNone;
  if (pimpl_->background_color.a == 0) {
    field.drawsBackground = field.isEditable && is_bordered;
  }
  pimpl_->InvalidateIntrinsicSize();
}

bool TextField::IsBordered() const {
  return pimpl_->platform->bordered;
}

void TextField::SetSelection(TextRange range) {
  NSTextField* field = (NSTextField*)pimpl_->platform->view;
  NSText* editor = field.currentEditor;
  if (!editor) {
    return;
  }
  const NSUInteger total = editor.string.length;
  const NSUInteger start = range.start > total ? total : static_cast<NSUInteger>(range.start);
  const NSUInteger max_len = total - start;
  const NSUInteger len = range.length > max_len ? max_len : static_cast<NSUInteger>(range.length);
  editor.selectedRange = NSMakeRange(start, len);
}

TextRange TextField::GetSelection() const {
  NSTextField* field = (NSTextField*)pimpl_->platform->view;
  NSText* editor = field.currentEditor;
  if (!editor) {
    return TextRange{0, 0};
  }
  const NSRange sel = editor.selectedRange;
  if (sel.location == NSNotFound) {
    return TextRange{0, 0};
  }
  return TextRange{static_cast<size_t>(sel.location), static_cast<size_t>(sel.length)};
}

bool TextField::IsComposing() const {
  NSTextField* field = (NSTextField*)pimpl_->platform->view;
  NSText* editor = field.currentEditor;
  if (!editor || ![editor conformsToProtocol:@protocol(NSTextInputClient)]) {
    return false;
  }
  return [(id<NSTextInputClient>)editor hasMarkedText] == YES;
}

// ---------------------------------------------------------------------------
// ImageView
// ---------------------------------------------------------------------------

ImageView::ImageView() : View(Adopt([] {
  NSImageView* view = [[NSImageView alloc] initWithFrame:NSZeroRect];
  view.imageScaling = NSImageScaleProportionallyUpOrDown;
  view.imageAlignment = NSImageAlignCenter;
  return view;
}())) {}

void ImageView::SetImage(std::shared_ptr<Image> image) {
  pimpl_->platform->image = image;
  ((NSImageView*)pimpl_->platform->view).image =
      image ? (__bridge NSImage*)image->GetNativeObject() : nil;
  pimpl_->InvalidateIntrinsicSize();
}

std::shared_ptr<Image> ImageView::GetImage() const {
  return pimpl_->platform->image;
}

}  // namespace nativeapi
