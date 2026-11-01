#include "window_winui3_windows.h"
#include "winui3_runtime_windows.h"
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>

namespace nativeapi {
namespace W = winrt::Microsoft::UI::Windowing;
namespace {
W::AppWindow GetAppWindow(HWND window) {
  if (!window || !IsWindow(window) || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
    winrt::throw_hresult(E_INVALIDARG);
  InitializeWinUI3();
  return W::AppWindow::GetFromWindowId(winrt::Microsoft::UI::GetWindowIdFromWindow(window));
}
}
bool SetWinUI3Frameless(HWND window) {
  try {
    auto presenter = GetAppWindow(window).Presenter().try_as<W::OverlappedPresenter>();
    if (!presenter) return false;
    presenter.SetBorderAndTitleBar(false, false);
    return true;
  } catch (const winrt::hresult_error&) { return false; }
}
bool SetWinUI3TitleBarStyle(HWND window, TitleBarStyle style) {
  try {
    auto presenter = GetAppWindow(window).Presenter().try_as<W::OverlappedPresenter>();
    if (!presenter || (style != TitleBarStyle::Normal && style != TitleBarStyle::Hidden)) return false;
    presenter.SetBorderAndTitleBar(true, style == TitleBarStyle::Normal);
    return true;
  } catch (const winrt::hresult_error&) { return false; }
}
bool SetWinUI3TitleBarColors(HWND window, const Color& background, const Color& foreground) {
  try {
    auto app = GetAppWindow(window);
    if (!W::AppWindowTitleBar::IsCustomizationSupported()) return false;
    auto title = app.TitleBar();
    winrt::Windows::UI::Color bg{background.a, background.r, background.g, background.b};
    winrt::Windows::UI::Color fg{foreground.a, foreground.r, foreground.g, foreground.b};
    title.BackgroundColor(bg);
    title.ForegroundColor(fg);
    title.ButtonBackgroundColor(bg);
    title.ButtonForegroundColor(fg);
    return true;
  } catch (const winrt::hresult_error&) { return false; }
}
bool ResetWinUI3TitleBarColors(HWND window) {
  try {
    auto app = GetAppWindow(window);
    if (!W::AppWindowTitleBar::IsCustomizationSupported()) return false;
    auto title = app.TitleBar();
    title.BackgroundColor(nullptr);
    title.ForegroundColor(nullptr);
    title.ButtonBackgroundColor(nullptr);
    title.ButtonForegroundColor(nullptr);
    return true;
  } catch (const winrt::hresult_error&) { return false; }
}
}
