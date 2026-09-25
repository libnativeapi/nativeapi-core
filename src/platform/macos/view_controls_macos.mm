// Label, Button, TextField and ImageView on AppKit.

#include "../../view.h"

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

NSTextField* MakeTextField(bool secure) {
  NSTextField* field = secure ? [[NativeApiSecureTextField alloc] initWithFrame:NSZeroRect]
                              : [[NativeApiTextField alloc] initWithFrame:NSZeroRect];
  field.editable = YES;
  field.selectable = YES;
  field.bezeled = YES;
  field.bezelStyle = NSTextFieldSquareBezel;
  field.drawsBackground = YES;
  return field;
}

void ApplyMultiline(NSTextField* field, bool multiline) {
  field.cell.wraps = multiline;
  field.cell.scrollable = !multiline;
  field.usesSingleLineMode = !multiline;
  field.maximumNumberOfLines = multiline ? 0 : 1;
  field.lineBreakMode = multiline ? NSLineBreakByWordWrapping : NSLineBreakByClipping;
}

// The text properties shared by Label and TextField.

void SetFieldTextColor(ViewInternal::Impl& impl, Color color) {
  NSTextField* field = (NSTextField*)impl.platform->view;
  impl.platform->text_color = color;
  field.textColor = color.a == 0 ? [NSColor textColor] : ToNSColor(color);
}

void SetFieldFontSize(ViewInternal::Impl& impl, double size) {
  NSTextField* field = (NSTextField*)impl.platform->view;
  impl.platform->font_size = size < 0 ? 0 : size;
  field.font = [NSFont systemFontOfSize:size > 0 ? size : [NSFont systemFontSize]];
  impl.InvalidateIntrinsicSize();
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
  SetFieldFontSize(*pimpl_, size);
}

double Label::GetFontSize() const {
  return pimpl_->platform->font_size;
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

// ---------------------------------------------------------------------------
// TextField
// ---------------------------------------------------------------------------

TextField::TextField(const std::string& text) : View(Adopt([&] {
  NSTextField* field = MakeTextField(false);
  field.stringValue = ToNSString(text);
  ApplyMultiline(field, false);
  return field;
}())) {}

void TextField::SetText(const std::string& text) {
  ((NSTextField*)pimpl_->platform->view).stringValue = ToNSString(text);
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
  SetFieldFontSize(*pimpl_, size);
}

double TextField::GetFontSize() const {
  return pimpl_->platform->font_size;
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
  NSTextField* field = MakeTextField(is_secure);
  field.stringValue = old_field.stringValue;
  field.placeholderString = old_field.placeholderString;
  field.editable = old_field.isEditable;
  field.enabled = old_field.isEnabled;
  field.alignment = old_field.alignment;
  field.font = old_field.font;
  field.textColor = old_field.textColor;
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
