#include "menu_winui3_windows.h"
#include "application_theme_windows.h"
#include "winui3_runtime_windows.h"
#undef GetCurrentTime

#include <gdiplus.h>
#include <robuffer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Interop.h>

#include <iostream>
#include <map>
#include <set>
#include "../../image.h"

namespace nativeapi {
namespace {
namespace X = winrt::Microsoft::UI::Xaml;
namespace C = X::Controls;
namespace P = C::Primitives;
namespace H = X::Hosting;

P::FlyoutPlacementMode ConvertPlacement(Placement placement) {
  switch (placement) {
    case Placement::Top: return P::FlyoutPlacementMode::Top;
    case Placement::TopStart: return P::FlyoutPlacementMode::TopEdgeAlignedLeft;
    case Placement::TopEnd: return P::FlyoutPlacementMode::TopEdgeAlignedRight;
    case Placement::Right: return P::FlyoutPlacementMode::Right;
    case Placement::RightStart: return P::FlyoutPlacementMode::RightEdgeAlignedTop;
    case Placement::RightEnd: return P::FlyoutPlacementMode::RightEdgeAlignedBottom;
    case Placement::Bottom: return P::FlyoutPlacementMode::Bottom;
    case Placement::BottomStart: return P::FlyoutPlacementMode::BottomEdgeAlignedLeft;
    case Placement::BottomEnd: return P::FlyoutPlacementMode::BottomEdgeAlignedRight;
    case Placement::Left: return P::FlyoutPlacementMode::Left;
    case Placement::LeftStart: return P::FlyoutPlacementMode::LeftEdgeAlignedTop;
    case Placement::LeftEnd: return P::FlyoutPlacementMode::LeftEdgeAlignedBottom;
  }
  winrt::throw_hresult(E_INVALIDARG);
}

std::string AcceleratorText(const KeyboardAccelerator& key) {
  if (key.key.empty()) return {};
  std::string text;
  if ((key.modifiers & ModifierKey::Ctrl) != ModifierKey::None) text += "Ctrl+";
  if ((key.modifiers & ModifierKey::Alt) != ModifierKey::None) text += "Alt+";
  if ((key.modifiers & ModifierKey::Shift) != ModifierKey::None) text += "Shift+";
  if ((key.modifiers & ModifierKey::Meta) != ModifierKey::None) text += "Win+";
  return text + key.key;
}

C::IconElement MakeIcon(const std::shared_ptr<Image>& image) {
  if (!image) return nullptr;
  auto* native = static_cast<Gdiplus::Bitmap*>(image->GetNativeObject());
  if (!native) return nullptr;
  const int width = static_cast<int>(native->GetWidth());
  const int height = static_cast<int>(native->GetHeight());
  X::Media::Imaging::WriteableBitmap bitmap(width, height);
  auto access = bitmap.PixelBuffer().as<::Windows::Storage::Streams::IBufferByteAccess>();
  BYTE* pixels = nullptr;
  winrt::check_hresult(access->Buffer(&pixels));
  Gdiplus::Rect rect(0, 0, width, height);
  Gdiplus::BitmapData data{};
  if (native->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data)
      != Gdiplus::Ok) winrt::throw_hresult(E_FAIL);
  for (int y = 0; y < height; ++y)
    memcpy(pixels + static_cast<size_t>(y) * width * 4,
           static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride,
           static_cast<size_t>(width) * 4);
  native->UnlockBits(&data);
  bitmap.Invalidate();
  C::ImageIcon icon;
  icon.Source(bitmap);
  return icon;
}

}  // namespace

class WinUI3MenuSession::Impl {
 public:
  inline static thread_local Impl* active = nullptr;
  std::map<MenuItem*, C::MenuFlyoutItemBase> controls;
  HWND host = nullptr;
  DWORD thread = GetCurrentThreadId();
  H::DesktopWindowXamlSource source{nullptr};
  C::Grid root{nullptr};
  C::MenuFlyout flyout{nullptr};
  bool done = false;
  bool opened = false;
  bool presented = false;
  std::exception_ptr failure;
  X::FrameworkElement::Loaded_revoker loaded_event;
  P::FlyoutBase::Opened_revoker opened_event;
  P::FlyoutBase::Closed_revoker closed_event;
  std::set<Menu*> ancestors;
  std::vector<std::shared_ptr<MenuItem>> retained_items;

  ~Impl() {
    try { if (source) source.Close(); } catch (...) {}
    if (host) DestroyWindow(host);
  }

  void Refresh(MenuItem& item) {
    const auto found = controls.find(&item);
    if (found == controls.end()) return;
    const auto& control = found->second;
    control.IsEnabled(item.IsEnabled());
    auto label = winrt::to_hstring(item.GetLabel().value_or(""));
    if (auto normal = control.try_as<C::MenuFlyoutItem>()) {
      normal.Text(label);
      normal.Icon(MakeIcon(item.GetIcon()));
      normal.KeyboardAcceleratorTextOverride(winrt::to_hstring(AcceleratorText(item.GetAccelerator())));
    } else if (auto sub = control.try_as<C::MenuFlyoutSubItem>()) {
      sub.Text(label);
      sub.Icon(MakeIcon(item.GetIcon()));
    }
    if (auto toggle = control.try_as<C::ToggleMenuFlyoutItem>())
      toggle.IsChecked(item.GetState() == MenuItemState::Checked);
    if (auto radio = control.try_as<C::RadioMenuFlyoutItem>())
      radio.IsChecked(item.GetState() == MenuItemState::Checked);
    X::Automation::AutomationProperties::SetName(control, label);
    C::ToolTipService::SetToolTip(control, item.GetTooltip()
        ? winrt::box_value(winrt::to_hstring(*item.GetTooltip())) : nullptr);
  }

  void Build(Menu& menu, const winrt::Windows::Foundation::Collections::IVector<C::MenuFlyoutItemBase>& items) {
    if (!ancestors.insert(&menu).second) winrt::throw_hresult(E_INVALIDARG);
    for (const auto& item : menu.GetAllItems()) {
      if (!item) continue;
      retained_items.push_back(item);
      C::MenuFlyoutItemBase control{nullptr};
      if (item->GetType() == MenuItemType::Separator) {
        control = C::MenuFlyoutSeparator();
      } else if (auto submenu = item->GetSubmenu()) {
        C::MenuFlyoutSubItem sub;
        sub.Text(winrt::to_hstring(item->GetLabel().value_or("")));
        sub.Icon(MakeIcon(item->GetIcon()));
        Build(*submenu, sub.Items());
        // The first child is loaded/unloaded with its submenu presenter.
        if (sub.Items().Size()) {
          auto visible = std::make_shared<bool>(false);
          sub.Items().GetAt(0).Loaded([submenu, visible](auto&&, auto&&) {
            if (!*visible) { *visible = true; submenu->Emit<MenuOpenedEvent>(submenu->GetId()); }
          });
          sub.Items().GetAt(0).Unloaded([submenu, visible](auto&&, auto&&) {
            if (*visible) { *visible = false; submenu->Emit<MenuClosedEvent>(submenu->GetId()); }
          });
        }
        control = sub;
      } else {
        C::MenuFlyoutItem normal{nullptr};
        if (item->GetType() == MenuItemType::Checkbox) {
          C::ToggleMenuFlyoutItem toggle;
          toggle.IsChecked(item->GetState() == MenuItemState::Checked);
          normal = toggle;
        } else if (item->GetType() == MenuItemType::Radio) {
          C::RadioMenuFlyoutItem radio;
          radio.IsChecked(item->GetState() == MenuItemState::Checked);
          radio.GroupName(winrt::to_hstring(std::to_string(menu.GetId()) + ":" +
              (item->GetRadioGroup() >= 0 ? std::to_string(item->GetRadioGroup())
                                        : "item:" + std::to_string(item->GetId()))));
          normal = radio;
        } else {
          normal = C::MenuFlyoutItem();
        }
        normal.Text(winrt::to_hstring(item->GetLabel().value_or("")));
        normal.Icon(MakeIcon(item->GetIcon()));
        normal.KeyboardAcceleratorTextOverride(winrt::to_hstring(AcceleratorText(item->GetAccelerator())));
        normal.Click([item](auto&&, auto&&) {
          // Match the nativeapi contract: the application owns checked state.
          if (item->IsEnabled()) item->Emit<MenuItemClickedEvent>(item->GetId());
        });
        control = normal;
      }
      control.IsEnabled(item->IsEnabled());
      X::Automation::AutomationProperties::SetName(control, winrt::to_hstring(item->GetLabel().value_or("")));
      if (auto tooltip = item->GetTooltip())
        C::ToolTipService::SetToolTip(control, winrt::box_value(winrt::to_hstring(*tooltip)));
      items.Append(control);
      controls.insert_or_assign(item.get(), control);
    }
    ancestors.erase(&menu);
  }
};

WinUI3MenuSession::WinUI3MenuSession() : pimpl_(std::make_unique<Impl>()) {}
WinUI3MenuSession::~WinUI3MenuSession() = default;

bool WinUI3MenuSession::Open(Menu& menu, HWND owner, POINT anchor, Placement placement) {
  auto& impl = *pimpl_;
  if (Impl::active) return false;
  Impl::active = &impl;
  struct ActiveScope { ~ActiveScope() { Impl::active = nullptr; } } active_scope;
  const HWND previous_foreground = GetForegroundWindow();
  bool success = false;
  try {
    InitializeWinUI3();
    // The native dispatcher uses HWND_MESSAGE. A message-only window cannot
    // own a visible XAML popup; use a real active window or an unowned host.
    if (!IsWindowVisible(owner)) owner = GetActiveWindow();
    if (owner && !IsWindowVisible(owner)) owner = nullptr;
    impl.host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"nativeapi WinUI menu",
        WS_POPUP, anchor.x, anchor.y, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!impl.host) winrt::throw_last_error();
    impl.source = H::DesktopWindowXamlSource();
    impl.source.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(impl.host));
    impl.source.SiteBridge().ResizePolicy(winrt::Microsoft::UI::Content::ContentSizePolicy::ResizeContentToParentWindow);
    impl.source.SiteBridge().MoveAndResize({0, 0, 1, 1});
    impl.source.SiteBridge().Show();
    impl.root = C::Grid();
    switch (application_brightness.load()) {
      case Brightness::Light:
        impl.root.RequestedTheme(X::ElementTheme::Light);
        break;
      case Brightness::Dark:
        impl.root.RequestedTheme(X::ElementTheme::Dark);
        break;
      case Brightness::System:
        impl.root.RequestedTheme(X::ElementTheme::Default);
        break;
    }
    impl.root.Width(1);
    impl.root.Height(1);
    impl.source.Content(impl.root);
    // The tray overflow panel can remain topmost while dispatching a click to
    // our process. HWND_TOP/foreground activation alone cannot put a normal
    // island above it. Elevate only this short-lived popup host (and its owned
    // XAML popups), never the application's window. DestroyWindow below ends
    // the topmost lifetime when the menu closes.
    SetWindowPos(impl.host, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetForegroundWindow(impl.host);
    impl.flyout = C::MenuFlyout();
    impl.flyout.ShouldConstrainToRootBounds(false);
    impl.flyout.XamlRoot(impl.root.XamlRoot());
    impl.closed_event = impl.flyout.Closed(winrt::auto_revoke, [&impl](auto&&, auto&&) { impl.done = true; });
    impl.opened_event = impl.flyout.Opened(winrt::auto_revoke, [&impl](auto&&, auto&&) { impl.presented = true; });
    impl.opened = true;
    menu.Emit<MenuOpenedEvent>(menu.GetId());
    if (!impl.done) {
      impl.Build(menu, impl.flyout.Items());
      if (!impl.flyout.Items().Size()) winrt::throw_hresult(E_INVALIDARG);
      // Place against the 1x1 root at the anchor, not a Position point: with a point the
      // flyout ignores the edge alignment (TopEdgeAlignedRight opened left-aligned).
      P::FlyoutShowOptions options;
      options.Placement(ConvertPlacement(placement));
      // ShowAt before the island's first layout can silently fail to present.
      impl.loaded_event = impl.root.Loaded(winrt::auto_revoke, [&impl, options](auto&&, auto&&) {
        try { if (!impl.done) impl.flyout.ShowAt(impl.root, options); }
        catch (...) { impl.failure = std::current_exception(); impl.done = true; }
      });
      if (impl.root.IsLoaded()) impl.flyout.ShowAt(impl.root, options);
      success = true;
      while (!impl.done) {
        MSG msg{};
        const int result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
          if (!result) PostQuitMessage(static_cast<int>(msg.wParam));
          else success = false;
          break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
          impl.flyout.Hide();
        } else {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
      }
      success = success && impl.presented;
      if (impl.failure) std::rethrow_exception(impl.failure);
    } else {
      success = true; // Close() called from the opening listener.
    }
  } catch (const winrt::hresult_error& error) {
    success = false;
    std::cerr << "WinUI3 menu: " << winrt::to_string(error.message()) << '\n';
  } catch (const std::exception& error) {
    success = false;
    std::cerr << "WinUI3 menu: " << error.what() << '\n';
  }
  try {
    if (impl.flyout) impl.flyout.Hide();
    if (impl.source) impl.source.Close();
    impl.source = nullptr;
  } catch (...) { success = false; }
  impl.loaded_event.revoke();
  impl.opened_event.revoke();
  impl.closed_event.revoke();
  if (impl.host) {
    const HWND foreground = GetForegroundWindow();
    const bool restore = foreground == impl.host ||
        (foreground && GetAncestor(foreground, GA_ROOTOWNER) == GetAncestor(impl.host, GA_ROOTOWNER));
    DestroyWindow(impl.host);
    impl.host = nullptr;
    if (restore && IsWindow(previous_foreground)) SetForegroundWindow(previous_foreground);
  }
  if (impl.opened) menu.Emit<MenuClosedEvent>(menu.GetId());
  return success;
}

bool WinUI3MenuSession::Close() {
  if (GetCurrentThreadId() != pimpl_->thread) return false;
  pimpl_->done = true;
  try { if (pimpl_->flyout) pimpl_->flyout.Hide(); }
  catch (const winrt::hresult_error&) { return false; }
  return true;
}

void WinUI3MenuSession::Refresh(MenuItem& item) {
  if (!Impl::active) return;
  try { Impl::active->Refresh(item); }
  catch (const winrt::hresult_error& error) {
    std::cerr << "WinUI3 menu update: " << winrt::to_string(error.message()) << '\n';
  }
}

}  // namespace nativeapi
