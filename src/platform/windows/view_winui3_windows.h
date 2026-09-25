#pragma once

// The WinUI 3 half of a Windows view. Declared for every Windows build so the
// Win32 files can hold one; defined in view_winui3_windows.cpp, which is
// compiled only with NATIVEAPI_ENABLE_WINUI3 (view_windows.cpp carries inert
// stand-ins otherwise). No WinRT type appears here.

#include <memory>
#include <optional>
#include <string>

#include "../../image.h"
#include "../../view_impl.h"

namespace nativeapi {

enum class ViewKind;

/**
 * A view drawn by WinUI 3: its XAML element and, for a window's root view, the
 * XAML Island that hosts the tree.
 *
 * - A root view wraps the window's HWND (still its native object) and puts a
 *   DesktopWindowXamlSource over the whole client area, with a Canvas inside.
 * - A container is a Canvas; subviews are its children, placed with
 *   Canvas.Left / Canvas.Top and sized with Width / Height, in effective pixels
 *   (the logical points the shared layout works in).
 * - Label is a Border around a TextBlock, Button a Button, TextField a Grid
 *   holding a TextBox and a PasswordBox (the secure one is shown), ImageView an
 *   Image. The native object of each is that element, as an IInspectable*.
 *
 * Every method must be called on the thread the view was created on, which is
 * the thread whose XAML runtime it belongs to. None throws.
 */
class XamlView {
 public:
  /// Whether views created on this thread right now use WinUI 3: the default
  /// backend is WinUI3 and the Windows App Runtime started here. The runtime is
  /// started on the first call; a failure is written to stderr once and
  /// remembered for the thread, which then creates Native views.
  static bool IsActive();

  /// A new XAML control of `kind` showing `text` (ignored for ImageView), as an
  /// IInspectable* carrying one reference for View(NativeControl) to take over.
  /// nullptr if it could not be created.
  static void* CreateControl(ViewKind kind, const std::string& text);

  /// Takes over `native`: an element from CreateControl() (`owned`), nullptr
  /// for a new container (`owned`), or a window's HWND for its root view (not
  /// owned). Points `impl.native` at the element, except for a root.
  XamlView(ViewInternal::Impl& impl, void* native, bool owned);
  ~XamlView();

  XamlView(const XamlView&) = delete;
  XamlView& operator=(const XamlView&) = delete;

  // The View::Impl platform seam.
  void SetFrame(Rectangle frame);
  Rectangle GetFrame() const;
  Size GetIntrinsicSize() const;
  void AddSubview(XamlView* child, size_t index);
  void RemoveSubview(XamlView* child);
  void SetVisible(bool is_visible);
  void SetEnabled(bool is_enabled);
  bool IsEnabled() const;
  void SetBackgroundColor(Color color);
  void SetTooltip(const std::optional<std::string>& tooltip);
  void Focus();
  void Blur();
  bool IsFocused() const;
  void StartListening();
  void StopListening();
  void ObserveResize(bool observe);

  // The controls. Each applies to the kinds that have the property and is
  // ignored by the others.
  void SetText(const std::string& text);
  std::string GetText() const;
  void SetTextColor(Color color);
  void SetFontSize(double size);
  void SetTextAlignment(TextAlignment alignment);
  void SetPlaceholder(const std::optional<std::string>& placeholder);
  void SetEditable(bool is_editable);
  void SetSecure(bool is_secure);
  void SetMultiline(bool is_multiline);
  void SetImage(const std::shared_ptr<Image>& image);

  struct State;

 private:
  std::unique_ptr<State> state_;
};

}  // namespace nativeapi

// Forwards a View::Impl seam call to the view's XamlView when it has one.
// `impl` is a View::Impl; `call` a member call on XamlView. Used at the top of
// the Win32 implementations, which then only ever see Native views.
#ifdef NATIVEAPI_ENABLE_WINUI3
#define NATIVEAPI_VIEW_XAML(impl, call)           \
  if ((impl).platform && (impl).platform->xaml) { \
    return (impl).platform->xaml->call;           \
  }
#else
#define NATIVEAPI_VIEW_XAML(impl, call)
#endif
