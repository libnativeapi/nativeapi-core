#include "../../view.h"

// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <algorithm>
// gdiplus.h spells min / max unqualified and breaks under NOMINMAX without these.
using std::max;
using std::min;
#include <gdiplus.h>
// clang-format on
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <string>

#include "../../foundation/id_allocator.h"
#include "../../window.h"
#include "view_internal_windows.h"
#include "window_message_dispatcher.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace nativeapi {

const wchar_t* const kViewContainerClass = L"NativeApiView";

namespace {

// The background colour of a container, as 0x1RRGGBB (the leading 1 tells a
// black from "no property"). On the HWND so the class procedure, which has no
// Impl, and the labels underneath, which show through to it, can read it.
const wchar_t* const kViewBackgroundProperty = L"NativeApiViewBackground";
// The same encoding on a top-level window of the library's own class, set by
// Window::SetBackgroundColor (window_windows.cpp). Read here only.
const wchar_t* const kWindowBackgroundProperty = L"NativeAPIBackgroundColor";

constexpr UINT_PTR kEditSubclassId = 0x4E415649;  // 'NAVI'

bool IsContainerClass(HWND hwnd) {
  wchar_t name[64] = {};
  if (!hwnd || !GetClassNameW(hwnd, name, static_cast<int>(sizeof(name) / sizeof(name[0])))) {
    return false;
  }
  return _wcsicmp(name, kViewContainerClass) == 0;
}

ViewKind DetectKind(HWND hwnd) {
  wchar_t name[64] = {};
  if (!hwnd || !GetClassNameW(hwnd, name, static_cast<int>(sizeof(name) / sizeof(name[0])))) {
    return ViewKind::Container;
  }
  if (_wcsicmp(name, L"Static") == 0) {
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    return (style & SS_TYPEMASK) == SS_BITMAP ? ViewKind::ImageView : ViewKind::Label;
  }
  if (_wcsicmp(name, L"Button") == 0) {
    return ViewKind::Button;
  }
  if (_wcsicmp(name, L"Edit") == 0) {
    return ViewKind::TextField;
  }
  return ViewKind::Container;
}

COLORREF StoredColor(uintptr_t stored) {
  return RGB((stored >> 16) & 0xFF, (stored >> 8) & 0xFF, stored & 0xFF);
}

LRESULT CALLBACK ContainerWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_ERASEBKGND: {
      RECT client = {};
      GetClientRect(hwnd, &client);
      HBRUSH brush = CreateSolidBrush(EffectiveBackgroundColor(hwnd));
      if (brush) {
        FillRect(reinterpret_cast<HDC>(wparam), &client, brush);
        DeleteObject(brush);
      }
      return 1;
    }
    case WM_NCDESTROY:
      RemovePropW(hwnd, kViewBackgroundProperty);
      break;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void EnsureContainerClass() {
  static bool registered = false;
  if (registered) {
    return;
  }
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = ContainerWindowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW, whatever UNICODE says
  wc.hbrBackground = nullptr;  // WM_ERASEBKGND paints the stored colour
  wc.lpszClassName = kViewContainerClass;
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    registered = true;
  }
}

/// Measures `text` with `font` the way a STATIC draws it: every line, no
/// mnemonic prefix. An empty text measures one line high and zero wide.
SIZE MeasureText(HWND hwnd, HFONT font, const std::wstring& text) {
  SIZE size = {0, 0};
  HDC hdc = GetDC(hwnd);
  if (!hdc) {
    return size;
  }
  HGDIOBJ previous = SelectObject(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
  RECT rect = {0, 0, 0, 0};
  DrawTextW(hdc, text.empty() ? L" " : text.c_str(), -1, &rect,
            DT_CALCRECT | DT_NOPREFIX | DT_EXPANDTABS);
  size.cx = text.empty() ? 0 : rect.right;
  size.cy = rect.bottom;
  if (previous) {
    SelectObject(hdc, previous);
  }
  ReleaseDC(hwnd, hdc);
  return size;
}

/// The line height of `font`, in pixels.
int FontLineHeight(HWND hwnd, HFONT font) {
  HDC hdc = GetDC(hwnd);
  if (!hdc) {
    return 0;
  }
  HGDIOBJ previous = SelectObject(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
  TEXTMETRICW metrics = {};
  int height = GetTextMetricsW(hdc, &metrics) ? metrics.tmHeight + metrics.tmExternalLeading : 0;
  if (previous) {
    SelectObject(hdc, previous);
  }
  ReleaseDC(hwnd, hdc);
  return height;
}

std::wstring WindowTextWide(HWND hwnd) {
  const int length = hwnd ? GetWindowTextLengthW(hwnd) : 0;
  if (length <= 0) {
    return std::wstring();
  }
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(hwnd, &text[0], length + 1);
  text.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
  return text;
}

/// Enter in a single-line EDIT submits instead of beeping; a multi-line one
/// keeps its newline (ES_WANTRETURN).
LRESULT CALLBACK EditSubclassProc(HWND hwnd,
                                  UINT msg,
                                  WPARAM wparam,
                                  LPARAM lparam,
                                  UINT_PTR /*subclass_id*/,
                                  DWORD_PTR ref_data) {
  auto* impl = reinterpret_cast<ViewInternal::Impl*>(ref_data);
  switch (msg) {
    case WM_KEYDOWN:
      if (wparam == VK_RETURN && impl && impl->platform && !impl->platform->multiline) {
        impl->Emit(TextFieldSubmittedEvent(impl->id));
        // `impl` may be gone: a listener can destroy the view.
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\r' && impl && impl->platform && !impl->platform->multiline) {
        return 0;
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(hwnd, EditSubclassProc, kEditSubclassId);
      if (impl && impl->platform && impl->platform->hwnd == hwnd) {
        impl->platform->subclassed = false;
      }
      break;
    default:
      break;
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

}  // namespace

// ---------------------------------------------------------------------------
// Shared helpers (view_internal_windows.h)
// ---------------------------------------------------------------------------

COLORREF EffectiveBackgroundColor(HWND hwnd) {
  for (HWND ancestor = hwnd; ancestor; ancestor = GetAncestor(ancestor, GA_PARENT)) {
    if (IsContainerClass(ancestor)) {
      if (const auto stored = reinterpret_cast<uintptr_t>(GetPropW(ancestor, kViewBackgroundProperty))) {
        return StoredColor(stored);
      }
      continue;
    }
    // A window, of the library's class or not: the walk ends here.
    if (const auto stored = reinterpret_cast<uintptr_t>(GetPropW(ancestor, kWindowBackgroundProperty))) {
      return StoredColor(stored);
    }
    break;
  }
  return GetSysColor(COLOR_WINDOW);
}

std::string WindowTextUtf8(HWND hwnd) {
  std::wstring text = WindowTextWide(hwnd);
  std::wstring unix_text;
  unix_text.reserve(text.size());
  for (wchar_t c : text) {
    if (c != L'\r') {
      unix_text.push_back(c);
    }
  }
  return WStringToString(unix_text);
}

void SetWindowTextUtf8(HWND hwnd, const std::string& text) {
  if (!hwnd) {
    return;
  }
  std::wstring wide = StringToWString(text);
  std::wstring windows_text;
  windows_text.reserve(wide.size() + 8);
  for (size_t i = 0; i < wide.size(); ++i) {
    if (wide[i] == L'\n' && (i == 0 || wide[i - 1] != L'\r')) {
      windows_text.push_back(L'\r');
    }
    windows_text.push_back(wide[i]);
  }
  SetWindowTextW(hwnd, windows_text.c_str());
}

void EnsureCommonControls() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  INITCOMMONCONTROLSEX init = {};
  init.dwSize = sizeof(init);
  init.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;  // buttons, edits, statics, tooltips
  InitCommonControlsEx(&init);
  initialized = true;
}

// ---------------------------------------------------------------------------
// Platform state
// ---------------------------------------------------------------------------

View::Impl::Platform::Platform(Impl* impl, HWND hwnd)
    : impl(impl), hwnd(hwnd), kind(DetectKind(hwnd)) {
  if (kind != ViewKind::Container) {
    ApplyFont();
  }
}

View::Impl::Platform::~Platform() {
  UnhookParent();
  Unsubclass();
  if (resize_handler_id) {
    WindowMessageDispatcher::GetInstance().UnregisterHandler(resize_handler_id);
    resize_handler_id = 0;
  }
  DestroyTooltip();
  if (hwnd && IsWindow(hwnd)) {
    if (font) {
      SendMessageW(hwnd, WM_SETFONT, 0, FALSE);
    }
    if (bitmap) {
      SendMessageW(hwnd, STM_SETIMAGE, IMAGE_BITMAP, 0);
    }
  }
  if (font) {
    DeleteObject(font);
    font = nullptr;
  }
  if (bitmap) {
    DeleteObject(bitmap);
    bitmap = nullptr;
  }
  if (color_brush) {
    DeleteObject(color_brush);
    color_brush = nullptr;
  }
  hwnd = nullptr;
  impl = nullptr;
}

void View::Impl::Platform::HookParent() {
  if (!hwnd || kind == ViewKind::Container) {
    return;  // containers paint themselves and send no notifications
  }
  HWND parent = GetAncestor(hwnd, GA_PARENT);
  auto& dispatcher = WindowMessageDispatcher::GetInstance();
  if (!parent || parent == dispatcher.GetHostWindow()) {
    UnhookParent();
    return;
  }
  if (hooked_parent != parent) {
    UnhookParent();
  }
  hooked_parent = parent;
  Impl* self = impl;
  if (!color_handler_id) {
    color_handler_id = dispatcher.RegisterHandler(
        parent, [self](HWND, UINT msg, WPARAM wparam, LPARAM lparam) -> std::optional<LRESULT> {
          if ((msg != WM_CTLCOLORSTATIC && msg != WM_CTLCOLOREDIT) || !self->platform ||
              reinterpret_cast<HWND>(lparam) != self->platform->hwnd) {
            return std::nullopt;
          }
          return reinterpret_cast<LRESULT>(
              self->platform->OnControlColor(reinterpret_cast<HDC>(wparam)));
        });
  }
  if (listening && !command_handler_id) {
    command_handler_id = dispatcher.RegisterHandler(
        parent, [self](HWND, UINT msg, WPARAM wparam, LPARAM lparam) -> std::optional<LRESULT> {
          if (msg != WM_COMMAND || !self->platform ||
              reinterpret_cast<HWND>(lparam) != self->platform->hwnd) {
            return std::nullopt;
          }
          self->platform->OnCommand(HIWORD(wparam));
          // `self` may be gone: a listener can destroy the view.
          return std::optional<LRESULT>(0);
        });
  }
}

void View::Impl::Platform::UnhookParent() {
  auto& dispatcher = WindowMessageDispatcher::GetInstance();
  if (color_handler_id) {
    dispatcher.UnregisterHandler(color_handler_id);
    color_handler_id = 0;
  }
  if (command_handler_id) {
    dispatcher.UnregisterHandler(command_handler_id);
    command_handler_id = 0;
  }
  hooked_parent = nullptr;
}

void View::Impl::Platform::Subclass() {
  if (!hwnd || kind != ViewKind::TextField || subclassed) {
    return;
  }
  subclassed = SetWindowSubclass(hwnd, EditSubclassProc, kEditSubclassId,
                                 reinterpret_cast<DWORD_PTR>(impl)) != FALSE;
}

void View::Impl::Platform::Unsubclass() {
  if (!subclassed) {
    return;
  }
  if (hwnd && IsWindow(hwnd)) {
    RemoveWindowSubclass(hwnd, EditSubclassProc, kEditSubclassId);
  }
  subclassed = false;
}

HBRUSH View::Impl::Platform::OnControlColor(HDC hdc) {
  COLORREF background;
  if (impl->background_color.a != 0) {
    background = ToColorRef(impl->background_color);
  } else if (kind == ViewKind::TextField) {
    // The theme colour: white while editable, face colour while read-only or
    // disabled, as a plain EDIT would be.
    background = GetSysColor(editable && IsWindowEnabled(hwnd) ? COLOR_WINDOW : COLOR_3DFACE);
  } else {
    background = EffectiveBackgroundColor(GetAncestor(hwnd, GA_PARENT));
  }
  COLORREF text;
  if (text_color.a != 0) {
    text = ToColorRef(text_color);
  } else {
    text = GetSysColor(IsWindowEnabled(hwnd) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT);
  }
  SetTextColor(hdc, text);
  SetBkColor(hdc, background);
  SetBkMode(hdc, OPAQUE);
  return BrushFor(background);
}

void View::Impl::Platform::OnCommand(UINT code) {
  Impl* self = impl;
  switch (kind) {
    case ViewKind::Button:
      if (code == BN_CLICKED) {
        self->Emit(ButtonClickedEvent(self->id));
      } else if (code == BN_SETFOCUS) {
        self->Emit(ViewFocusedEvent(self->id));
      } else if (code == BN_KILLFOCUS) {
        self->Emit(ViewBlurredEvent(self->id));
      }
      break;
    case ViewKind::TextField:
      if (code == EN_CHANGE) {
        if (!suppress_change) {
          self->Emit(TextFieldChangedEvent(self->id, WindowTextUtf8(hwnd)));
        }
      } else if (code == EN_SETFOCUS) {
        self->Emit(ViewFocusedEvent(self->id));
      } else if (code == EN_KILLFOCUS) {
        self->Emit(ViewBlurredEvent(self->id));
      }
      break;
    default:
      break;
  }
}

void View::Impl::Platform::ApplyFont() {
  if (!hwnd || kind == ViewKind::Container) {
    return;
  }
  LOGFONTW logfont = {};
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
    logfont = metrics.lfMessageFont;
  } else {
    GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(logfont), &logfont);
  }
  const double scale = ScaleFor(hwnd);
  if (font_size > 0.0) {
    // Points to pixels at this DPI: 1pt is 96/72 px at 96 DPI.
    logfont.lfHeight = -ToPixels(font_size * 96.0 / 72.0, scale);
  } else {
    // The metrics are for the system DPI; rescale to the control's monitor.
    logfont.lfHeight =
        static_cast<LONG>(std::lround(logfont.lfHeight * scale / ScaleFor(nullptr)));
  }
  HFONT created = CreateFontIndirectW(&logfont);
  if (!created) {
    return;
  }
  SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(created), TRUE);
  if (font) {
    DeleteObject(font);
  }
  font = created;
}

void View::Impl::Platform::RefreshFontsRecursive() {
  ApplyFont();
  if (kind == ViewKind::ImageView) {
    RenderImage();
  }
  for (const auto& subview : impl->subviews) {
    if (subview && subview->pimpl_ && subview->pimpl_->platform) {
      subview->pimpl_->platform->RefreshFontsRecursive();
    }
  }
}

void View::Impl::Platform::ReplaceHwnd(HWND replacement) {
  if (!replacement || replacement == hwnd) {
    return;
  }
  HWND old = hwnd;
  UnhookParent();
  Unsubclass();
  DestroyTooltip();
  if (old && IsWindow(old)) {
    RECT rect = {};
    GetWindowRect(old, &rect);
    HWND parent = GetAncestor(old, GA_PARENT);
    if (parent) {
      MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
      SetParent(replacement, parent);
    }
    // Directly below the old control, which then leaves: same z-slot.
    SetWindowPos(replacement, old, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOACTIVATE);
    EnableWindow(replacement, IsWindowEnabled(old));
    ShowWindow(replacement, (GetWindowLongPtrW(old, GWL_STYLE) & WS_VISIBLE) ? SW_SHOWNA : SW_HIDE);
    const bool focused = GetFocus() == old;
    DestroyWindow(old);
    if (focused) {
      SetFocus(replacement);
    }
  }
  hwnd = replacement;
  impl->native = replacement;
  ApplyFont();
  if (impl->tooltip) {
    impl->SetNativeTooltip(impl->tooltip);
  }
  HookParent();
  if (listening) {
    Subclass();
  }
}

void View::Impl::Platform::RenderImage() {
  if (!hwnd || kind != ViewKind::ImageView) {
    return;
  }
  HBITMAP previous = bitmap;
  bitmap = nullptr;
  RECT rect = {};
  GetWindowRect(hwnd, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  auto* source = image ? static_cast<Gdiplus::Bitmap*>(image->GetNativeObject()) : nullptr;
  if (source && width > 0 && height > 0) {
    const double image_width = source->GetWidth();
    const double image_height = source->GetHeight();
    if (image_width > 0.0 && image_height > 0.0) {
      // Scaled to fit, up or down, and centred.
      const double fit = (std::min)(width / image_width, height / image_height);
      const int dest_width = (std::max)(1, static_cast<int>(std::lround(image_width * fit)));
      const int dest_height = (std::max)(1, static_cast<int>(std::lround(image_height * fit)));
      const int dest_x = (width - dest_width) / 2;
      const int dest_y = (height - dest_height) / 2;
      // A STATIC blits the bitmap without alpha: flatten onto the background.
      const COLORREF background = impl->background_color.a != 0
                                      ? ToColorRef(impl->background_color)
                                      : EffectiveBackgroundColor(GetAncestor(hwnd, GA_PARENT));
      const Gdiplus::Color fill(255, GetRValue(background), GetGValue(background),
                                GetBValue(background));
      Gdiplus::Bitmap canvas(width, height, PixelFormat32bppARGB);
      Gdiplus::Graphics graphics(&canvas);
      graphics.Clear(fill);
      graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
      graphics.DrawImage(source, Gdiplus::Rect(dest_x, dest_y, dest_width, dest_height));
      HBITMAP rendered = nullptr;
      if (canvas.GetHBITMAP(fill, &rendered) == Gdiplus::Ok) {
        bitmap = rendered;
      }
    }
  }
  SendMessageW(hwnd, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmap));
  if (previous) {
    DeleteObject(previous);
  }
  InvalidateRect(hwnd, nullptr, TRUE);
}

HBRUSH View::Impl::Platform::BrushFor(COLORREF color) {
  if (!color_brush || color_brush_color != color) {
    if (color_brush) {
      DeleteObject(color_brush);
    }
    color_brush = CreateSolidBrush(color);
    color_brush_color = color;
  }
  return color_brush ? color_brush : GetSysColorBrush(COLOR_WINDOW);
}

void View::Impl::Platform::DestroyTooltip() {
  if (tooltip_hwnd) {
    if (IsWindow(tooltip_hwnd)) {
      DestroyWindow(tooltip_hwnd);
    }
    tooltip_hwnd = nullptr;
  }
}

// ---------------------------------------------------------------------------
// Impl: construction and the platform seam
// ---------------------------------------------------------------------------

View::Impl::Impl(View* owner, void* native, bool owned)
    : owner(owner), native(native), owned(owned), id(IdAllocator::Allocate<View>()) {
  if (!this->native && owned) {
    this->native = CreateNativeContainer();
  }
  platform = std::make_unique<Platform>(this, static_cast<HWND>(this->native));
}

View::Impl::~Impl() {
  // DestroyWindow takes every child with it; the subviews may outlive this
  // view through references elsewhere, so park their HWNDs first.
  if (platform && platform->hwnd) {
    for (const auto& subview : subviews) {
      if (subview && subview->pimpl_) {
        RemoveNativeSubview(*subview->pimpl_);
      }
    }
  }
  const HWND hwnd = platform ? platform->hwnd : nullptr;
  platform.reset();
  if (owned && hwnd && IsWindow(hwnd)) {
    DestroyWindow(hwnd);
  }
  native = nullptr;
}

void* View::Impl::CreateNativeContainer() {
  EnsureContainerClass();
  HWND host = WindowMessageDispatcher::GetInstance().GetHostWindow();
  if (!host) {
    return nullptr;  // a WS_CHILD needs a parent
  }
  return CreateWindowExW(0, kViewContainerClass, L"",
                         WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                         host, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void View::Impl::SetNativeFrame(Rectangle frame) {
  HWND hwnd = platform->hwnd;
  if (!hwnd || is_root) {
    return;
  }
  const double scale = ScaleFor(hwnd);
  RECT before = {};
  GetWindowRect(hwnd, &before);
  const int width = (std::max)(0, ToPixels(frame.width, scale));
  const int height = (std::max)(0, ToPixels(frame.height, scale));
  SetWindowPos(hwnd, nullptr, ToPixels(frame.x, scale), ToPixels(frame.y, scale), width, height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  if (platform->kind == ViewKind::ImageView &&
      (before.right - before.left != width || before.bottom - before.top != height)) {
    platform->RenderImage();
  }
}

Rectangle View::Impl::GetNativeFrame() const {
  HWND hwnd = platform->hwnd;
  if (!hwnd) {
    return Rectangle{0, 0, 0, 0};
  }
  const double scale = ScaleFor(hwnd);
  if (is_root) {
    RECT client = {};
    GetClientRect(hwnd, &client);
    return Rectangle{0, 0, (client.right - client.left) / scale,
                     (client.bottom - client.top) / scale};
  }
  RECT rect = {};
  GetWindowRect(hwnd, &rect);
  if (HWND parent = GetAncestor(hwnd, GA_PARENT)) {
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
  }
  return Rectangle{rect.left / scale, rect.top / scale, (rect.right - rect.left) / scale,
                   (rect.bottom - rect.top) / scale};
}

Size View::Impl::GetNativeIntrinsicSize() const {
  HWND hwnd = platform->hwnd;
  if (!hwnd) {
    return Size{0, 0};
  }
  const double scale = ScaleFor(hwnd);
  switch (platform->kind) {
    case ViewKind::Label: {
      const SIZE text = MeasureText(hwnd, platform->font, WindowTextWide(hwnd));
      // A little room so glyph overhang is not clipped at the edges.
      return Size{(text.cx + 4) / scale, (text.cy + 2) / scale};
    }
    case ViewKind::Button: {
      SIZE ideal = {0, 0};
      if (SendMessageW(hwnd, BCM_GETIDEALSIZE, 0, reinterpret_cast<LPARAM>(&ideal)) &&
          ideal.cx > 0 && ideal.cy > 0) {
        return Size{ideal.cx / scale, ideal.cy / scale};
      }
      // Without comctl32 v6: the text plus the classic push-button margins.
      const SIZE text = MeasureText(hwnd, platform->font, WindowTextWide(hwnd));
      const int width = text.cx + ToPixels(20.0, scale);
      const int height =
          (std::max)(static_cast<int>(text.cy) + ToPixels(12.0, scale), ToPixels(23.0, scale));
      return Size{width / scale, height / scale};
    }
    case ViewKind::TextField: {
      // No intrinsic width, like an editable field elsewhere; the height fits
      // the font (one line, or the lines it holds when multi-line) plus the
      // border and the control's inner margin.
      int lines = 1;
      if (platform->multiline) {
        const std::wstring text = WindowTextWide(hwnd);
        lines = 1 + static_cast<int>(std::count(text.begin(), text.end(), L'\n'));
      }
      const int height = lines * FontLineHeight(hwnd, platform->font) + ToPixels(8.0, scale);
      return Size{0, height / scale};
    }
    case ViewKind::ImageView:
      return platform->image ? platform->image->GetSize() : Size{0, 0};
    case ViewKind::Container:
    default:
      return Size{0, 0};
  }
}

void View::Impl::AddNativeSubview(Impl& child, size_t index) {
  HWND hwnd = platform->hwnd;
  HWND child_hwnd = child.platform ? child.platform->hwnd : nullptr;
  if (!hwnd || !child_hwnd) {
    return;
  }
  SetParent(child_hwnd, hwnd);
  // Index 0 is the bottom. `subviews` already holds the child at `index`, so
  // the entry after it is the one it goes directly underneath.
  HWND insert_after = HWND_TOP;
  if (index == 0) {
    insert_after = HWND_BOTTOM;
  } else if (index + 1 < subviews.size()) {
    const auto& above = subviews[index + 1];
    if (above && above->pimpl_ && above->pimpl_->platform && above->pimpl_->platform->hwnd) {
      insert_after = above->pimpl_->platform->hwnd;
    }
  }
  SetWindowPos(child_hwnd, insert_after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  ShowWindow(child_hwnd, child.visible ? SW_SHOWNA : SW_HIDE);
  // The new window may sit on another monitor: fonts and the rendered image
  // follow its DPI, and the tooltip must be owned by a real top-level window.
  child.platform->RefreshFontsRecursive();
  child.platform->HookParent();
  if (child.tooltip) {
    child.platform->DestroyTooltip();
    child.SetNativeTooltip(child.tooltip);
  }
}

void View::Impl::RemoveNativeSubview(Impl& child) {
  HWND child_hwnd = child.platform ? child.platform->hwnd : nullptr;
  if (!child_hwnd || !platform->hwnd || GetAncestor(child_hwnd, GA_PARENT) != platform->hwnd) {
    return;
  }
  child.platform->UnhookParent();
  child.platform->DestroyTooltip();
  ShowWindow(child_hwnd, SW_HIDE);
  // Back under the hidden host, where it was created, until it is added again.
  SetParent(child_hwnd, WindowMessageDispatcher::GetInstance().GetHostWindow());
}

void View::Impl::SetNativeVisible(bool is_visible) {
  HWND hwnd = platform->hwnd;
  if (!hwnd || is_root) {
    return;
  }
  ShowWindow(hwnd, is_visible ? SW_SHOWNA : SW_HIDE);
}

void View::Impl::SetNativeEnabled(bool is_enabled) {
  HWND hwnd = platform->hwnd;
  if (!hwnd || is_root) {
    return;
  }
  EnableWindow(hwnd, is_enabled ? TRUE : FALSE);
}

bool View::Impl::IsNativeEnabled() const {
  HWND hwnd = platform->hwnd;
  if (!hwnd) {
    return true;
  }
  return IsWindowEnabled(hwnd) != FALSE;
}

void View::Impl::SetNativeBackgroundColor(Color color) {
  HWND hwnd = platform->hwnd;
  if (!hwnd) {
    return;
  }
  if (platform->kind == ViewKind::Container) {
    // Only a container of the library's own class paints from the property;
    // a wrapped window keeps Window::SetBackgroundColor's colour.
    if (!IsContainerClass(hwnd)) {
      return;
    }
    if (color.a == 0) {
      RemovePropW(hwnd, kViewBackgroundProperty);
    } else {
      const uintptr_t stored = 0x1000000u | (static_cast<uintptr_t>(color.r) << 16) |
                               (static_cast<uintptr_t>(color.g) << 8) | color.b;
      SetPropW(hwnd, kViewBackgroundProperty, reinterpret_cast<HANDLE>(stored));
    }
  } else if (platform->kind == ViewKind::ImageView) {
    platform->RenderImage();
  }
  // The controls read `background_color` on their next WM_CTLCOLOR*; the
  // children of a container show through to it.
  RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void View::Impl::SetNativeTooltip(const std::optional<std::string>& tooltip) {
  HWND hwnd = platform->hwnd;
  if (!hwnd) {
    return;
  }
  if (!tooltip) {
    platform->DestroyTooltip();
    return;
  }
  EnsureCommonControls();
  std::wstring text = StringToWString(*tooltip);
  TOOLINFOW info = {};
  info.cbSize = TTTOOLINFOW_V2_SIZE;
  info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  info.hwnd = hwnd;
  info.uId = reinterpret_cast<UINT_PTR>(hwnd);
  info.lpszText = &text[0];
  if (!platform->tooltip_hwnd) {
    platform->tooltip_hwnd = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!platform->tooltip_hwnd) {
      return;
    }
    SendMessageW(platform->tooltip_hwnd, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
  } else {
    SendMessageW(platform->tooltip_hwnd, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
  }
}

void View::Impl::NativeFocus() {
  HWND hwnd = platform->hwnd;
  if (!hwnd || is_root) {
    return;
  }
  if (platform->kind == ViewKind::TextField || platform->kind == ViewKind::Button) {
    SetFocus(hwnd);
  }
}

void View::Impl::NativeBlur() {
  HWND hwnd = platform->hwnd;
  if (!hwnd || !IsNativeFocused()) {
    return;
  }
  SetFocus(GetAncestor(hwnd, GA_ROOT));
}

bool View::Impl::IsNativeFocused() const {
  HWND hwnd = platform->hwnd;
  return hwnd && GetFocus() == hwnd;
}

void View::Impl::StartNativeListening() {
  platform->listening = true;
  platform->HookParent();
  platform->Subclass();
}

void View::Impl::StopNativeListening() {
  platform->listening = false;
  if (platform->command_handler_id) {
    WindowMessageDispatcher::GetInstance().UnregisterHandler(platform->command_handler_id);
    platform->command_handler_id = 0;
  }
  platform->Unsubclass();
}

void View::Impl::ObserveNativeResize(bool observe) {
  HWND hwnd = platform->hwnd;
  if (!hwnd) {
    return;
  }
  auto& dispatcher = WindowMessageDispatcher::GetInstance();
  if (observe && !platform->resize_handler_id) {
    Impl* self = this;
    platform->resize_handler_id = dispatcher.RegisterHandler(
        hwnd, [self](HWND, UINT msg, WPARAM, LPARAM) -> std::optional<LRESULT> {
          if (msg == WM_SIZE) {
            self->OnNativeResized();
          } else if (msg == WM_DPICHANGED) {
            // Fonts and images are in pixels of the old monitor; a layout
            // pass follows since every frame is in points.
            if (self->platform) {
              self->platform->RefreshFontsRecursive();
            }
            self->OnNativeResized();
          }
          return std::nullopt;
        });
  } else if (!observe && platform->resize_handler_id) {
    dispatcher.UnregisterHandler(platform->resize_handler_id);
    platform->resize_handler_id = 0;
  }
}

bool View::IsSupported() {
  return true;
}

// ---------------------------------------------------------------------------
// Window::GetContentView
// ---------------------------------------------------------------------------

std::shared_ptr<View> Window::GetContentView() const {
  void* native = GetNativeObject();
  if (!native || !IsWindow(static_cast<HWND>(native))) {
    return nullptr;
  }
  return ContentViewFor(native);
}

}  // namespace nativeapi
