// Actual native popups. Default mode checks states and cancels using a timer.
// --clicks emits owned menu-item rectangles for the guarded GUI harness.
#include <windows.h>
#include <commctrl.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include "nativeapi.h"

namespace {
int failures = 0;
constexpr UINT kCustom = 0x1234;
UINT last_command = 0;
void Check(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  failures += !ok;
}
void Settle(int milliseconds = 180) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < end) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
LRESULT CALLBACK RecordCommand(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
  if (message == WM_SYSCOMMAND) {
    last_command = static_cast<UINT>(wp);
    if (last_command == kCustom) return 0;
  }
  return DefSubclassProc(hwnd, message, wp, lp);
}
struct Round {
  HWND hwnd = nullptr;
  HMENU menu = nullptr;
  bool restore = false, move = true, size = true, minimize = true, maximize = true, close = true;
  UINT click = 0;
  bool inspected = false;
  unsigned ticks = 0;
  POINT expected = {};
  bool check_position = false;
} round;
bool Enabled(UINT command) {
  const UINT state = GetMenuState(round.menu, command, MF_BYCOMMAND);
  return state != static_cast<UINT>(-1) && !(state & (MF_DISABLED | MF_GRAYED));
}
void CALLBACK InspectMenu(HWND, UINT, UINT_PTR, DWORD) {
  if (++round.ticks > 600) { Check(false, "menu timeout"); EndMenu(); return; }
  if (round.inspected) return;
  RECT first = {};
  if (!GetMenuItemRect(round.hwnd, round.menu, 0, &first)) return;
  round.inspected = true;
  Check(Enabled(SC_RESTORE) == round.restore, "restore state");
  Check(Enabled(SC_MOVE) == round.move, "move state");
  Check(Enabled(SC_SIZE) == round.size, "size state");
  Check(Enabled(SC_MINIMIZE) == round.minimize, "minimize state");
  Check(Enabled(SC_MAXIMIZE) == round.maximize, "maximize state");
  Check(Enabled(SC_CLOSE) == round.close && Enabled(kCustom), "host close state and custom item preserved");
  if (round.check_position) {
    // Native menu content has a small border inset, and Windows may add shadows.
    Check(std::abs(first.left - round.expected.x) <= 8 &&
          std::abs(first.top - round.expected.y) <= 8, "content-relative logical position scales once");
  }
  if (!round.click) {
    EndMenu();
    // A host callback can change LastError independently of popup success.
    SetLastError(ERROR_ACCESS_DENIED);
    return;
  }
  const int count = GetMenuItemCount(round.menu);
  for (int i = 0; i < count; ++i) {
    if (GetMenuItemID(round.menu, i) == round.click) {
      RECT item = {};
      if (GetMenuItemRect(round.hwnd, round.menu, i, &item)) {
        std::cout << "CLICK " << round.click << ' ' << (item.left + item.right) / 2 << ' '
                  << (item.top + item.bottom) / 2 << std::endl;
        return;
      }
    }
  }
  Check(false, "click item rectangle missing");
  EndMenu();
}
void Popup(nativeapi::Window& window, UINT click = 0, bool capi = false, bool position = false) {
  round.hwnd = static_cast<HWND>(window.GetNativeObject());
  round.menu = GetSystemMenu(round.hwnd, FALSE);
  round.restore = window.IsMinimized() || window.IsMaximized() || window.IsFullScreen();
  round.move = !window.IsMaximized() && !window.IsFullScreen();
  round.size = !round.restore && window.IsResizable();
  round.minimize = !window.IsMinimized() && !window.IsFullScreen() && window.IsMinimizable();
  round.maximize = !window.IsMaximized() && !window.IsFullScreen() && window.IsMaximizable();
  round.close = Enabled(SC_CLOSE);
  round.click = click;
  round.inspected = false;
  round.ticks = 0;
  round.check_position = position;
  round.expected = {80, 50};
  using GetDpi = UINT(WINAPI*)(HWND);
  auto dpi = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  const double scale = dpi ? dpi(round.hwnd) / 96.0 : 1.0;
  round.expected = {static_cast<LONG>(std::lround(80 * scale)), static_cast<LONG>(std::lround(50 * scale))};
  ClientToScreen(round.hwnd, &round.expected);
  SetTimer(round.hwnd, 999, 50, InspectMenu);
  bool shown;
  if (capi) {
    const auto handle = native_window_create_with_native_window(round.hwnd);
    shown = native_window_show_system_menu(handle, {80, 50});
    native_window_free(handle);
  } else shown = window.ShowSystemMenu({80, 50});
  if (IsWindow(round.hwnd)) KillTimer(round.hwnd, 999);
  Check(shown && round.inspected, "native popup accepted, including cancellation");
  Settle();
}
}  // namespace

int main(int argc, char** argv) {
  using namespace nativeapi;
  using SetDpiContext = BOOL(WINAPI*)(HANDLE);
  auto dpi = reinterpret_cast<SetDpiContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
  if (dpi) dpi(reinterpret_cast<HANDLE>(-4));
  Application::GetInstance();
  Check(Window::IsSystemMenuSupported() && native_window_is_system_menu_supported(), "Windows capability");
  Window window;
  HWND hwnd = static_cast<HWND>(window.GetNativeObject());
  SetWindowSubclass(hwnd, RecordCommand, 991, 0);
  Check(!window.ShowSystemMenu({10, 20}), "hidden window rejects menu");
  Check(!native_window_show_system_menu(0, {10, 20}), "invalid C handle rejects menu");
  window.SetTitleBarStyle(TitleBarStyle::Hidden);
  RECT work;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  SetWindowPos(hwnd, HWND_TOPMOST, (work.left + work.right - 600) / 2,
               (work.top + work.bottom - 400) / 2, 600, 400, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  Settle();
  Check(!window.ShowSystemMenu({std::numeric_limits<double>::quiet_NaN(), 0}) &&
        !window.ShowSystemMenu({1e100, 0}), "invalid coordinates rejected");
  HMENU menu = GetSystemMenu(hwnd, FALSE);
  AppendMenuW(menu, MF_STRING, kCustom, L"nativeapi custom item");
  Popup(window, 0, false, true);
  window.SetResizable(false);
  window.SetMinimizable(false);
  Popup(window, 0, true);
  window.SetResizable(true);
  window.SetMinimizable(true);
  EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
  Popup(window);
  EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
  window.Maximize(); Settle(); Popup(window);
  window.Unmaximize(); Settle();
  window.SetFullScreen(true); Settle(); Popup(window);
  window.SetFullScreen(false); Settle();
  window.SetClosable(false);
  Check(!window.ShowSystemMenu({80, 50}), "window without system menu rejects popup");
  window.SetClosable(true);
  menu = GetSystemMenu(hwnd, FALSE);
  if (GetMenuState(menu, kCustom, MF_BYCOMMAND) == static_cast<UINT>(-1))
    AppendMenuW(menu, MF_STRING, kCustom, L"nativeapi custom item");
  if (argc > 1 && std::string(argv[1]) == "--clicks") {
    const UINT commands[] = {kCustom, SC_MAXIMIZE, SC_RESTORE, SC_MINIMIZE, SC_RESTORE, SC_CLOSE};
    for (UINT command : commands) {
      last_command = 0;
      Popup(window, command);
      Check(last_command == command, "selection delivered as WM_SYSCOMMAND");
      if (command == SC_MAXIMIZE) Check(IsZoomed(hwnd), "selected maximize executed");
      if (command == SC_MINIMIZE) Check(IsIconic(hwnd), "selected minimize executed");
      if (command == SC_RESTORE) Check(!IsZoomed(hwnd) && !IsIconic(hwnd), "selected restore executed");
    }
    Check(!IsWindow(hwnd), "selected close executed");
  } else DestroyWindow(hwnd);
  Check(!window.ShowSystemMenu({80, 50}), "destroyed window rejects menu");
  std::cout << (failures ? "FAILURES" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
}
