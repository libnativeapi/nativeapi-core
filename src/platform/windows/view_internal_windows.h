#pragma once

// Shared between view_windows.cpp and view_controls_windows.cpp: the platform
// half of View::Impl and the Win32 glue that turns control notifications into
// events.

// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on
#include <cmath>
#include <memory>
#include <optional>
#include <string>

#include "../../image.h"
#include "../../view_impl.h"
#include "dpi_utils_windows.h"
#include "string_utils_windows.h"

namespace nativeapi {

/// Window class of a plain container View. Registered once, on first use.
extern const wchar_t* const kViewContainerClass;

/// What the wrapped HWND is. Decided once from its window class and style;
/// a control that rebuilds its HWND (TextField) keeps its kind.
enum class ViewKind {
  /// A NativeApiView container, or an HWND of some other class the library
  /// wraps without owning (a window's client area).
  Container,
  Label,
  Button,
  TextField,
  ImageView,
};

struct View::Impl::Platform {
  Platform(Impl* impl, HWND hwnd);
  ~Platform();

  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  Impl* impl;
  HWND hwnd;
  ViewKind kind = ViewKind::Container;
  bool listening = false;

  /// Handler on the parent HWND that answers WM_CTLCOLOR* for this control.
  /// Registered while the control sits under a parent of the library's.
  int color_handler_id = 0;
  /// Handler on the parent HWND that turns WM_COMMAND notifications into
  /// events. Registered only while listening.
  int command_handler_id = 0;
  /// The parent the two handlers above are registered on.
  HWND hooked_parent = nullptr;
  /// Root views only: the WM_SIZE / WM_DPICHANGED handler on the window.
  int resize_handler_id = 0;
  /// Whether the Enter-key subclass is installed on an EDIT control.
  bool subclassed = false;

  /// The control's font: the system message font at the control's DPI, or one
  /// of `font_size` points. Always owned here.
  HFONT font = nullptr;
  /// The brush handed back from WM_CTLCOLOR*, rebuilt when its colour changes.
  HBRUSH color_brush = nullptr;
  COLORREF color_brush_color = 0;
  /// Tooltip control for this view, created on the first SetTooltip.
  HWND tooltip_hwnd = nullptr;

  // Text controls remember what "default" was so a zero-alpha colour or a
  // zero size can restore it.
  Color text_color{0, 0, 0, 0};
  double font_size = 0.0;
  TextAlignment text_alignment = TextAlignment::Start;
  std::optional<std::string> placeholder;
  bool editable = true;
  bool secure = false;
  bool multiline = false;
  /// Set around a programmatic WM_SETTEXT so EN_CHANGE does not become a
  /// TextFieldChangedEvent.
  bool suppress_change = false;

  // ImageView: the image and the bitmap rendered from it at the frame size.
  std::shared_ptr<Image> image;
  HBITMAP bitmap = nullptr;

  /// Registers the colour handler, and the command handler while listening,
  /// on the current parent. Idempotent; a no-op while detached.
  void HookParent();
  /// Unregisters both parent handlers.
  void UnhookParent();
  /// Installs / removes the Enter-key subclass on an EDIT control.
  void Subclass();
  void Unsubclass();
  /// (Re)creates `font` for the control's current DPI and sends WM_SETFONT.
  void ApplyFont();
  /// Swaps the native control for `replacement`, keeping frame, parent,
  /// z-order, visibility, tooltip, font and the listening state. Used when a
  /// text field changes a style that cannot be toggled after creation.
  void ReplaceHwnd(HWND replacement);
  /// ImageView: renders `image` into `bitmap` at the control's size and shows
  /// it. Called when the image, the frame or the background changes.
  void RenderImage();
  /// The brush a WM_CTLCOLOR* reply hands back for `color`.
  HBRUSH BrushFor(COLORREF color);
  /// Answers WM_CTLCOLORSTATIC / WM_CTLCOLOREDIT for this control: sets the
  /// text colour on `hdc` and returns the background brush.
  HBRUSH OnControlColor(HDC hdc);
  /// Turns a WM_COMMAND notification `code` from this control into an event.
  /// The view may be destroyed by a listener: nothing runs after the Emit.
  void OnCommand(UINT code);
  /// Re-applies the font (and the rendered image) at the current DPI, here
  /// and in every subview. Called after a reparent and on WM_DPICHANGED.
  void RefreshFontsRecursive();
  void DestroyTooltip();
};

/// The DPI scale of `hwnd`, never zero.
inline double ScaleFor(HWND hwnd) {
  const double scale = GetScaleFactorForWindow(hwnd);
  return scale > 0.0 ? scale : 1.0;
}

inline int ToPixels(double points, double scale) {
  return static_cast<int>(std::lround(points * scale));
}

inline COLORREF ToColorRef(Color color) {
  return RGB(color.r, color.g, color.b);
}

/// The colour a control with no background of its own shows through to: the
/// nearest ancestor container's colour, else the window's, else COLOR_WINDOW.
COLORREF EffectiveBackgroundColor(HWND hwnd);

/// Reads the control's text as UTF-8 with LF line endings.
std::string WindowTextUtf8(HWND hwnd);
/// Sets the control's text from UTF-8, turning LF into CRLF for EDIT controls.
void SetWindowTextUtf8(HWND hwnd, const std::string& text);
/// Ensures comctl32 has registered the classes the views use.
void EnsureCommonControls();

}  // namespace nativeapi
