#include <windows.h>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>
#include "../../foundation/id_allocator.h"
#include "../../image.h"
#include "../../menu.h"
#include "../../window.h"
#include "application_theme_windows.h"
#include "dpi_utils_windows.h"
#include "string_utils_windows.h"
#include "window_message_dispatcher.h"
#ifdef NATIVEAPI_ENABLE_WINUI3
#include "menu_winui3_windows.h"
#endif

namespace nativeapi {

namespace {

// Classic popup menus require the undocumented UxTheme dark-mode opt-in; DWM's
// title-bar setting does not affect them. Resolve it only on Windows 10 1903+
// (including Windows 11): ordinal 135 had a different signature in 1809.
class MenuThemeScope {
 public:
  explicit MenuThemeScope(HWND window) : window_(window) {
    const auto& api = GetApi();
    if (!api.module)
      return;

    HIGHCONTRASTW contrast = {sizeof(contrast)};
    const bool allow_dark =
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        !(contrast.dwFlags & HCF_HIGHCONTRASTON);

    AppMode mode = AppMode::Default;
    if (allow_dark) {
      switch (application_brightness.load()) {
        case Brightness::Light:
          mode = AppMode::ForceLight;
          break;
        case Brightness::Dark:
          mode = AppMode::ForceDark;
          break;
        case Brightness::System:
          mode = AppMode::AllowDark;
          break;
      }
    }
    previous_mode_ = api.set_mode(mode);
    previous_window_dark_ = api.is_window_dark(window_);
    api.allow_window_dark(window_, allow_dark);
    // Read the current preference on every open, including when a host does not
    // forward WM_SETTINGCHANGE to nativeapi (e.g. a tray-only application).
    api.refresh_policy();
    api.flush_menus();
  }

  ~MenuThemeScope() {
    const auto& api = GetApi();
    if (!api.module)
      return;
    api.allow_window_dark(window_, previous_window_dark_);
    api.set_mode(previous_mode_);
    api.flush_menus();
  }

  MenuThemeScope(const MenuThemeScope&) = delete;
  MenuThemeScope& operator=(const MenuThemeScope&) = delete;

 private:
  enum class AppMode { Default, AllowDark, ForceDark, ForceLight };
  struct Api {
    HMODULE module = nullptr;
    AppMode(WINAPI* set_mode)(AppMode) = nullptr;
    bool(WINAPI* allow_window_dark)(HWND, bool) = nullptr;
    bool(WINAPI* is_window_dark)(HWND) = nullptr;
    void(WINAPI* refresh_policy)() = nullptr;
    void(WINAPI* flush_menus)() = nullptr;

    Api() {
      using GetVersion = void(WINAPI*)(DWORD*, DWORD*, DWORD*);
      const auto ntdll = GetModuleHandleW(L"ntdll.dll");
      if (!ntdll)
        return;
      const auto get_version =
          reinterpret_cast<GetVersion>(GetProcAddress(ntdll, "RtlGetNtVersionNumbers"));
      if (!get_version)
        return;
      DWORD major = 0, minor = 0, build = 0;
      get_version(&major, &minor, &build);
      build &= 0x0fffffff;
      if (major != 10 || minor != 0 || build < 18362)
        return;

      module = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
      if (!module)
        return;
      set_mode =
          reinterpret_cast<decltype(set_mode)>(GetProcAddress(module, MAKEINTRESOURCEA(135)));
      allow_window_dark = reinterpret_cast<decltype(allow_window_dark)>(
          GetProcAddress(module, MAKEINTRESOURCEA(133)));
      is_window_dark =
          reinterpret_cast<decltype(is_window_dark)>(GetProcAddress(module, MAKEINTRESOURCEA(137)));
      refresh_policy =
          reinterpret_cast<decltype(refresh_policy)>(GetProcAddress(module, MAKEINTRESOURCEA(104)));
      flush_menus =
          reinterpret_cast<decltype(flush_menus)>(GetProcAddress(module, MAKEINTRESOURCEA(136)));
      if (!set_mode || !allow_window_dark || !is_window_dark || !refresh_policy || !flush_menus) {
        FreeLibrary(module);
        module = nullptr;
      }
    }

    ~Api() {
      if (module)
        FreeLibrary(module);
    }
  };

  static const Api& GetApi() {
    static const Api api;
    return api;
  }

  HWND window_;
  AppMode previous_mode_ = AppMode::Default;
  bool previous_window_dark_ = false;
};

}  // namespace

HICON ImageToHICON(const Image* image, int width, int height);

// Helper function to convert KeyboardAccelerator to Windows accelerator
std::pair<UINT, UINT> ConvertAccelerator(const KeyboardAccelerator& accelerator) {
  UINT key = 0;
  UINT modifiers = 0;

  // Convert key
  if (!accelerator.key.empty()) {
    if (accelerator.key.length() == 1) {
      // Single character key
      char c = std::toupper(accelerator.key[0]);
      key = static_cast<UINT>(c);
    } else {
      // Special keys
      std::string key_str = accelerator.key;
      if (key_str == "F1")
        key = VK_F1;
      else if (key_str == "F2")
        key = VK_F2;
      else if (key_str == "F3")
        key = VK_F3;
      else if (key_str == "F4")
        key = VK_F4;
      else if (key_str == "F5")
        key = VK_F5;
      else if (key_str == "F6")
        key = VK_F6;
      else if (key_str == "F7")
        key = VK_F7;
      else if (key_str == "F8")
        key = VK_F8;
      else if (key_str == "F9")
        key = VK_F9;
      else if (key_str == "F10")
        key = VK_F10;
      else if (key_str == "F11")
        key = VK_F11;
      else if (key_str == "F12")
        key = VK_F12;
      else if (key_str == "Enter" || key_str == "Return")
        key = VK_RETURN;
      else if (key_str == "Tab")
        key = VK_TAB;
      else if (key_str == "Space")
        key = VK_SPACE;
      else if (key_str == "Escape")
        key = VK_ESCAPE;
      else if (key_str == "Delete" || key_str == "Backspace")
        key = VK_BACK;
      else if (key_str == "ArrowUp")
        key = VK_UP;
      else if (key_str == "ArrowDown")
        key = VK_DOWN;
      else if (key_str == "ArrowLeft")
        key = VK_LEFT;
      else if (key_str == "ArrowRight")
        key = VK_RIGHT;
    }
  }

  // Convert modifiers
  if ((accelerator.modifiers & ModifierKey::Ctrl) != ModifierKey::None) {
    modifiers |= FCONTROL;
  }
  if ((accelerator.modifiers & ModifierKey::Alt) != ModifierKey::None) {
    modifiers |= FALT;
  }
  if ((accelerator.modifiers & ModifierKey::Shift) != ModifierKey::None) {
    modifiers |= FSHIFT;
  }
  // Note: Windows doesn't have a direct equivalent for Meta key in accelerators

  return std::make_pair(key, modifiers);
}

// MenuItem::Impl implementation
class MenuItem::Impl {
 public:
  MenuItemId id_;
  HMENU parent_menu_;
  MenuItemType type_;
  std::optional<std::string> label_;
  std::shared_ptr<Image> image_;
  HICON menu_icon_;      // Stored icon for menu item with transparency support
  HBITMAP menu_bitmap_;  // Stored 32-bit ARGB bitmap with icon and transparent background
  std::optional<std::string> tooltip_;
  KeyboardAccelerator accelerator_;
  bool has_accelerator_;
  MenuItemState state_;
  bool enabled_;
  int radio_group_;
  std::shared_ptr<Menu> submenu_;
  int window_proc_handle_id_;
  std::function<void(MenuItemId)> clicked_callback_;
  size_t submenu_opened_listener_id_;
  size_t submenu_closed_listener_id_;

  Impl(MenuItemId id, HMENU parent_menu, MenuItemType type)
      : id_(id),
        parent_menu_(parent_menu),
        type_(type),
        menu_icon_(nullptr),
        menu_bitmap_(nullptr),
        accelerator_("", ModifierKey::None),
        has_accelerator_(false),
        state_(MenuItemState::Unchecked),
        enabled_(true),
        radio_group_(-1),
        window_proc_handle_id_(-1),
        submenu_opened_listener_id_(0),
        submenu_closed_listener_id_(0) {}

  ~Impl() {
    // Unregister window procedure handler
    if (window_proc_handle_id_ != -1) {
      WindowMessageDispatcher::GetInstance().UnregisterHandler(window_proc_handle_id_);
    }

    // Remove submenu listeners before cleaning up submenu reference
    if (submenu_ && submenu_opened_listener_id_ != 0) {
      submenu_->RemoveListener(submenu_opened_listener_id_);
      submenu_opened_listener_id_ = 0;
    }
    if (submenu_ && submenu_closed_listener_id_ != 0) {
      submenu_->RemoveListener(submenu_closed_listener_id_);
      submenu_closed_listener_id_ = 0;
    }

    // Clean up menu icon
    if (menu_icon_) {
      DestroyIcon(menu_icon_);
      menu_icon_ = nullptr;
    }

    // Clean up menu bitmap
    if (menu_bitmap_) {
      DeleteObject(menu_bitmap_);
      menu_bitmap_ = nullptr;
    }

    // Windows menu items are automatically cleaned up when the menu is
    // destroyed
  }

  // Handle window procedure delegate for menu item clicks
  std::optional<LRESULT> HandleWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_COMMAND) {
      // For WM_COMMAND from menus, wparam contains the menu item ID
      // When using popup menus (TrackPopupMenu), the full 32-bit ID is
      // preserved in wparam, unlike menu bars which only use 16-bit IDs
      if (lparam == 0) {
        // Reconstruct the full 32-bit menu item ID from wparam
        MenuItemId menu_item_id = static_cast<MenuItemId>(wparam);

        // Check if this is our menu item
        if (menu_item_id == id_) {
          std::cout << "MenuItem: Item clicked, ID = " << menu_item_id << std::endl;
          // Call the clicked callback to emit the event
          if (clicked_callback_) {
            clicked_callback_(id_);
          }
          return 0;
        }
      }
    }
    return std::nullopt;  // Let other handlers process
  }
};

MenuItem::MenuItem(void* native_item)
    : pimpl_(std::make_unique<Impl>(IdAllocator::Allocate<MenuItem>(),
                                    nullptr,
                                    MenuItemType::Normal)) {
  // Set clicked callback to emit event
  pimpl_->clicked_callback_ = [this](MenuItemId id) { Emit<MenuItemClickedEvent>(id); };

  // Register window procedure handler for menu item clicks
  HWND host_window = WindowMessageDispatcher::GetInstance().GetHostWindow();
  if (host_window) {
    pimpl_->window_proc_handle_id_ = WindowMessageDispatcher::GetInstance().RegisterHandler(
        host_window, [this](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
          return pimpl_->HandleWindowProc(hwnd, message, wparam, lparam);
        });
  }
}

MenuItem::MenuItem(const std::string& label, MenuItemType type)
    : pimpl_(std::make_unique<Impl>(IdAllocator::Allocate<MenuItem>(), nullptr, type)) {
  pimpl_->label_ = label;

  // Set clicked callback to emit event
  pimpl_->clicked_callback_ = [this](MenuItemId id) { Emit<MenuItemClickedEvent>(id); };

  // Register window procedure handler for menu item clicks
  HWND host_window = WindowMessageDispatcher::GetInstance().GetHostWindow();
  if (host_window) {
    pimpl_->window_proc_handle_id_ = WindowMessageDispatcher::GetInstance().RegisterHandler(
        host_window, [this](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
          return pimpl_->HandleWindowProc(hwnd, message, wparam, lparam);
        });
  }
}

MenuItem::~MenuItem() {}

MenuItemId MenuItem::GetId() const {
  return pimpl_->id_;
}

MenuItemType MenuItem::GetType() const {
  return pimpl_->type_;
}

void MenuItem::SetLabel(const std::optional<std::string>& label) {
  pimpl_->label_ = label;
  if (pimpl_->parent_menu_) {
    MENUITEMINFOW mii = {};
    mii.cbSize = sizeof(MENUITEMINFOW);
    mii.fMask = MIIM_STRING;
    std::string label_str = label.has_value() ? *label : "";
    std::wstring w_label_str = StringToWString(label_str);
    mii.dwTypeData = const_cast<LPWSTR>(w_label_str.c_str());
    SetMenuItemInfoW(pimpl_->parent_menu_, pimpl_->id_, FALSE, &mii);
  }
#ifdef NATIVEAPI_ENABLE_WINUI3
  WinUI3MenuSession::Refresh(*this);
#endif
}

std::optional<std::string> MenuItem::GetLabel() const {
  return pimpl_->label_;
}

void MenuItem::SetIcon(std::shared_ptr<Image> image) {
  // Clean up previous resources
  if (pimpl_->menu_icon_) {
    DestroyIcon(pimpl_->menu_icon_);
    pimpl_->menu_icon_ = nullptr;
  }
  if (pimpl_->menu_bitmap_) {
    DeleteObject(pimpl_->menu_bitmap_);
    pimpl_->menu_bitmap_ = nullptr;
  }

  pimpl_->image_ = image;

  if (image && pimpl_->parent_menu_) {
    // Use 32x32 for menu icons
    const int iconSize = 32;

    // Convert image to HICON first for proper transparency support
    HICON hIcon = ImageToHICON(image.get(), iconSize, iconSize);

    if (hIcon) {
      pimpl_->menu_icon_ = hIcon;

      // Create a 32-bit ARGB bitmap
      HDC hdc = GetDC(nullptr);

      // Create BITMAPINFO for 32-bit ARGB
      BITMAPINFO bmi = {};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = iconSize;
      bmi.bmiHeader.biHeight = -iconSize;  // Negative for top-down DIB
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;

      void* pBits = nullptr;
      HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);

      if (hBmp && pBits) {
        // Fill with fully transparent pixels (all-zero ARGB)
        // This ensures the icon blends seamlessly with the menu background.
        // On Windows Vista+, MIIM_BITMAP with a 32-bit ARGB DIB section
        // respects the alpha channel for proper transparency.
        DWORD* pixels = static_cast<DWORD*>(pBits);
        memset(pixels, 0, iconSize * iconSize * sizeof(DWORD));

        // Draw the icon on the bitmap
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

        // Draw icon with proper blending
        DrawIconEx(hdcMem, 0, 0, hIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);

        SelectObject(hdcMem, hOldBmp);
        DeleteDC(hdcMem);

        // Store the bitmap for cleanup
        pimpl_->menu_bitmap_ = hBmp;

        // Set the bitmap on the menu item
        MENUITEMINFOW mii = {};
        mii.cbSize = sizeof(MENUITEMINFOW);
        mii.fMask = MIIM_BITMAP;
        mii.hbmpItem = hBmp;
        SetMenuItemInfoW(pimpl_->parent_menu_, pimpl_->id_, FALSE, &mii);
      }

      ReleaseDC(nullptr, hdc);
    }
  }
#ifdef NATIVEAPI_ENABLE_WINUI3
  WinUI3MenuSession::Refresh(*this);
#endif
}

std::shared_ptr<Image> MenuItem::GetIcon() const {
  return pimpl_->image_;
}

void MenuItem::SetTooltip(const std::optional<std::string>& tooltip) {
  pimpl_->tooltip_ = tooltip;
  // Windows doesn't have built-in tooltip support for menu items
  // This would require custom implementation
#ifdef NATIVEAPI_ENABLE_WINUI3
  WinUI3MenuSession::Refresh(*this);
#endif
}

std::optional<std::string> MenuItem::GetTooltip() const {
  return pimpl_->tooltip_;
}

void MenuItem::SetAccelerator(const std::optional<KeyboardAccelerator>& accelerator) {
  if (accelerator.has_value()) {
    pimpl_->accelerator_ = *accelerator;
    pimpl_->has_accelerator_ = true;
  } else {
    pimpl_->accelerator_ = KeyboardAccelerator("", ModifierKey::None);
    pimpl_->has_accelerator_ = false;
  }
  // Windows accelerators would be handled through accelerator tables
  // This is a placeholder implementation
#ifdef NATIVEAPI_ENABLE_WINUI3
  WinUI3MenuSession::Refresh(*this);
#endif
}

KeyboardAccelerator MenuItem::GetAccelerator() const {
  if (pimpl_->has_accelerator_) {
    return pimpl_->accelerator_;
  }
  return KeyboardAccelerator("", ModifierKey::None);
}

void MenuItem::SetEnabled(bool enabled) {
  pimpl_->enabled_ = enabled;
  if (pimpl_->parent_menu_) {
    EnableMenuItem(pimpl_->parent_menu_, pimpl_->id_, enabled ? MF_ENABLED : MF_GRAYED);
  }
#ifdef NATIVEAPI_ENABLE_WINUI3
  WinUI3MenuSession::Refresh(*this);
#endif
}

bool MenuItem::IsEnabled() const {
  return pimpl_->enabled_;
}

void MenuItem::SetState(MenuItemState state) {
  if (pimpl_->type_ == MenuItemType::Checkbox || pimpl_->type_ == MenuItemType::Radio) {
    pimpl_->state_ = state;

    if (pimpl_->parent_menu_) {
      UINT check_state = MF_UNCHECKED;
      if (state == MenuItemState::Checked) {
        check_state = MF_CHECKED;
      }
      CheckMenuItem(pimpl_->parent_menu_, pimpl_->id_, check_state);
    }

    // Handle radio button group logic
    if (pimpl_->type_ == MenuItemType::Radio && state == MenuItemState::Checked &&
        pimpl_->radio_group_ >= 0) {
      // Note: Radio group logic would need to be implemented differently
      // without global registry. This could be done by maintaining group
      // information in the parent menu or through other means.
      // For now, this functionality is disabled.
    }
  }
#ifdef NATIVEAPI_ENABLE_WINUI3
  WinUI3MenuSession::Refresh(*this);
#endif
}

MenuItemState MenuItem::GetState() const {
  return pimpl_->state_;
}

void MenuItem::SetRadioGroup(int group_id) {
  pimpl_->radio_group_ = group_id;
#ifdef NATIVEAPI_ENABLE_WINUI3
  WinUI3MenuSession::Refresh(*this);
#endif
}

int MenuItem::GetRadioGroup() const {
  return pimpl_->radio_group_;
}

void MenuItem::SetSubmenu(std::shared_ptr<Menu> submenu) {
  // Remove previous submenu listeners if they exist
  if (pimpl_->submenu_ && pimpl_->submenu_opened_listener_id_ != 0) {
    pimpl_->submenu_->RemoveListener(pimpl_->submenu_opened_listener_id_);
    pimpl_->submenu_opened_listener_id_ = 0;
  }
  if (pimpl_->submenu_ && pimpl_->submenu_closed_listener_id_ != 0) {
    pimpl_->submenu_->RemoveListener(pimpl_->submenu_closed_listener_id_);
    pimpl_->submenu_closed_listener_id_ = 0;
  }

  std::shared_ptr<Menu> old = pimpl_->submenu_;
  pimpl_->submenu_ = submenu;

  // Update platform menu if parent_menu_ is set; a null submenu detaches the old one.
  if (pimpl_->parent_menu_) {
    // An item added with a submenu was appended with MF_POPUP and has no command ID of its
    // own, so a lookup by ID misses it: find it by position (its ID or its old submenu).
    HMENU old_submenu = old ? static_cast<HMENU>(old->GetNativeObject()) : nullptr;
    int count = GetMenuItemCount(pimpl_->parent_menu_);
    for (int i = 0; i < count; ++i) {
      MENUITEMINFOW info = {};
      info.cbSize = sizeof(MENUITEMINFOW);
      info.fMask = MIIM_ID | MIIM_SUBMENU;
      if (!GetMenuItemInfoW(pimpl_->parent_menu_, i, TRUE, &info)) continue;
      if (info.wID != pimpl_->id_ && !(old_submenu && info.hSubMenu == old_submenu)) continue;
      // Replacing hSubMenu through SetMenuItemInfo destroys the old submenu, which the Menu
      // object still owns (and may attach again). RemoveMenu keeps it alive: remove the item
      // and insert it again at the same position.
      RemoveMenu(pimpl_->parent_menu_, i, MF_BYPOSITION);
      std::wstring label = StringToWString(pimpl_->label_.value_or(""));
      MENUITEMINFOW mii = {};
      mii.cbSize = sizeof(MENUITEMINFOW);
      mii.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_STRING | MIIM_STATE | MIIM_FTYPE | MIIM_BITMAP;
      mii.fType = MFT_STRING;
      mii.fState = (pimpl_->state_ == MenuItemState::Checked ? MFS_CHECKED : MFS_UNCHECKED) |
                   (pimpl_->enabled_ ? MFS_ENABLED : MFS_GRAYED);
      mii.wID = static_cast<UINT>(pimpl_->id_);
      mii.hSubMenu = submenu ? static_cast<HMENU>(submenu->GetNativeObject()) : nullptr;
      mii.dwTypeData = const_cast<LPWSTR>(label.c_str());
      mii.hbmpItem = pimpl_->menu_bitmap_;
      InsertMenuItemW(pimpl_->parent_menu_, i, TRUE, &mii);
      break;
    }
  }

  // Add event listeners to forward submenu events (independent of parent_menu_)
  if (submenu) {
    MenuItemId menu_item_id = pimpl_->id_;
    MenuItem* self = this;
    pimpl_->submenu_opened_listener_id_ =
        submenu->AddListener<MenuOpenedEvent>([self, menu_item_id](const MenuOpenedEvent& event) {
          self->Emit<MenuItemSubmenuOpenedEvent>(menu_item_id);
        });

    pimpl_->submenu_closed_listener_id_ =
        submenu->AddListener<MenuClosedEvent>([self, menu_item_id](const MenuClosedEvent& event) {
          self->Emit<MenuItemSubmenuClosedEvent>(menu_item_id);
        });
  }
}

std::shared_ptr<Menu> MenuItem::GetSubmenu() const {
  return pimpl_->submenu_;
}

void* MenuItem::GetNativeObjectInternal() const {
  return reinterpret_cast<void*>(static_cast<uintptr_t>(pimpl_->id_));
}

// Menu::Impl implementation
class Menu::Impl {
 public:
#ifdef NATIVEAPI_ENABLE_WINUI3
  MenuBackend backend_ = MenuBackend::WinUI3;
#else
  MenuBackend backend_ = MenuBackend::Native;
#endif
  bool wraps_native_ = false;
  bool opening_ = false;
  // Set from WM_INITMENUPOPUP: the popup really appeared on screen.
  bool shown_ = false;
#ifdef NATIVEAPI_ENABLE_WINUI3
  std::shared_ptr<WinUI3MenuSession> modern_session_;
#endif
  MenuId id_;
  HMENU hmenu_;
  std::vector<std::shared_ptr<MenuItem>> items_;
  int window_proc_handle_id_;
  std::function<void(MenuId)> opened_callback_;
  std::function<void(MenuId)> closed_callback_;

  Impl(MenuId id, HMENU menu) : id_(id), hmenu_(menu), window_proc_handle_id_(-1) {}

  ~Impl() {
    // Unregister window procedure handler
    if (window_proc_handle_id_ != -1) {
      WindowMessageDispatcher::GetInstance().UnregisterHandler(window_proc_handle_id_);
    }

    if (hmenu_) {
      DestroyMenu(hmenu_);
    }
  }

  // Handle window procedure delegate for menu lifecycle events
  std::optional<LRESULT> HandleWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    // Handle menu lifecycle events only
    // Menu item clicks are now handled by individual MenuItem instances
    if (message == WM_INITMENUPOPUP) {
      // wParam contains the HMENU handle of the popup menu being opened
      HMENU popup_menu = reinterpret_cast<HMENU>(wparam);

      if (popup_menu == hmenu_) {
        // This is our menu being opened
        shown_ = true;
        // Emit menu opened event via callback
        if (opened_callback_) {
          opened_callback_(id_);
        }
      } else {
        // Check if this is a submenu of one of our items
        for (const auto& item : items_) {
          auto submenu = item->GetSubmenu();
          if (submenu && submenu->GetNativeObject() == popup_menu) {
            // This is a submenu of one of our items being opened
            // The MenuItem will handle emitting MenuItemSubmenuOpenedEvent
            // through its event listeners
            break;
          }
        }
      }
    } else if (message == WM_UNINITMENUPOPUP) {
      // wParam contains the HMENU handle of the popup menu being closed
      HMENU popup_menu = reinterpret_cast<HMENU>(wparam);

      if (popup_menu == hmenu_) {
        // This is our menu being closed
        // Emit menu closed event via callback
        if (closed_callback_) {
          closed_callback_(id_);
        }
      } else {
        // Check if this is a submenu of one of our items
        for (const auto& item : items_) {
          auto submenu = item->GetSubmenu();
          if (submenu && submenu->GetNativeObject() == popup_menu) {
            // This is a submenu of one of our items being closed
            // The MenuItem will handle emitting MenuItemSubmenuClosedEvent
            // through its event listeners
            break;
          }
        }
      }
    }
    return std::nullopt;  // Let other handlers (including MenuItem handlers) process it
  }
};

Menu::Menu(void* native_menu)
    : pimpl_(
          std::make_unique<Impl>(IdAllocator::Allocate<Menu>(), static_cast<HMENU>(native_menu))) {
  pimpl_->wraps_native_ = true;
  pimpl_->backend_ = MenuBackend::Native;
  // Set callbacks to emit events
  pimpl_->opened_callback_ = [this](MenuId id) { Emit<MenuOpenedEvent>(id); };
  pimpl_->closed_callback_ = [this](MenuId id) { Emit<MenuClosedEvent>(id); };

  // Register window procedure handler for menu commands and events
  HWND host_window = WindowMessageDispatcher::GetInstance().GetHostWindow();
  if (host_window) {
    pimpl_->window_proc_handle_id_ = WindowMessageDispatcher::GetInstance().RegisterHandler(
        host_window, [this](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
          return pimpl_->HandleWindowProc(hwnd, message, wparam, lparam);
        });
  }
}

Menu::Menu() : pimpl_(std::make_unique<Impl>(IdAllocator::Allocate<Menu>(), CreatePopupMenu())) {
  // Set callbacks to emit events
  pimpl_->opened_callback_ = [this](MenuId id) { Emit<MenuOpenedEvent>(id); };
  pimpl_->closed_callback_ = [this](MenuId id) { Emit<MenuClosedEvent>(id); };

  // Register window procedure handler for menu commands and events
  HWND host_window = WindowMessageDispatcher::GetInstance().GetHostWindow();
  if (host_window) {
    pimpl_->window_proc_handle_id_ = WindowMessageDispatcher::GetInstance().RegisterHandler(
        host_window, [this](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
          return pimpl_->HandleWindowProc(hwnd, message, wparam, lparam);
        });
  }
}

Menu::~Menu() {}

MenuId Menu::GetId() const {
  return pimpl_->id_;
}

void Menu::AddItem(std::shared_ptr<MenuItem> item) {
  if (!item)
    return;

  pimpl_->items_.push_back(item);

  UINT flags = MF_STRING;
  if (item->GetType() == MenuItemType::Separator) {
    flags = MF_SEPARATOR;
  } else if (item->GetType() == MenuItemType::Checkbox) {
    flags |= (item->GetState() == MenuItemState::Checked) ? MF_CHECKED
                                                           : MF_UNCHECKED;
  } else if (item->GetType() == MenuItemType::Radio) {
    flags |= (item->GetState() == MenuItemState::Checked) ? MF_CHECKED
                                                           : MF_UNCHECKED;
  }

  if (!item->IsEnabled()) {
    flags |= MF_GRAYED;
  }

  UINT_PTR menu_id = item->GetId();
  HMENU sub_menu = nullptr;
  if (item->GetSubmenu()) {
    sub_menu = static_cast<HMENU>(item->GetSubmenu()->GetNativeObject());
    flags |= MF_POPUP;
    menu_id = reinterpret_cast<UINT_PTR>(sub_menu);
  }

  auto label_opt = item->GetLabel();
  std::string label_str = label_opt.has_value() ? *label_opt : "";
  std::wstring w_label_str = StringToWString(label_str);
  AppendMenuW(pimpl_->hmenu_, flags, menu_id, w_label_str.c_str());

  // Update the item's impl with menu info
  item->pimpl_->parent_menu_ = pimpl_->hmenu_;
}

void Menu::InsertItem(size_t index, std::shared_ptr<MenuItem> item) {
  if (!item)
    return;

  if (index >= pimpl_->items_.size()) {
    AddItem(item);
    return;
  }

  pimpl_->items_.insert(pimpl_->items_.begin() + index, item);

  UINT flags = MF_STRING | MF_BYPOSITION;
  if (item->GetType() == MenuItemType::Separator) {
    flags = MF_SEPARATOR | MF_BYPOSITION;
  } else if (item->GetType() == MenuItemType::Checkbox) {
    flags |= (item->GetState() == MenuItemState::Checked) ? MF_CHECKED
                                                           : MF_UNCHECKED;
  } else if (item->GetType() == MenuItemType::Radio) {
    flags |= (item->GetState() == MenuItemState::Checked) ? MF_CHECKED
                                                           : MF_UNCHECKED;
  }

  if (!item->IsEnabled()) {
    flags |= MF_GRAYED;
  }

  UINT_PTR menu_id = item->GetId();
  if (item->GetSubmenu()) {
    auto sub_menu =
        static_cast<HMENU>(item->GetSubmenu()->GetNativeObject());
    flags |= MF_POPUP;
    menu_id = reinterpret_cast<UINT_PTR>(sub_menu);
  }

  auto label_opt = item->GetLabel();
  std::string label_str = label_opt.has_value() ? *label_opt : "";
  std::wstring w_label_str = StringToWString(label_str);
  InsertMenuW(pimpl_->hmenu_, static_cast<UINT>(index), flags, menu_id,
              w_label_str.c_str());

  item->pimpl_->parent_menu_ = pimpl_->hmenu_;
}

bool Menu::RemoveItem(std::shared_ptr<MenuItem> item) {
  if (!item)
    return false;

  auto it = std::find(pimpl_->items_.begin(), pimpl_->items_.end(), item);
  if (it != pimpl_->items_.end()) {
    RemoveMenu(pimpl_->hmenu_, item->GetId(), MF_BYCOMMAND);
    pimpl_->items_.erase(it);
    return true;
  }
  return false;
}

bool Menu::RemoveItemById(MenuItemId item_id) {
  for (auto it = pimpl_->items_.begin(); it != pimpl_->items_.end(); ++it) {
    if ((*it)->GetId() == item_id) {
      RemoveMenu(pimpl_->hmenu_, item_id, MF_BYCOMMAND);
      pimpl_->items_.erase(it);
      return true;
    }
  }
  return false;
}

bool Menu::RemoveItemAt(size_t index) {
  if (index >= pimpl_->items_.size())
    return false;

  RemoveMenu(pimpl_->hmenu_, static_cast<UINT>(index), MF_BYPOSITION);
  pimpl_->items_.erase(pimpl_->items_.begin() + index);
  return true;
}

void Menu::Clear() {
  while (GetMenuItemCount(pimpl_->hmenu_) > 0) {
    RemoveMenu(pimpl_->hmenu_, 0, MF_BYPOSITION);
  }
  pimpl_->items_.clear();
}

void Menu::AddSeparator() {
  auto separator = std::make_shared<MenuItem>("", MenuItemType::Separator);
  AddItem(separator);
}

void Menu::InsertSeparator(size_t index) {
  auto separator = std::make_shared<MenuItem>("", MenuItemType::Separator);
  InsertItem(index, separator);
}

size_t Menu::GetItemCount() const {
  return pimpl_->items_.size();
}

std::shared_ptr<MenuItem> Menu::GetItemAt(size_t index) const {
  if (index >= pimpl_->items_.size())
    return nullptr;
  return pimpl_->items_[index];
}

std::shared_ptr<MenuItem> Menu::GetItemById(MenuItemId item_id) const {
  for (const auto& item : pimpl_->items_) {
    if (item->GetId() == item_id) {
      return item;
    }
  }
  return nullptr;
}

std::vector<std::shared_ptr<MenuItem>> Menu::GetAllItems() const {
  return pimpl_->items_;
}

bool Menu::Open(const PositioningStrategy& strategy, Placement placement) {
  if (pimpl_->opening_) return false;
  POINT pt = {0, 0};

  // Determine position based on strategy type
  switch (strategy.GetType()) {
    case PositioningStrategy::Type::Absolute:
      pt.x = static_cast<int>(strategy.GetAbsolutePosition().x);
      pt.y = static_cast<int>(strategy.GetAbsolutePosition().y);
      break;

    case PositioningStrategy::Type::CursorPosition: {
      GetCursorPos(&pt);
      break;
    }

    case PositioningStrategy::Type::Relative: {
      Rectangle rect = strategy.GetRelativeRectangle();
      Point offset = strategy.GetRelativeOffset();
      if (strategy.GetRelativeWindow() != nullptr) {
        // rect and offset are in logical pixels (DIP) for Window-relative
        HWND rel_hwnd = static_cast<HWND>(strategy.GetRelativeWindow()->GetNativeObject());
        double scale = GetScaleFactorForWindow(rel_hwnd);
        if (scale <= 0.0)
          scale = 1.0;
        pt.x = static_cast<int>(std::lround((rect.x + offset.x) * scale));
        pt.y = static_cast<int>(std::lround((rect.y + offset.y) * scale));
      } else {
        // For plain rectangles, assume inputs already in screen pixels
        pt.x = static_cast<int>(rect.x + offset.x);
        pt.y = static_cast<int>(rect.y + offset.y);
      }
      break;
    }
  }

  // Get the host window for menus
  HWND host_window = WindowMessageDispatcher::GetInstance().GetHostWindow();
  if (!host_window) {
    return false;
  }

#ifdef NATIVEAPI_ENABLE_WINUI3
  if (pimpl_->backend_ == MenuBackend::WinUI3) {
    auto session = std::make_shared<WinUI3MenuSession>();
    pimpl_->modern_session_ = session;
    pimpl_->opening_ = true;
    const bool result = session->Open(*this, host_window, pt, placement);
    pimpl_->opening_ = false;
    pimpl_->modern_session_.reset();
    return result;
  }
#endif

  // Set the host window as foreground to ensure menu can be displayed
  SetForegroundWindow(host_window);

  // Determine alignment flags based on placement (both axes)
  // Horizontal:  TPM_LEFTALIGN | TPM_CENTERALIGN | TPM_RIGHTALIGN
  // Vertical:    TPM_TOPALIGN  | TPM_VCENTERALIGN | TPM_BOTTOMALIGN
  UINT uFlags = 0;
  switch (placement) {
    case Placement::Top:
      uFlags = TPM_BOTTOMALIGN | TPM_CENTERALIGN;  // above anchor, horizontally centered
      break;
    case Placement::TopStart:
      uFlags = TPM_BOTTOMALIGN | TPM_LEFTALIGN;  // above anchor, align left
      break;
    case Placement::TopEnd:
      uFlags = TPM_BOTTOMALIGN | TPM_RIGHTALIGN;  // above anchor, align right
      break;
    case Placement::Right:
      uFlags = TPM_LEFTALIGN | TPM_VCENTERALIGN;  // right of anchor, vertically centered
      break;
    case Placement::RightStart:
      uFlags = TPM_LEFTALIGN | TPM_TOPALIGN;  // right of anchor, align top
      break;
    case Placement::RightEnd:
      uFlags = TPM_LEFTALIGN | TPM_BOTTOMALIGN;  // right of anchor, align bottom
      break;
    case Placement::Bottom:
      uFlags = TPM_TOPALIGN | TPM_CENTERALIGN;  // below anchor, horizontally centered
      break;
    case Placement::BottomStart:
      uFlags = TPM_TOPALIGN | TPM_LEFTALIGN;  // below anchor, align left
      break;
    case Placement::BottomEnd:
      uFlags = TPM_TOPALIGN | TPM_RIGHTALIGN;  // below anchor, align right
      break;
    case Placement::Left:
      uFlags = TPM_RIGHTALIGN | TPM_VCENTERALIGN;  // left of anchor, vertically centered
      break;
    case Placement::LeftStart:
      uFlags = TPM_RIGHTALIGN | TPM_TOPALIGN;  // left of anchor, align top
      break;
    case Placement::LeftEnd:
      uFlags = TPM_RIGHTALIGN | TPM_BOTTOMALIGN;  // left of anchor, align bottom
      break;
  }

  // Show the context menu using the host window
  // Note: TrackPopupMenu is a blocking call
  // - WM_INITMENUPOPUP is sent when the menu opens (triggers MenuOpenedEvent)
  // - WM_UNINITMENUPOPUP is sent when the menu closes (triggers
  // MenuClosedEvent)
  // TPM_RETURNCMD: return the chosen item's id instead of posting WM_COMMAND.
  // A posted WM_COMMAND is dispatched after Open() has returned, from the
  // host's message loop, where no caller is on the stack; bindings whose
  // listeners must run synchronously inside a call (Dart's
  // NativeCallable.isolateLocal) then abort with "Cannot invoke native
  // callback outside an isolate". Emitting the click here, before Open()
  // returns, keeps every menu event inside the Open() call.
  pimpl_->opening_ = true;
  pimpl_->shown_ = false;
  UINT cmd;
  {
    // Scope the process-wide opt-in to the native modal menu loop and restore
    // the embedding application's policy before dispatching its click callback.
    MenuThemeScope theme(host_window);
    cmd = static_cast<UINT>(TrackPopupMenu(
        pimpl_->hmenu_, uFlags | TPM_RETURNCMD, pt.x, pt.y, 0, host_window, nullptr));
  }
  pimpl_->opening_ = false;
  // With TPM_RETURNCMD, 0 means "dismissed without a pick" or "failed".
  // WM_INITMENUPOPUP tells them apart: a menu that never appeared is a failure.
  // (The last error code cannot: TrackPopupMenu runs a whole modal loop, whose
  // internal calls leave one set on success too.)
  if (cmd == 0 && !pimpl_->shown_) {
    return false;
  }

  if (cmd != 0) {
    // Find the item (searching submenus) and fire its click.
    std::function<bool(const Menu&)> dispatch = [&](const Menu& menu) -> bool {
      for (const auto& item : menu.pimpl_->items_) {
        if (item->GetId() == cmd) {
          if (item->pimpl_->clicked_callback_) {
            item->pimpl_->clicked_callback_(item->pimpl_->id_);
          }
          return true;
        }
        if (auto submenu = item->GetSubmenu(); submenu && dispatch(*submenu)) {
          return true;
        }
      }
      return false;
    };
    dispatch(*this);
  }

  // Shown (and possibly dismissed without a pick): success, as before.
  return true;
}

bool Menu::Close() {
#ifdef NATIVEAPI_ENABLE_WINUI3
  if (pimpl_->modern_session_) return pimpl_->modern_session_->Close();
#endif
  // Send WM_CANCELMODE to close any open menus
  HWND host_window = WindowMessageDispatcher::GetInstance().GetHostWindow();
  if (host_window) {
    // This will close the menu and trigger WM_UNINITMENUPOPUP
    SendMessage(host_window, WM_CANCELMODE, 0, 0);
    return true;
  }
  return false;
}

void* Menu::GetNativeObjectInternal() const {
  return pimpl_->hmenu_;
}

bool Menu::SetBackend(MenuBackend backend) {
  if (!IsBackendSupported(backend) || pimpl_->opening_ ||
      (pimpl_->wraps_native_ && backend != MenuBackend::Native)) return false;
  pimpl_->backend_ = backend;
  return true;
}

MenuBackend Menu::GetBackend() const {
  return pimpl_->backend_;
}

bool Menu::IsBackendSupported(MenuBackend backend) {
  if (backend == MenuBackend::Native) return true;
#ifdef NATIVEAPI_ENABLE_WINUI3
  return backend == MenuBackend::WinUI3;
#else
  return false;
#endif
}

}  // namespace nativeapi
