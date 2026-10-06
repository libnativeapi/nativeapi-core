// Real desktop regression for focus policy, shared wrappers and mouse activation.
// With --clicks, a guarded GUI harness clicks the palette in every blocked mode.
#include "nativeapi.h"
#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#else
#include <gtk/gtk.h>
#endif
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
int clicks = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Pump(int milliseconds = 250) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  do {
#ifdef _WIN32
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
#else
    while (gtk_events_pending()) gtk_main_iteration();
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < end);
}
#ifdef _WIN32
LRESULT CALLBACK CountClick(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                           UINT_PTR id, DWORD_PTR) {
  if (message == WM_LBUTTONUP) ++clicks;
  if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, CountClick, id);
  return DefSubclassProc(hwnd, message, wp, lp);
}
#endif
void Destroy(nativeapi::Window& window) {
#ifdef _WIN32
  DestroyWindow(static_cast<HWND>(window.GetNativeObject()));
#else
  gtk_widget_destroy(GTK_WIDGET(window.GetNativeObject()));
#endif
}
}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetProcessDPIAware();
#else
  if (!gtk_init_check(&argc, &argv)) return 77;
#endif
  nativeapi::Application::GetInstance();
  const bool foreign = argc > 1 && std::strcmp(argv[1], "--foreign-clicks") == 0;
  const bool interactive = foreign || (argc > 1 && std::strcmp(argv[1], "--clicks") == 0);
#ifdef _WIN32
  if (argc > 1 && std::strcmp(argv[1], "--anchor") == 0) {
    nativeapi::Window anchor;
    anchor.SetTitle("nativeapi foreign focus anchor");
    anchor.SetBounds({80, 100, 500, 350});
    anchor.Show();
    std::cout << "FOREIGN_READY" << std::endl;
    Pump(60000);
    Destroy(anchor);
    return 0;
  }
#endif
  nativeapi::Window main_window;
  nativeapi::Window palette;
  main_window.SetTitle("nativeapi focus anchor");
  palette.SetTitle("nativeapi focus palette");
  main_window.SetBounds({80, 100, 500, 350});
  palette.SetBounds({650, 100, 350, 250});
  // A utility palette is placed above unrelated apps without activation.
  // Otherwise a correctly non-activating Show() may remain covered.
  if (interactive) palette.SetAlwaysOnTop(true);
#ifdef _WIN32
  const auto hwnd = static_cast<HWND>(palette.GetNativeObject());
  HWND button = CreateWindowW(L"BUTTON", L"Click without activating", WS_CHILD | WS_VISIBLE,
                              20, 20, 250, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
  SetWindowSubclass(button, CountClick, 1, 0);
#else
  auto* widget = GTK_WIDGET(palette.GetNativeObject());
  auto* button = gtk_button_new_with_label("Click without activating");
  gtk_container_add(GTK_CONTAINER(widget), button);
  gtk_widget_show(button);
  g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) { ++clicks; }), nullptr);
#endif
  nativeapi::Window other(palette.GetNativeObject());
  std::function<void()> focus_anchor = [&] { main_window.Focus(); };
  std::function<bool()> anchor_focused = [&] { return main_window.IsFocused(); };
#ifdef _WIN32
  if (foreign) {
    HWND anchor = FindWindowW(nullptr, L"nativeapi foreign focus anchor");
    if (!anchor) { std::cerr << "Missing foreign anchor" << std::endl; return 1; }
    focus_anchor = [anchor] { SetForegroundWindow(anchor); };
    anchor_focused = [anchor] { return GetForegroundWindow() == anchor; };
  }
#endif
  Check(palette.IsFocusable() && !palette.IsNonActivating(), "default focus policy");
  if (!foreign) main_window.Show();
  Pump();
  if (interactive && !anchor_focused()) {
    // The OS may deny background startup activation. The guarded harness
    // activates this window through an owned title-bar click before proceeding.
    std::cout << "WAIT_ACTIVATION" << std::endl;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!anchor_focused() && std::chrono::steady_clock::now() < deadline) Pump(10);
  }
  if (!anchor_focused()) {
    std::cout << "SKIP desktop did not grant initial activation" << std::endl;
    Destroy(palette);
    Destroy(main_window);
    return 77;
  }
  for (int mode = 0; mode < 4; ++mode) {
    std::cout << "CASE " << mode << std::endl;
    palette.Hide();
    palette.SetNonActivating(false);
    palette.SetFocusable(true);
    if (mode == 0) palette.SetFocusable(false);
    if (mode == 1) palette.SetNonActivating(true);
    if (mode == 2) {
      palette.SetFocusable(false);
      palette.SetNonActivating(true);
      palette.SetNonActivating(false);
    }
    if (mode == 3) {
      palette.SetNonActivating(true);
      palette.SetFocusable(false);
      palette.SetFocusable(true);
    }
    Check(!palette.IsFocusable() && !other.IsFocusable(), "shared effective focus policy");
    Check(other.IsNonActivating() == (mode == 1 || mode == 3), "shared non-activation policy");
#ifdef _WIN32
    Check((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0,
          "native no-activate style");
    Check(IsWindowEnabled(hwnd), "mouse input remains enabled");
    Check(SendMessageW(hwnd, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(hwnd),
                       MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE,
          "mouse activation declined without swallowing click");
    Check(SendMessageW(button, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(hwnd),
                       MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE,
          "child mouse activation declined");
#else
    Check(!gtk_window_get_accept_focus(GTK_WINDOW(widget)) &&
              !gtk_window_get_focus_on_map(GTK_WINDOW(widget)), "native focus hints");
#endif
    focus_anchor();
    Pump();
    palette.Show();
    Pump();
    Check(palette.IsVisible(), "blocked palette still shows");
    Check(anchor_focused() && !palette.IsFocused(), "Show preserves anchor focus");
    palette.Focus();
    Pump();
    Check(anchor_focused() && !palette.IsFocused(), "Focus respects disabled policy");
#ifdef _WIN32
    if (mode == 0 && !foreign) {
      HWND late = CreateWindowW(L"BUTTON", L"Late child", WS_CHILD | WS_VISIBLE,
                                 20, 140, 200, 30, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
      SetFocus(late);
      Pump();
      Check(anchor_focused() && !palette.IsFocused(), "new child cannot focus after policy was set");
      SetFocus(hwnd);
      Pump();
      Check(anchor_focused() && !palette.IsFocused(), "native toplevel focus request is rejected");
      DestroyWindow(late);
    }
#endif
    if (mode == 0) {
      palette.SetAlwaysOnTop(true);
      Pump();
      Check(anchor_focused() && !palette.IsFocused(), "topmost change preserves focus");
      palette.Minimize();
      Pump();
      focus_anchor();
      Pump();
      palette.Show();
      Pump();
      Check(palette.IsVisible() && !palette.IsMinimized(), "Show restores a blocked minimized window");
      Check(anchor_focused() && !palette.IsFocused(), "restoring preserves anchor focus");
      if (!interactive) palette.SetAlwaysOnTop(false);
    }
    if (interactive) {
      const int before = clicks;
      std::cout << "READY " << mode << std::endl;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
      while (clicks == before && std::chrono::steady_clock::now() < deadline) Pump(10);
      Pump();
      Check(clicks == before + 1, "blocked palette receives real button click");
      Check(anchor_focused() && !palette.IsFocused(), "real click preserves anchor focus");
    }
  }
  other.SetNonActivating(false);
  Check(palette.IsFocusable(), "non-activating off restores last requested focusability");
  other.SetFocusable(false);
  palette.SetNonActivating(true);
  other.SetNonActivating(false);
  Check(!palette.IsFocusable(), "non-activating off preserves separately disabled focusability");
  palette.SetFocusable(true);
  Check(other.IsFocusable(), "focusability restored across wrappers");
  palette.Focus();
  Pump();
  Check(palette.IsFocused(), "restored palette can focus again");
  palette.SetFocusable(false);
  Pump();
  Check(!palette.IsFocused(), "disabling focus releases existing keyboard focus");
  palette.SetFocusable(true);
  palette.Focus();
  Pump();
  Check(palette.IsFocused(), "keyboard focus can be reacquired after re-enabling");
  // State survives destroying a wrapper of a host-owned window.
  {
    nativeapi::Window temporary(palette.GetNativeObject());
    temporary.SetNonActivating(true);
  }
  Check(other.IsNonActivating() && !other.IsFocusable(), "policy survives wrapper destruction");
  Pump();
  Check(!other.IsFocused(), "non-activating policy releases existing keyboard focus");
  Destroy(palette);
  Destroy(main_window);
  std::cout << (failures ? "FAILED" : "ALL PASS") << std::endl;
  return failures ? 1 : 0;
}
