// Label, Button, TextField and ImageView on Win32 common controls.

#include "../../view.h"

#include <algorithm>
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

/// The control for the backend this thread creates views with now: a XAML
/// element, or the HWND `make_hwnd` builds. The Impl constructor asks
/// XamlView::IsActive() again and gets the same answer.
template <typename MakeHwnd>
ViewInternal::NativeControl AdoptFor(ViewKind kind, const std::string& text, MakeHwnd make_hwnd) {
  if (XamlView::IsActive()) {
    return ViewInternal::NativeControl{XamlView::CreateControl(kind, text)};
  }
  return Adopt(make_hwnd());
}

HWND CreateControl(const wchar_t* class_name, DWORD style, DWORD ex_style) {
  EnsureCommonControls();
  HWND host = WindowMessageDispatcher::GetInstance().GetHostWindow();
  if (!host) {
    return nullptr;  // a WS_CHILD needs a parent
  }
  return CreateWindowExW(ex_style, class_name, L"",
                         WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 0, 0,
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

HWND CreateEditHwnd(TextAlignment alignment,
                    bool multiline,
                    bool secure,
                    bool editable,
                    bool bordered) {
  DWORD style = WS_TABSTOP | EditAlignmentStyle(alignment);
  if (bordered) {
    style |= WS_BORDER;
  }
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
  HWND hwnd = CreateControl(L"EDIT", style, 0);
  if (hwnd && !bordered) {
    SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
    static auto set_theme = uxtheme ? reinterpret_cast<SetWindowThemeFn>(
                                          GetProcAddress(uxtheme, "SetWindowTheme"))
                                    : nullptr;
    if (set_theme) {
      set_theme(hwnd, L"", L"");
    }
  }
  return hwnd;
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
  if (impl.platform->text_color.ToRGBA() == color.ToRGBA()) {
    return;
  }
  impl.platform->text_color = color;
  NATIVEAPI_VIEW_XAML(impl, SetTextColor(color))
  if (impl.platform->hwnd) {
    // Picked up by the next WM_CTLCOLOR* on the parent.
    InvalidateRect(impl.platform->hwnd, nullptr, TRUE);
  }
}

void SetControlFontSize(ViewInternal::Impl& impl, double size) {
  const double clamped = size < 0 ? 0 : size;
  if (impl.platform->font_size == clamped) {
    return;
  }
  impl.platform->font_size = clamped;
  NATIVEAPI_VIEW_XAML(impl, SetFontSize(impl.platform->font_size))
  impl.platform->ApplyFont();
  impl.InvalidateIntrinsicSize();
}

void SetControlFontFamily(ViewInternal::Impl& impl, const std::optional<std::string>& family) {
  if (impl.platform->font_family == family) {
    return;
  }
  impl.platform->font_family = family;
  NATIVEAPI_VIEW_XAML(impl, SetFontFamily(impl.platform->font_family))
  impl.platform->ApplyFont();
  impl.InvalidateIntrinsicSize();
}

void SetControlFontWeight(ViewInternal::Impl& impl, int weight) {
  const int clamped = weight <= 0 ? 0 : (std::clamp)(weight, 100, 900);
  if (impl.platform->font_weight == clamped) {
    return;
  }
  impl.platform->font_weight = clamped;
  NATIVEAPI_VIEW_XAML(impl, SetFontWeight(impl.platform->font_weight))
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
  HWND replacement = CreateEditHwnd(platform.text_alignment, platform.multiline, platform.secure,
                                    platform.editable, platform.bordered);
  if (!replacement) {
    return;
  }
  // Under the host still: its EN_CHANGE reaches no handler.
  SetWindowTextUtf8(replacement, WindowTextUtf8(old));
  if (platform.placeholder && !platform.multiline) {
    SetCueBanner(replacement, platform.placeholder);
  }
  platform.ReplaceHwnd(replacement);
}

}  // namespace

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------

Label::Label(const std::string& text)
    : View(AdoptFor(ViewKind::Label, text, [&] { return CreateLabelHwnd(text); })) {}

void Label::SetText(const std::string& text) {
  NATIVEAPI_VIEW_XAML(*pimpl_, SetText(text))
  SetWindowTextUtf8(pimpl_->platform->hwnd, text);
  pimpl_->InvalidateIntrinsicSize();
}

std::string Label::GetText() const {
  NATIVEAPI_VIEW_XAML(*pimpl_, GetText())
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
  pimpl_->platform->text_alignment = alignment;
  NATIVEAPI_VIEW_XAML(*pimpl_, SetTextAlignment(alignment))
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

Button::Button(const std::string& text)
    : View(AdoptFor(ViewKind::Button, text, [&] { return CreateButtonHwnd(text); })) {}

void Button::SetText(const std::string& text) {
  NATIVEAPI_VIEW_XAML(*pimpl_, SetText(text))
  SetWindowTextUtf8(pimpl_->platform->hwnd, text);
  pimpl_->InvalidateIntrinsicSize();
}

std::string Button::GetText() const {
  NATIVEAPI_VIEW_XAML(*pimpl_, GetText())
  return WindowTextUtf8(pimpl_->platform->hwnd);
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

TextField::TextField(const std::string& text)
    : View(AdoptFor(ViewKind::TextField, text, [] {
        return CreateEditHwnd(TextAlignment::Start, false, false, true, true);
      })) {
  if (!text.empty() && GetBackend() == ViewBackend::Native) {
    SetText(text);
  }
}

void TextField::SetText(const std::string& text) {
  NATIVEAPI_VIEW_XAML(*pimpl_, SetText(text))
  auto& platform = *pimpl_->platform;
  if (!platform.hwnd || WindowTextUtf8(platform.hwnd) == text) {
    return;
  }
  DWORD sel_start = 0;
  DWORD sel_end = 0;
  SendMessageW(platform.hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start),
               reinterpret_cast<LPARAM>(&sel_end));
  // WM_SETTEXT raises EN_CHANGE too; it is not user input.
  platform.suppress_change = true;
  SetWindowTextUtf8(platform.hwnd, text);
  platform.suppress_change = false;
  const DWORD length = static_cast<DWORD>(GetWindowTextLengthW(platform.hwnd));
  SendMessageW(platform.hwnd, EM_SETSEL, (std::min)(sel_start, length),
               (std::min)(sel_end, length));
}

std::string TextField::GetText() const {
  NATIVEAPI_VIEW_XAML(*pimpl_, GetText())
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
  auto& platform = *pimpl_->platform;
  if (platform.text_alignment == alignment) {
    return;
  }
  platform.text_alignment = alignment;
  NATIVEAPI_VIEW_XAML(*pimpl_, SetTextAlignment(alignment))
  RebuildEdit(*pimpl_);
}

TextAlignment TextField::GetTextAlignment() const {
  return pimpl_->platform->text_alignment;
}

void TextField::SetPlaceholder(const std::optional<std::string>& placeholder) {
  if (pimpl_->platform->placeholder == placeholder) {
    return;
  }
  pimpl_->platform->placeholder = placeholder;
  NATIVEAPI_VIEW_XAML(*pimpl_, SetPlaceholder(placeholder))
  if (pimpl_->platform->multiline) {
    if (pimpl_->platform->hwnd) {
      InvalidateRect(pimpl_->platform->hwnd, nullptr, TRUE);
    }
  } else {
    SetCueBanner(pimpl_->platform->hwnd, placeholder);
  }
}

std::optional<std::string> TextField::GetPlaceholder() const {
  return pimpl_->platform->placeholder;
}

void TextField::SetEditable(bool is_editable) {
  auto& platform = *pimpl_->platform;
  platform.editable = is_editable;
  NATIVEAPI_VIEW_XAML(*pimpl_, SetEditable(is_editable))
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
  NATIVEAPI_VIEW_XAML(*pimpl_, SetSecure(is_secure))
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
  NATIVEAPI_VIEW_XAML(*pimpl_, SetMultiline(is_multiline))
  RebuildEdit(*pimpl_);
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
  NATIVEAPI_VIEW_XAML(*pimpl_, SetBordered(is_bordered))
  // Win32 EDIT latches WS_BORDER into its internal border flag during
  // WM_NCCREATE and clears WS_BORDER from GWL_STYLE, painting a 1px
  // COLOR_WINDOWFRAME outline in WM_PAINT until the control is recreated.
  RebuildEdit(*pimpl_);
  pimpl_->InvalidateIntrinsicSize();
}

bool TextField::IsBordered() const {
  return pimpl_->platform->bordered;
}

void TextField::SetSelection(TextRange range) {
  NATIVEAPI_VIEW_XAML(*pimpl_, SetSelection(range))
  HWND hwnd = pimpl_->platform->hwnd;
  if (!hwnd) {
    return;
  }
  const DWORD total = static_cast<DWORD>(GetWindowTextLengthW(hwnd));
  const DWORD start = range.start > total ? total : static_cast<DWORD>(range.start);
  const DWORD max_len = total - start;
  const DWORD len = range.length > max_len ? max_len : static_cast<DWORD>(range.length);
  SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(start), static_cast<LPARAM>(start + len));
}

TextRange TextField::GetSelection() const {
  NATIVEAPI_VIEW_XAML(*pimpl_, GetSelection())
  HWND hwnd = pimpl_->platform->hwnd;
  if (!hwnd) {
    return TextRange{0, 0};
  }
  DWORD start = 0;
  DWORD end = 0;
  SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
  if (end < start) {
    return TextRange{static_cast<size_t>(end), static_cast<size_t>(start - end)};
  }
  return TextRange{static_cast<size_t>(start), static_cast<size_t>(end - start)};
}

bool TextField::IsComposing() const {
  NATIVEAPI_VIEW_XAML(*pimpl_, IsComposing())
  HWND hwnd = pimpl_->platform->hwnd;
  if (!hwnd) {
    return false;
  }
  using ImmGetContextFn = HANDLE(WINAPI*)(HWND);
  using ImmGetCompositionStringWFn = LONG(WINAPI*)(HANDLE, DWORD, LPVOID, DWORD);
  using ImmReleaseContextFn = BOOL(WINAPI*)(HWND, HANDLE);
  static HMODULE imm32 = LoadLibraryW(L"imm32.dll");
  static auto get_ctx =
      imm32 ? reinterpret_cast<ImmGetContextFn>(GetProcAddress(imm32, "ImmGetContext")) : nullptr;
  static auto get_comp = imm32 ? reinterpret_cast<ImmGetCompositionStringWFn>(
                                     GetProcAddress(imm32, "ImmGetCompositionStringW"))
                               : nullptr;
  static auto rel_ctx = imm32 ? reinterpret_cast<ImmReleaseContextFn>(
                                    GetProcAddress(imm32, "ImmReleaseContext"))
                              : nullptr;
  if (!get_ctx || !get_comp || !rel_ctx) {
    return false;
  }
  HANDLE himc = get_ctx(hwnd);
  if (!himc) {
    return false;
  }
  constexpr DWORD kGcsCompStr = 0x0008;
  const LONG bytes = get_comp(himc, kGcsCompStr, nullptr, 0);
  rel_ctx(hwnd, himc);
  return bytes > 0;
}

// ---------------------------------------------------------------------------
// ImageView
// ---------------------------------------------------------------------------

ImageView::ImageView()
    : View(AdoptFor(ViewKind::ImageView, "", [] { return CreateImageViewHwnd(); })) {}

void ImageView::SetImage(std::shared_ptr<Image> image) {
  pimpl_->platform->image = std::move(image);
  NATIVEAPI_VIEW_XAML(*pimpl_, SetImage(pimpl_->platform->image))
  pimpl_->platform->RenderImage();
  pimpl_->InvalidateIntrinsicSize();
}

std::shared_ptr<Image> ImageView::GetImage() const {
  return pimpl_->platform->image;
}

}  // namespace nativeapi
