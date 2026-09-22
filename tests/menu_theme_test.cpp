// Explicit desktop integration test for issue #27. Temporarily changes the app
// color preference (restored on exit), reuses one menu across system and explicit
// light/dark modes, and samples its rendered background. No synthetic input is sent.
#include <windows.h>
#include <iostream>
#include <memory>
#include "nativeapi.h"

using namespace nativeapi;

namespace {
int failures = 0;
Menu* menu_under_test = nullptr;
bool expect_dark = false;

void Check(bool ok, const char* message) {
  std::cout << (ok ? "PASS " : "FAIL ") << message << std::endl;
  if (!ok)
    ++failures;
}

struct Preference {
  HKEY key = nullptr;
  DWORD previous = 1;
  bool existed = false;
  bool valid = false;
  Preference() {
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
      return;
    DWORD size = sizeof(previous), type = 0;
    const auto status = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                         reinterpret_cast<BYTE*>(&previous), &size);
    existed = status == ERROR_SUCCESS;
    valid = status == ERROR_FILE_NOT_FOUND ||
            (existed && type == REG_DWORD && size == sizeof(previous));
  }
  bool Set(DWORD light) {
    return valid &&
           RegSetValueExW(key, L"AppsUseLightTheme", 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&light), sizeof(light)) == ERROR_SUCCESS;
  }
  ~Preference() {
    if (valid) {
      if (existed)
        Set(previous);
      else
        RegDeleteValueW(key, L"AppsUseLightTheme");
    }
    if (key)
      RegCloseKey(key);
  }
};

BOOL CALLBACK FindMenu(HWND hwnd, LPARAM data) {
  wchar_t name[32] = {};
  GetClassNameW(hwnd, name, 32);
  if (wcscmp(name, L"#32768") == 0 && IsWindowVisible(hwnd)) {
    *reinterpret_cast<HWND*>(data) = hwnd;
    return FALSE;
  }
  return TRUE;
}

void CALLBACK InspectAndClose(HWND, UINT, UINT_PTR timer, DWORD) {
  KillTimer(nullptr, timer);
  HWND popup = nullptr;
  EnumThreadWindows(GetCurrentThreadId(), FindMenu, reinterpret_cast<LPARAM>(&popup));
  Check(popup != nullptr, "native popup is visible on the test thread");
  if (popup) {
    RECT item = {};
    auto native_menu = static_cast<HMENU>(menu_under_test->GetNativeObject());
    if (GetMenuItemRect(nullptr, native_menu, 0, &item)) {
      // Blank trailing space in the first normal item, away from text/checkmark.
      POINT point{item.right - 12, (item.top + item.bottom) / 2};
      ScreenToClient(popup, &point);
      HDC dc = GetDC(popup);
      const COLORREF color = GetPixel(dc, point.x, point.y);
      ReleaseDC(popup, dc);
      std::cout << "background RGB " << static_cast<int>(GetRValue(color)) << ' '
                << static_cast<int>(GetGValue(color)) << ' ' << static_cast<int>(GetBValue(color))
                << std::endl;
      Check(color != CLR_INVALID, "menu background could be sampled");
      const bool dark = GetRValue(color) < 128 && GetGValue(color) < 128 && GetBValue(color) < 128;
      Check(dark == expect_dark, "rendered background follows current app theme");
    } else {
      Check(false, "menu item geometry is available");
    }
  }
  menu_under_test->Close();
}
}  // namespace

int main(int argc, char**) {
  if (argc <= 1) {
    std::cout << "Run with --desktop in an interactive Windows session." << std::endl;
    return 0;
  }
  using GetVersion = void(WINAPI*)(DWORD*, DWORD*, DWORD*);
  const auto get_version = reinterpret_cast<GetVersion>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
  DWORD major = 0, minor = 0, build = 0;
  if (get_version)
    get_version(&major, &minor, &build);
  if (major != 10 || minor != 0 || (build & 0x0fffffff) < 18362) {
    std::cout << "SKIP: native dark menus require Windows 10 1903 or later." << std::endl;
    return 0;
  }
  HIGHCONTRASTW contrast = {sizeof(contrast)};
  if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) ||
      (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
    std::cout << "SKIP: light/dark test requires high contrast to be off." << std::endl;
    return 0;
  }
  Preference preference;
  if (!preference.valid)
    return 1;
  Application::GetInstance();
  // A library must restore the embedding application's process-wide policy.
  enum class AppMode { Default, AllowDark, ForceDark, ForceLight };
  using SetMode = AppMode(WINAPI*)(AppMode);
  HMODULE theme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto set_mode =
      theme ? reinterpret_cast<SetMode>(GetProcAddress(theme, MAKEINTRESOURCEA(135))) : nullptr;
  if (!set_mode) {
    if (theme)
      FreeLibrary(theme);
    return 1;
  }
  struct RestoreMode {
    HMODULE module;
    SetMode set;
    AppMode previous;
    ~RestoreMode() {
      set(previous);
      FreeLibrary(module);
    }
  } restore_mode{theme, set_mode, set_mode(AppMode::ForceLight)};
  auto menu = std::make_shared<Menu>();
  menu->SetBackend(MenuBackend::Native);
  menu->AddItem(std::make_shared<MenuItem>("Theme regression test"));
  auto disabled = std::make_shared<MenuItem>("Disabled item");
  disabled->SetEnabled(false);
  menu->AddItem(disabled);
  menu->AddSeparator();
  auto checked = std::make_shared<MenuItem>("Checked item", MenuItemType::Checkbox);
  checked->SetState(MenuItemState::Checked);
  menu->AddItem(checked);
  int opened = 0, closed = 0;
  menu->AddListener<MenuOpenedEvent>([&](const auto&) { ++opened; });
  menu->AddListener<MenuClosedEvent>([&](const auto&) { ++closed; });
  menu_under_test = menu.get();
  struct ThemeCase {
    DWORD system_light;
    Brightness brightness;
    bool dark;
  };
  const ThemeCase cases[] = {
      {1, Brightness::System, false}, {0, Brightness::System, true}, {1, Brightness::System, false},
      {0, Brightness::Light, false},  {1, Brightness::Dark, true},   {1, Brightness::System, false},
  };
  for (const auto& theme_case : cases) {
    const DWORD light = theme_case.system_light;
    if (!preference.Set(light))
      return 1;
    Check(Application::GetInstance().SetBrightness(theme_case.brightness),
          "application brightness accepted");
    expect_dark = theme_case.dark;
    std::cout << (expect_dark ? "DARK" : "LIGHT") << std::endl;
    const auto timer = SetTimer(nullptr, 0, 800, InspectAndClose);
    Check(timer != 0, "close timer created");
    if (!timer)
      return 1;
    Check(menu->Open(PositioningStrategy::Absolute({300, 300})), "menu opens and closes");
    Check(set_mode(AppMode::ForceLight) == AppMode::ForceLight,
          "embedding application's theme policy is restored");
    KillTimer(nullptr, timer);
  }
  Check(opened == 6 && closed == 6, "one opened/closed event per invocation");
  return failures ? 1 : 0;
}
