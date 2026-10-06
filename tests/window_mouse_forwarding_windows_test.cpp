// Real HWND timer/message delivery on a private desktop. The input desktop is
// never switched; GetCursorPos is replaced only in this executable's import table.
#include "nativeapi.h"
#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <string>

namespace {
POINT cursor = {};
int failures = 0;
struct Events { int moves = 0, leaves = 0, buttons = 0; POINT latest = {}; };
BOOL WINAPI TestCursor(POINT* point) { *point = cursor; return TRUE; }
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
LRESULT CALLBACK Record(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                       UINT_PTR, DWORD_PTR data) {
  auto* events = reinterpret_cast<Events*>(data);
  if (message == WM_MOUSEMOVE) {
    ++events->moves;
    events->latest = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
  }
  if (message == WM_MOUSELEAVE) ++events->leaves;
  if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MOUSEWHEEL) ++events->buttons;
  return DefSubclassProc(hwnd, message, wparam, lparam);
}
void Pump() {
  MSG message;
  const auto end = GetTickCount64() + 100;
  while (GetTickCount64() < end) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    Sleep(1);
  }
}
void Move(HWND target, LONG x, LONG y) { cursor = {x, y}; ClientToScreen(target, &cursor); Pump(); }
ULONG_PTR* CursorImport() {
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress) return nullptr;
  auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
  const ULONG_PTR original = reinterpret_cast<ULONG_PTR>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetCursorPos"));
  for (; descriptor->Name; ++descriptor) {
    auto* slot = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
    for (; slot->u1.Function; ++slot)
      if (slot->u1.Function == original) return &slot->u1.Function;
  }
  return nullptr;
}
bool Replace(ULONG_PTR* slot, ULONG_PTR value) {
  DWORD protection = 0;
  if (!slot || !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
  *slot = value;
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(*slot), protection, &ignored);
  return true;
}
}
int main() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const HDESK previous = GetThreadDesktop(GetCurrentThreadId());
  std::wstring name = L"NativeAPI-Mouse-Forward-Test-" + std::to_wstring(GetCurrentProcessId());
  const HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  Check(desktop && SetThreadDesktop(desktop), "private desktop created and attached to test thread");
  if (failures) { if (desktop) CloseDesktop(desktop); return 1; }
  ULONG_PTR* slot = CursorImport();
  const ULONG_PTR original = slot ? *slot : 0;
  Check(Replace(slot, reinterpret_cast<ULONG_PTR>(TestCursor)), "pointer query replaced only inside test executable");
  if (!failures) {
    nativeapi::Window window;
    HWND host = static_cast<HWND>(window.GetNativeObject());
    window.SetBounds({100, 100, 320, 200});
    HWND first = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 30, 100, 100,
                              host, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND nested = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 10, 60, 60,
                               first, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND second = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 150, 30, 100, 100,
                               host, nullptr, GetModuleHandleW(nullptr), nullptr);
    Events a, b;
    Check(first && nested && second &&
          SetWindowSubclass(nested, Record, 1, reinterpret_cast<DWORD_PTR>(&a)) &&
          SetWindowSubclass(second, Record, 1, reinterpret_cast<DWORD_PTR>(&b)), "nested native content windows");
    window.ShowInactive();
    cursor = {0, 0}; Pump();
    Check(window.SetIgnoreMouseEvents(true, true), "enable pass-through and movement forwarding");
    Move(nested, 15, 17);
    Check(a.moves == 1 && b.moves == 0, "actual timer posts movement to deepest child HWND");
    Check(a.latest.x == 15 && a.latest.y == 17, "child-local physical coordinates");
    Pump(); Check(a.moves == 1, "stationary pointer emits no duplicate movement");
    Move(second, 25, 35);
    Check(a.leaves == 1 && b.moves == 1 && b.latest.x == 25 && b.latest.y == 35,
          "target transition posts leave and movement");
    Move(host, -30, -30); Check(b.leaves == 1, "leaving window posts leave");
    Check(a.buttons == 0 && b.buttons == 0, "no button or wheel messages forwarded");
    const auto handle = native_window_create_with_native_window(host);
    Check(native_window_set_ignore_mouse_events(handle, true, false), "disable movement forwarding through another wrapper");
    const int previous_moves = a.moves;
    Move(nested, 25, 25); Check(a.moves == previous_moves && window.IsIgnoreMouseEvents(), "ordinary pass-through has no forwarding");
    Check(native_window_set_ignore_mouse_events(handle, true, true), "reenable movement forwarding");
    native_window_free(handle);
    Move(nested, 35, 35); Check(a.moves > previous_moves, "timer survives wrapper release");
    const int before_destroy = a.moves;
    DestroyWindow(host); Pump();
    Check(!window.IsMouseMoveForwardingEnabled() && a.moves == before_destroy, "native destruction stops timer");
  }
  if (slot) Check(Replace(slot, original), "original pointer query restored");
  // The process owns this desktop throughout; no SwitchDesktop call is made.
  if (SetThreadDesktop(previous)) CloseDesktop(desktop);
  std::cout << (failures ? "FAILED" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
}
