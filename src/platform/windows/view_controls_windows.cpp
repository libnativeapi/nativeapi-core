// Label, Button, TextField and ImageView on Win32 common controls.

#include "../../view.h"

#include <string>

#include "view_internal_windows.h"
#include "window_message_dispatcher.h"

namespace nativeapi {

namespace {

/// A control the view will own: created under the hidden host window and
/// reparented by AddNativeSubview. nullptr when the host is unavailable; every
/// seam function tolerates that.
ViewInternal::NativeControl Adopt(HWND hwnd) {
  return ViewInternal::NativeControl{hwnd};
}

HWND CreateControl(const wchar_t* class_name, DWORD style, DWORD ex_style) {
  EnsureCommonControls();
  HWND host = WindowMessageDispatcher::GetInstance().GetHostWindow();
  if (!host) {
    return nullptr;  // a WS_CHILD needs a parent
  }
  return CreateWindowExW(ex_style, class_name, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                         host, nullptr, GetModuleHandleW(nullptr), nullptr);
}

DWORD StaticAlignmentStyle(TextAlignment alignment) {
  switch (alignment) {
    case TextAlignment::Center:
      return SS_CENTER;
    case TextAlignment::End:
      return SS_RIGHT;
    case TextAlignment::Start:
    default:
      return SS_LEFT;
  }
}

DWORD EditAlignmentStyle(TextAlignment alignment) {
  switch (alignment) {
    case TextAlignment::Center:
      return ES_CENTER;
    case TextAlignment::End:
      return ES_RIGHT;
    case TextAlignment::Start:
    default:
      return ES_LEFT;
  }
}

HWND CreateLabelHwnd(const std::string& text) {
  // SS_EDITCONTROL wraps like a multi-line edit; SS_NOPREFIX keeps '&' literal.
  HWND hwnd = CreateControl(L"STATIC", SS_LEFT | SS_NOPREFIX | SS_EDITCONTROL, 0);
  if (hwnd) {
    SetWindowTextUtf8(hwnd, text);
  }
  return hwnd;
}

HWND CreateButtonHwnd(const std::string& text) {
  // BS_NOTIFY: BN_SETFOCUS / BN_KILLFOCUS for the focus events.
  HWND hwnd = CreateControl(L"BUTTON", WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, 0);
  if (hwnd) {
    SetWindowTextUtf8(hwnd, text);
  }
  return hwnd;
}

HWND CreateEditHwnd(TextAlignment alignment, bool multiline, bool secure, bool editable) {
  DWORD style = WS_TABSTOP | WS_BORDER | EditAlignmentStyle(alignment);
  if (multiline) {
    style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
  } else {
    style |= ES_AUTOHSCROLL;
  }
  if (secure) {
    style |= ES_PASSWORD;
  }
  if (!editable) {
    style |= ES_READONLY;
  }
  return CreateControl(L"EDIT", style, 0);
}

HWND CreateImageViewHwnd() {
  // SS_REALSIZECONTROL: STM_SETIMAGE must not resize the control to the
  // bitmap; the bitmap is rendered at the control's size anyway.
  return CreateControl(L"STATIC", SS_BITMAP | SS_CENTERIMAGE | SS_REALSIZECONTROL, 0);
}

void SetCueBanner(HWND hwnd, const std::optional<std::string>& placeholder) {
  if (!hwnd) {
    return;
  }
  std::wstring text = placeholder ? StringToWString(*placeholder) : std::wstring();
  // TRUE: shown while focused too, until the first character, as elsewhere.
  SendMessageW(hwnd, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
}

// The text properties shared by Label and TextField.

void SetControlTextColor(ViewInternal::Impl& impl, Color color) {
  impl.platform->text_color = color;
  if (impl.platform->hwnd) {
    // Picked up by the next WM_CTLCOLOR* on the parent.
    InvalidateRect(impl.platform->hwnd, nullptr, TRUE);
  }
}

void SetControlFontSize(ViewInternal::Impl& impl, double size) {
  impl.platform->font_size = size < 0 ? 0 : size;
  impl.platform->ApplyFont();
  impl.InvalidateIntrinsicSize();
}

/// EDIT styles other than ES_PASSWORD and ES_READONLY are fixed at creation:
/// build a new control from the recorded state and swap it in.
void RebuildEdit(ViewInternal::Impl& impl) {
  auto& platform = *impl.platform;
  HWND old = platform.hwnd;
  if (!old) {
    return;
  }
  HWND replacement =
      CreateEditHwnd(platform.text_alignment, platform.multiline, platform.secure, platform.editable);
  if (!replacement) {
    return;
  }
  // Under the host still: its EN_CHANGE reaches no handler.
  SetWindowTextUtf8(replacement, WindowTextUtf8(old));
  if (platform.placeholder) {
    SetCueBanner(replacement, platform.placeholder);
  }
  platform.ReplaceHwnd(replacement);
}

}  // namespace

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------

Label::Label(const std::string& text) : View(Adopt(CreateLabelHwnd(text))) {}

void Label::SetText(const std::string& text) {
  SetWindowTextUtf8(pimpl_->platform->hwnd, text);
  pimpl_->InvalidateIntrinsicSize();
}

std::string Label::GetText() const {
  return WindowTextUtf8(pimpl_->platform->hwnd);
}

void Label::SetTextColor(Color color) {
  SetControlTextColor(*pimpl_, color);
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

void Label::SetTextAlignment(TextAlignment alignment) {
  pimpl_->platform->text_alignment = alignment;
  HWND hwnd = pimpl_->platform->hwnd;
  if (!hwnd) {
    return;
  }
  // SS_LEFT / SS_CENTER / SS_RIGHT live in the type bits; the rest stays.
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  SetWindowLongPtrW(hwnd, GWL_STYLE,
                    (style & ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                        static_cast<LONG_PTR>(StaticAlignmentStyle(alignment)));
  InvalidateRect(hwnd, nullptr, TRUE);
}

TextAlignment Label::GetTextAlignment() const {
  return pimpl_->platform->text_alignment;
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

Button::Button(const std::string& text) : View(Adopt(CreateButtonHwnd(text))) {}

void Button::SetText(const std::string& text) {
  SetWindowTextUtf8(pimpl_->platform->hwnd, text);
  pimpl_->InvalidateIntrinsicSize();
}

std::string Button::GetText() const {
  return WindowTextUtf8(pimpl_->platform->hwnd);
}

// ---------------------------------------------------------------------------
// TextField
// ---------------------------------------------------------------------------

TextField::TextField(const std::string& text)
    : View(Adopt(CreateEditHwnd(TextAlignment::Start, false, false, true))) {
  if (!text.empty()) {
    SetText(text);
  }
}

void TextField::SetText(const std::string& text) {
  auto& platform = *pimpl_->platform;
  if (!platform.hwnd) {
    return;
  }
  // WM_SETTEXT raises EN_CHANGE too; it is not user input.
  platform.suppress_change = true;
  SetWindowTextUtf8(platform.hwnd, text);
  platform.suppress_change = false;
}

std::string TextField::GetText() const {
  return WindowTextUtf8(pimpl_->platform->hwnd);
}

void TextField::SetTextColor(Color color) {
  SetControlTextColor(*pimpl_, color);
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

void TextField::SetTextAlignment(TextAlignment alignment) {
  auto& platform = *pimpl_->platform;
  if (platform.text_alignment == alignment) {
    return;
  }
  platform.text_alignment = alignment;
  RebuildEdit(*pimpl_);
}

TextAlignment TextField::GetTextAlignment() const {
  return pimpl_->platform->text_alignment;
}

void TextField::SetPlaceholder(const std::optional<std::string>& placeholder) {
  pimpl_->platform->placeholder = placeholder;
  SetCueBanner(pimpl_->platform->hwnd, placeholder);
}

std::optional<std::string> TextField::GetPlaceholder() const {
  return pimpl_->platform->placeholder;
}

void TextField::SetEditable(bool is_editable) {
  auto& platform = *pimpl_->platform;
  platform.editable = is_editable;
  if (!platform.hwnd) {
    return;
  }
  SendMessageW(platform.hwnd, EM_SETREADONLY, is_editable ? FALSE : TRUE, 0);
  InvalidateRect(platform.hwnd, nullptr, TRUE);  // the background follows
}

bool TextField::IsEditable() const {
  return pimpl_->platform->editable;
}

void TextField::SetSecure(bool is_secure) {
  auto& platform = *pimpl_->platform;
  if (platform.secure == is_secure) {
    return;
  }
  platform.secure = is_secure;
  if (!platform.hwnd) {
    return;
  }
  // Toggles ES_PASSWORD in place; U+25CF is the system's password glyph.
  SendMessageW(platform.hwnd, EM_SETPASSWORDCHAR, is_secure ? 0x25CF : 0, 0);
  InvalidateRect(platform.hwnd, nullptr, TRUE);
}

bool TextField::IsSecure() const {
  return pimpl_->platform->secure;
}

void TextField::SetMultiline(bool is_multiline) {
  auto& platform = *pimpl_->platform;
  if (platform.multiline == is_multiline) {
    return;
  }
  platform.multiline = is_multiline;
  RebuildEdit(*pimpl_);
  pimpl_->InvalidateIntrinsicSize();
}

bool TextField::IsMultiline() const {
  return pimpl_->platform->multiline;
}

// ---------------------------------------------------------------------------
// ImageView
// ---------------------------------------------------------------------------

ImageView::ImageView() : View(Adopt(CreateImageViewHwnd())) {}

void ImageView::SetImage(std::shared_ptr<Image> image) {
  pimpl_->platform->image = std::move(image);
  pimpl_->platform->RenderImage();
  pimpl_->InvalidateIntrinsicSize();
}

std::shared_ptr<Image> ImageView::GetImage() const {
  return pimpl_->platform->image;
}

}  // namespace nativeapi
