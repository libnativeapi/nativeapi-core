// Views on WinUI 3: XAML controls in a XAML Island over the window. Compiled
// only with NATIVEAPI_ENABLE_WINUI3; see view_winui3_windows.h for the shape.

// clang-format off
#include <windows.h>
#include <algorithm>
// gdiplus.h spells min / max unqualified and breaks under NOMINMAX without these.
using std::max;
using std::min;
#include <gdiplus.h>
// clang-format on
#undef GetCurrentTime

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>

#include "view_internal_windows.h"
#include "view_winui3_windows.h"
#include "window_message_dispatcher.h"
#include "winui3_runtime_windows.h"

namespace nativeapi {

namespace {

namespace X = winrt::Microsoft::UI::Xaml;
namespace C = X::Controls;
namespace H = X::Hosting;
namespace M = X::Media;
using winrt::Windows::Foundation::IInspectable;

// What an element is, recorded on its Tag so a view can tell from the element
// alone (View(NativeControl) hands over only a pointer).
const wchar_t* const kLabelTag = L"nativeapi.Label";
const wchar_t* const kButtonTag = L"nativeapi.Button";
const wchar_t* const kTextFieldTag = L"nativeapi.TextField";
const wchar_t* const kImageViewTag = L"nativeapi.ImageView";

void Log(const char* where, const winrt::hresult_error& error) {
  std::cerr << "WinUI3 view (" << where << "): " << winrt::to_string(error.message()) << '\n';
}

// Runs `body`, turning a WinRT failure into a diagnostic: the public API does
// not throw, and a view keeps working on whatever did succeed.
template <typename Body>
void Guard(const char* where, Body&& body) {
  try {
    body();
  } catch (const winrt::hresult_error& error) {
    Log(where, error);
  } catch (...) {
    std::cerr << "WinUI3 view (" << where << "): unexpected exception\n";
  }
}

winrt::hstring ToHString(const std::string& text) {
  return winrt::to_hstring(text);
}

M::SolidColorBrush Brush(Color color) {
  return M::SolidColorBrush(winrt::Windows::UI::Color{color.a, color.r, color.g, color.b});
}

X::TextAlignment ToXamlAlignment(TextAlignment alignment) {
  switch (alignment) {
    case TextAlignment::Center:
      return X::TextAlignment::Center;
    case TextAlignment::End:
      return X::TextAlignment::Right;
    case TextAlignment::Start:
    default:
      return X::TextAlignment::Left;
  }
}

ViewKind KindOf(const X::FrameworkElement& element) {
  const auto tag = winrt::unbox_value_or<winrt::hstring>(element.Tag(), winrt::hstring());
  if (tag == kLabelTag) return ViewKind::Label;
  if (tag == kButtonTag) return ViewKind::Button;
  if (tag == kTextFieldTag) return ViewKind::TextField;
  if (tag == kImageViewTag) return ViewKind::ImageView;
  return ViewKind::Container;
}

// Canonical identity, for comparing two references to one object.
bool Same(const IInspectable& a, const IInspectable& b) {
  if (!a || !b) return false;
  return a.as<winrt::Windows::Foundation::IUnknown>() ==
         b.as<winrt::Windows::Foundation::IUnknown>();
}

// The process-wide default is read through View::GetDefaultBackend(); whether
// the runtime came up is per thread, like the runtime itself.
thread_local int g_runtime_state = 0;  // 0 unknown, 1 running, -1 failed

}  // namespace

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct XamlView::State {
  ViewInternal::Impl* impl = nullptr;
  ViewKind kind = ViewKind::Container;
  bool owned = true;

  X::FrameworkElement element{nullptr};
  C::Canvas canvas{nullptr};  // container and root

  C::Border label_border{nullptr};
  C::TextBlock label_text{nullptr};
  C::Button button{nullptr};
  C::Grid field_host{nullptr};
  C::TextBox text_box{nullptr};
  C::PasswordBox password_box{nullptr};
  C::Image image{nullptr};

  // Root only.
  HWND window = nullptr;
  H::DesktopWindowXamlSource source{nullptr};
  int resize_handler_id = 0;
  X::FrameworkElement::Loaded_revoker loaded;

  Rectangle frame{0, 0, 0, 0};
  bool has_frame = false;
  bool enabled = true;
  bool secure = false;
  bool multiline = false;
  bool listening = false;
  /// Set around a programmatic text change, so it is not reported as input.
  int suppress_change = 0;

  C::Button::Click_revoker click;
  C::TextBox::TextChanged_revoker text_changed;
  C::PasswordBox::PasswordChanged_revoker password_changed;
  X::UIElement::PreviewKeyDown_revoker text_key;
  X::UIElement::PreviewKeyDown_revoker password_key;
  X::UIElement::GotFocus_revoker got_focus;
  X::UIElement::LostFocus_revoker lost_focus;
  /// A Focus() that came before the target was loaded, which XAML ignores:
  /// applied when it loads.
  X::FrameworkElement::Loaded_revoker focus_when_loaded;

  /// The element that takes keyboard focus: the button, or the text field's
  /// visible box. Null for the kinds that take none.
  C::Control FocusTarget() const {
    if (button) return button;
    if (kind == ViewKind::TextField) {
      return secure ? C::Control(password_box) : C::Control(text_box);
    }
    return nullptr;
  }

  /// Every Control the view is made of (for IsEnabled / Foreground / FontSize).
  std::vector<C::Control> Controls() const {
    std::vector<C::Control> controls;
    if (button) controls.push_back(button);
    if (text_box) controls.push_back(text_box);
    if (password_box) controls.push_back(password_box);
    return controls;
  }

  void ShowActiveBox() {
    if (!text_box || !password_box) return;
    text_box.Visibility(secure ? X::Visibility::Collapsed : X::Visibility::Visible);
    password_box.Visibility(secure ? X::Visibility::Visible : X::Visibility::Collapsed);
  }

  /// Root only: the island over the whole client area, on top of any other
  /// child window (a host framework's view).
  void FitIsland() {
    if (!source || !window) return;
    RECT rect{};
    GetClientRect(window, &rect);
    source.SiteBridge().MoveAndResize({0, 0, rect.right - rect.left, rect.bottom - rect.top});
    source.SiteBridge().MoveInZOrderAtTop();
  }

  template <typename E>
  void Emit(const E& event) {
    // The listener may destroy the view: nothing may touch `this` afterwards.
    impl->Emit(event);
  }
};

// ---------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------

bool XamlView::IsActive() {
  if (View::GetDefaultBackend() != ViewBackend::WinUI3) {
    return false;
  }
  if (g_runtime_state == 0) {
    try {
      InitializeWinUI3();
      g_runtime_state = 1;
    } catch (const winrt::hresult_error& error) {
      Log("starting the Windows App Runtime; views on this thread use Win32 controls", error);
      g_runtime_state = -1;
    } catch (...) {
      std::cerr << "WinUI3 view: the Windows App Runtime did not start; views on this "
                   "thread use Win32 controls\n";
      g_runtime_state = -1;
    }
  }
  return g_runtime_state == 1;
}

void* XamlView::CreateControl(ViewKind kind, const std::string& text) {
  try {
    X::FrameworkElement element{nullptr};
    switch (kind) {
      case ViewKind::Label: {
        C::TextBlock block;
        block.Text(ToHString(text));
        block.TextWrapping(X::TextWrapping::Wrap);
        C::Border border;
        border.Child(block);
        border.Tag(winrt::box_value(kLabelTag));
        element = border;
        break;
      }
      case ViewKind::Button: {
        C::Button button;
        button.Content(winrt::box_value(ToHString(text)));
        button.Tag(winrt::box_value(kButtonTag));
        element = button;
        break;
      }
      case ViewKind::TextField: {
        C::TextBox box;
        box.Text(ToHString(text));
        C::PasswordBox password;
        password.Visibility(X::Visibility::Collapsed);
        C::Grid grid;
        grid.Children().Append(box);
        grid.Children().Append(password);
        grid.Tag(winrt::box_value(kTextFieldTag));
        element = grid;
        break;
      }
      case ViewKind::ImageView: {
        C::Image image;
        image.Stretch(M::Stretch::Uniform);
        image.Tag(winrt::box_value(kImageViewTag));
        element = image;
        break;
      }
      case ViewKind::Container:
      default:
        return nullptr;
    }
    return winrt::detach_abi(element.as<IInspectable>());
  } catch (const winrt::hresult_error& error) {
    Log("creating a control", error);
    return nullptr;
  }
}

XamlView::XamlView(ViewInternal::Impl& impl, void* native, bool owned)
    : state_(std::make_unique<State>()) {
  auto& s = *state_;
  s.impl = &impl;
  s.owned = owned;
  Guard("creating a view", [&] {
    if (!owned) {
      // A window's root: an island over its client area.
      s.window = static_cast<HWND>(native);
      s.canvas = C::Canvas();
      s.canvas.Background(Brush(Color{0, 0, 0, 0}));  // hit-testable, invisible
      s.element = s.canvas;
      s.source = H::DesktopWindowXamlSource();
      s.source.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(s.window));
      s.source.SiteBridge().ResizePolicy(
          winrt::Microsoft::UI::Content::ContentSizePolicy::ResizeContentToParentWindow);
      s.source.Content(s.canvas);
      s.FitIsland();
      s.source.SiteBridge().Show();
      // Measuring needs a live tree; lay out again once there is one.
      ViewInternal::Impl* root = &impl;
      s.loaded = s.canvas.Loaded(winrt::auto_revoke,
                                 [root](auto&&, auto&&) { root->OnNativeResized(); });
      return;  // impl.native stays the HWND: Window's cache compares against it
    }
    if (native) {
      IInspectable inspectable{nullptr};
      winrt::attach_abi(inspectable, native);  // takes CreateControl's reference
      s.element = inspectable.as<X::FrameworkElement>();
    } else {
      s.canvas = C::Canvas();
      s.element = s.canvas;
    }
    s.kind = KindOf(s.element);
    switch (s.kind) {
      case ViewKind::Label:
        s.label_border = s.element.as<C::Border>();
        s.label_text = s.label_border.Child().as<C::TextBlock>();
        break;
      case ViewKind::Button:
        s.button = s.element.as<C::Button>();
        break;
      case ViewKind::TextField:
        s.field_host = s.element.as<C::Grid>();
        s.text_box = s.field_host.Children().GetAt(0).as<C::TextBox>();
        s.password_box = s.field_host.Children().GetAt(1).as<C::PasswordBox>();
        break;
      case ViewKind::ImageView:
        s.image = s.element.as<C::Image>();
        break;
      case ViewKind::Container:
      default:
        if (!s.canvas) s.canvas = s.element.try_as<C::Canvas>();
        break;
    }
    impl.native = winrt::get_abi(s.element.as<IInspectable>());
  });
}

XamlView::~XamlView() {
  auto& s = *state_;
  Guard("destroying a view", [&] {
    s.focus_when_loaded.revoke();
    StopListening();
    ObserveResize(false);
    s.loaded.revoke();
    if (s.source) {
      s.source.Close();
      s.source = nullptr;
    }
    // Leave the parent's children, so nothing refers to a dead element.
    if (s.owned && s.element) {
      if (auto parent = M::VisualTreeHelper::GetParent(s.element).try_as<C::Panel>()) {
        uint32_t index = 0;
        if (parent.Children().IndexOf(s.element, index)) parent.Children().RemoveAt(index);
      }
    }
  });
}

// ---------------------------------------------------------------------------
// The seam
// ---------------------------------------------------------------------------

void XamlView::SetFrame(Rectangle frame) {
  auto& s = *state_;
  if (s.window) return;  // a root follows the window
  s.frame = frame;
  s.has_frame = true;
  Guard("SetFrame", [&] {
    C::Canvas::SetLeft(s.element, frame.x);
    C::Canvas::SetTop(s.element, frame.y);
    s.element.Width((std::max)(0.0, frame.width));
    s.element.Height((std::max)(0.0, frame.height));
  });
}

Rectangle XamlView::GetFrame() const {
  auto& s = *state_;
  if (s.window) {
    RECT rect{};
    GetClientRect(s.window, &rect);
    const double scale = ScaleFor(s.window);
    return Rectangle{0, 0, (rect.right - rect.left) / scale, (rect.bottom - rect.top) / scale};
  }
  if (s.has_frame) return s.frame;
  Rectangle frame{0, 0, 0, 0};
  Guard("GetFrame", [&] {
    frame.width = s.element.ActualWidth();
    frame.height = s.element.ActualHeight();
  });
  return frame;
}

Size XamlView::GetIntrinsicSize() const {
  auto& s = *state_;
  if (s.kind == ViewKind::Container || s.window) return Size{0, 0};
  Size size{0, 0};
  Guard("GetIntrinsicSize", [&] {
    // Measure what the element wants, not the size the layout gave it.
    X::FrameworkElement target = s.element;
    if (s.kind == ViewKind::TextField) target = s.FocusTarget();
    const double width = target.Width();
    const double height = target.Height();
    target.ClearValue(X::FrameworkElement::WidthProperty());
    target.ClearValue(X::FrameworkElement::HeightProperty());
    if (auto control = target.try_as<C::Control>()) control.ApplyTemplate();
    const float infinite = std::numeric_limits<float>::infinity();
    target.Measure(winrt::Windows::Foundation::Size{infinite, infinite});
    const auto desired = target.DesiredSize();
    if (!std::isnan(width)) target.Width(width);
    if (!std::isnan(height)) target.Height(height);
    size = Size{desired.Width, desired.Height};
    if (s.kind == ViewKind::TextField) size.width = 0;  // like an editable field elsewhere
  });
  return size;
}

void XamlView::AddSubview(XamlView* child, size_t index) {
  auto& s = *state_;
  if (!child || !s.canvas) return;
  Guard("AddSubview", [&] {
    auto children = s.canvas.Children();
    const auto count = children.Size();
    children.InsertAt(static_cast<uint32_t>((std::min)(index, static_cast<size_t>(count))),
                      child->state_->element);
  });
}

void XamlView::RemoveSubview(XamlView* child) {
  auto& s = *state_;
  if (!child || !s.canvas) return;
  Guard("RemoveSubview", [&] {
    uint32_t index = 0;
    if (s.canvas.Children().IndexOf(child->state_->element, index)) {
      s.canvas.Children().RemoveAt(index);
    }
  });
}

void XamlView::SetVisible(bool is_visible) {
  auto& s = *state_;
  if (s.window) return;
  Guard("SetVisible", [&] {
    s.element.Visibility(is_visible ? X::Visibility::Visible : X::Visibility::Collapsed);
  });
}

void XamlView::SetEnabled(bool is_enabled) {
  auto& s = *state_;
  if (s.window) return;
  s.enabled = is_enabled;
  Guard("SetEnabled", [&] {
    for (auto& control : s.Controls()) control.IsEnabled(is_enabled);
    if (s.label_text) s.label_text.Opacity(is_enabled ? 1.0 : 0.5);
  });
}

bool XamlView::IsEnabled() const {
  return state_->enabled;
}

void XamlView::SetBackgroundColor(Color color) {
  auto& s = *state_;
  Guard("SetBackgroundColor", [&] {
    const bool none = color.a == 0;
    if (s.canvas) {
      // Kept hit-testable when transparent, as a Win32 container is.
      s.canvas.Background(Brush(none ? Color{0, 0, 0, 0} : color));
    } else if (s.label_border) {
      if (none) {
        s.label_border.ClearValue(C::Border::BackgroundProperty());
      } else {
        s.label_border.Background(Brush(color));
      }
    } else {
      for (auto& control : s.Controls()) {
        if (none) {
          control.ClearValue(C::Control::BackgroundProperty());
        } else {
          control.Background(Brush(color));
        }
      }
    }
  });
}

void XamlView::SetTooltip(const std::optional<std::string>& tooltip) {
  auto& s = *state_;
  Guard("SetTooltip", [&] {
    C::ToolTipService::SetToolTip(
        s.element, tooltip ? winrt::box_value(ToHString(*tooltip)) : IInspectable{nullptr});
  });
}

void XamlView::Focus() {
  auto& s = *state_;
  Guard("Focus", [&] {
    auto target = s.FocusTarget();
    if (!target) return;
    s.focus_when_loaded.revoke();
    if (target.IsLoaded()) {
      target.Focus(X::FocusState::Programmatic);
      return;
    }
    // Views are usually built and focused before the window is shown, when
    // the island has not loaded its content yet.
    State* state = state_.get();
    s.focus_when_loaded = target.Loaded(winrt::auto_revoke, [state](auto&&, auto&&) {
      state->focus_when_loaded.revoke();
      Guard("Focus", [state] {
        if (auto loaded = state->FocusTarget()) loaded.Focus(X::FocusState::Programmatic);
      });
    });
  });
}

void XamlView::Blur() {
  if (!IsFocused()) return;
  // XAML cannot focus "nothing": hand the keyboard focus from the island (the
  // Win32 focus is on it while a XAML control has the XAML focus) back to the
  // window that hosts it.
  if (HWND focused = ::GetFocus()) {
    if (HWND window = GetAncestor(focused, GA_ROOT)) ::SetFocus(window);
  }
}

bool XamlView::IsFocused() const {
  auto& s = *state_;
  bool focused = false;
  Guard("IsFocused", [&] {
    auto target = s.FocusTarget();
    if (!target || !target.XamlRoot()) return;
    const auto current = X::Input::FocusManager::GetFocusedElement(target.XamlRoot());
    focused = Same(current, target);
  });
  return focused;
}

void XamlView::StartListening() {
  auto& s = *state_;
  if (s.listening) return;
  s.listening = true;
  State* state = state_.get();
  Guard("StartListening", [&] {
    if (s.button) {
      s.click = s.button.Click(winrt::auto_revoke, [state](auto&&, auto&&) {
        state->Emit(ButtonClickedEvent(state->impl->id));
      });
    }
    if (s.text_box) {
      s.text_changed = s.text_box.TextChanged(winrt::auto_revoke, [state](auto&&, auto&&) {
        if (state->suppress_change || state->secure) return;
        state->Emit(TextFieldChangedEvent(state->impl->id,
                                          winrt::to_string(state->text_box.Text())));
      });
      s.password_changed =
          s.password_box.PasswordChanged(winrt::auto_revoke, [state](auto&&, auto&&) {
            if (state->suppress_change || !state->secure) return;
            state->Emit(TextFieldChangedEvent(state->impl->id,
                                              winrt::to_string(state->password_box.Password())));
          });
      auto on_key = [state](auto&&, const X::Input::KeyRoutedEventArgs& args) {
        if (args.Key() != winrt::Windows::System::VirtualKey::Enter) return;
        if (state->multiline && !state->secure) return;  // a newline, not a submit
        args.Handled(true);
        state->Emit(TextFieldSubmittedEvent(state->impl->id));
      };
      s.text_key = s.text_box.PreviewKeyDown(winrt::auto_revoke, on_key);
      s.password_key = s.password_box.PreviewKeyDown(winrt::auto_revoke, on_key);
    }
    if (s.button || s.field_host) {
      // GotFocus / LostFocus bubble: the text field's Grid hears both boxes.
      X::UIElement source = s.button ? X::UIElement(s.button) : X::UIElement(s.field_host);
      s.got_focus = source.GotFocus(winrt::auto_revoke, [state](auto&&, auto&&) {
        state->Emit(ViewFocusedEvent(state->impl->id));
      });
      s.lost_focus = source.LostFocus(winrt::auto_revoke, [state](auto&&, auto&&) {
        state->Emit(ViewBlurredEvent(state->impl->id));
      });
    }
  });
}

void XamlView::StopListening() {
  auto& s = *state_;
  s.listening = false;
  s.click.revoke();
  s.text_changed.revoke();
  s.password_changed.revoke();
  s.text_key.revoke();
  s.password_key.revoke();
  s.got_focus.revoke();
  s.lost_focus.revoke();
}

void XamlView::ObserveResize(bool observe) {
  auto& s = *state_;
  if (!s.window) return;
  auto& dispatcher = WindowMessageDispatcher::GetInstance();
  if (observe && !s.resize_handler_id) {
    State* state = state_.get();
    s.resize_handler_id = dispatcher.RegisterHandler(
        s.window, [state](HWND, UINT msg, WPARAM, LPARAM) -> std::optional<LRESULT> {
          if (msg == WM_SIZE || msg == WM_DPICHANGED) {
            Guard("resizing the island", [state] { state->FitIsland(); });
            state->impl->OnNativeResized();
          }
          return std::nullopt;
        });
  } else if (!observe && s.resize_handler_id) {
    dispatcher.UnregisterHandler(s.resize_handler_id);
    s.resize_handler_id = 0;
  }
}

// ---------------------------------------------------------------------------
// The controls
// ---------------------------------------------------------------------------

void XamlView::SetText(const std::string& text) {
  auto& s = *state_;
  Guard("SetText", [&] {
    const auto value = ToHString(text);
    if (s.label_text) {
      s.label_text.Text(value);
    } else if (s.button) {
      s.button.Content(winrt::box_value(value));
    } else if (s.text_box) {
      ++s.suppress_change;
      s.text_box.Text(value);
      s.password_box.Password(value);
      --s.suppress_change;
      return;  // a text field's height does not follow its text
    }
  });
  s.impl->InvalidateIntrinsicSize();
}

std::string XamlView::GetText() const {
  auto& s = *state_;
  std::string text;
  Guard("GetText", [&] {
    if (s.label_text) {
      text = winrt::to_string(s.label_text.Text());
    } else if (s.button) {
      text = winrt::to_string(winrt::unbox_value_or<winrt::hstring>(s.button.Content(), winrt::hstring()));
    } else if (s.text_box) {
      text = winrt::to_string(s.secure ? s.password_box.Password() : s.text_box.Text());
    }
  });
  return text;
}

void XamlView::SetTextColor(Color color) {
  auto& s = *state_;
  Guard("SetTextColor", [&] {
    const bool none = color.a == 0;
    if (s.label_text) {
      if (none) {
        s.label_text.ClearValue(C::TextBlock::ForegroundProperty());
      } else {
        s.label_text.Foreground(Brush(color));
      }
    }
    for (auto& control : s.Controls()) {
      if (none) {
        control.ClearValue(C::Control::ForegroundProperty());
      } else {
        control.Foreground(Brush(color));
      }
    }
  });
}

void XamlView::SetFontSize(double size) {
  auto& s = *state_;
  Guard("SetFontSize", [&] {
    if (s.label_text) {
      if (size > 0) {
        s.label_text.FontSize(size);
      } else {
        s.label_text.ClearValue(C::TextBlock::FontSizeProperty());
      }
    }
    for (auto& control : s.Controls()) {
      if (size > 0) {
        control.FontSize(size);
      } else {
        control.ClearValue(C::Control::FontSizeProperty());
      }
    }
  });
  s.impl->InvalidateIntrinsicSize();
}

void XamlView::SetTextAlignment(TextAlignment alignment) {
  auto& s = *state_;
  Guard("SetTextAlignment", [&] {
    if (s.label_text) s.label_text.TextAlignment(ToXamlAlignment(alignment));
    if (s.text_box) s.text_box.TextAlignment(ToXamlAlignment(alignment));
    // PasswordBox has no alignment of its own.
  });
}

void XamlView::SetPlaceholder(const std::optional<std::string>& placeholder) {
  auto& s = *state_;
  if (!s.text_box) return;
  Guard("SetPlaceholder", [&] {
    const auto value = placeholder ? ToHString(*placeholder) : winrt::hstring();
    s.text_box.PlaceholderText(value);
    s.password_box.PlaceholderText(value);
  });
}

void XamlView::SetEditable(bool is_editable) {
  auto& s = *state_;
  if (!s.text_box) return;
  Guard("SetEditable", [&] {
    s.text_box.IsReadOnly(!is_editable);
    // PasswordBox has no read-only mode: a read-only secure field refuses input
    // by being disabled.
    s.password_box.IsEnabled(is_editable && s.enabled);
  });
}

void XamlView::SetSecure(bool is_secure) {
  auto& s = *state_;
  if (!s.text_box || s.secure == is_secure) return;
  Guard("SetSecure", [&] {
    const bool had_focus = IsFocused();
    ++s.suppress_change;
    if (is_secure) {
      s.password_box.Password(s.text_box.Text());
    } else {
      s.text_box.Text(s.password_box.Password());
    }
    --s.suppress_change;
    s.secure = is_secure;
    s.ShowActiveBox();
    if (had_focus) Focus();
  });
  s.impl->InvalidateIntrinsicSize();
}

void XamlView::SetMultiline(bool is_multiline) {
  auto& s = *state_;
  if (!s.text_box) return;
  s.multiline = is_multiline;
  Guard("SetMultiline", [&] {
    s.text_box.AcceptsReturn(is_multiline);
    s.text_box.TextWrapping(is_multiline ? X::TextWrapping::Wrap : X::TextWrapping::NoWrap);
  });
  s.impl->InvalidateIntrinsicSize();
}

void XamlView::SetImage(const std::shared_ptr<Image>& image) {
  auto& s = *state_;
  if (!s.image) return;
  Guard("SetImage", [&] {
    auto* bitmap = image ? static_cast<Gdiplus::Bitmap*>(image->GetNativeObject()) : nullptr;
    if (!bitmap || bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0) {
      s.image.Source(nullptr);
      return;
    }
    const UINT width = bitmap->GetWidth();
    const UINT height = bitmap->GetHeight();
    Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    Gdiplus::BitmapData data{};
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) !=
        Gdiplus::Ok) {
      s.image.Source(nullptr);
      return;
    }
    // WriteableBitmap is premultiplied BGRA, which is what PARGB is in memory.
    M::Imaging::WriteableBitmap target(static_cast<int32_t>(width), static_cast<int32_t>(height));
    auto buffer = target.PixelBuffer();
    uint8_t* destination = buffer.data();
    const auto* source = static_cast<const uint8_t*>(data.Scan0);
    for (UINT row = 0; row < height; ++row) {
      std::memcpy(destination + static_cast<size_t>(row) * width * 4,
                  source + static_cast<ptrdiff_t>(row) * data.Stride, static_cast<size_t>(width) * 4);
    }
    bitmap->UnlockBits(&data);
    target.Invalidate();
    s.image.Source(target);
  });
  s.impl->InvalidateIntrinsicSize();
}

}  // namespace nativeapi
