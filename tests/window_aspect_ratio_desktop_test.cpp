// Explicit desktop test for macOS and Windows; creates windows, sends no input.
// SetAspectRatio constrains the content area of user-driven resizes only:
// programmatic sizes are left exactly as requested.
#include "nativeapi.h"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#else
#include <windows.h>
#include <memory>
#include "../src/window_registry.h"
#endif
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Settle() {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
  while (std::chrono::steady_clock::now() < end) {
#ifdef __APPLE__
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
#else
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
  }
}
bool Near(double a, double b, double tolerance = 1.0) {
  return std::abs(a - b) <= tolerance;
}

#ifdef __APPLE__
void CheckNative(nativeapi::Window& window, double ratio, const char* label) {
  auto* native = (__bridge NSWindow*)window.GetNativeObject();
  const bool full_size = native.styleMask & NSWindowStyleMaskFullSizeContentView;
  const NSSize frame = native.aspectRatio;
  const NSSize content = native.contentAspectRatio;
  std::cout << "full_size=" << full_size << " aspectRatio=" << frame.width << ':' << frame.height
            << " contentAspectRatio=" << content.width << ':' << content.height << std::endl;
  auto is = [](NSSize size, double ratio) {
    return size.height > 0 && Near(size.width / size.height, ratio, 1e-6);
  };
  auto unset = [](NSSize size) { return size.width <= 0 || size.height <= 0; };
  bool ok;
  if (ratio <= 0) {
    ok = unset(frame) && unset(content);
  } else if (full_size) {
    // The content view covers the frame: the frame ratio is the content ratio.
    ok = is(frame, ratio) && unset(content);
  } else {
    ok = is(content, ratio) && unset(frame);
  }
  Check(ok, label);
}
#else
// Feeds WM_SIZING the rectangle a drag of `edge` by (dx, dy) would propose and
// returns the client size the handler settles on.
nativeapi::Size Drag(HWND hwnd, WPARAM edge, LONG dx, LONG dy) {
  RECT window_rect = {};
  RECT client_rect = {};
  GetWindowRect(hwnd, &window_rect);
  GetClientRect(hwnd, &client_rect);
  const LONG extra_width =
      (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
  const LONG extra_height =
      (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);
  RECT proposed = window_rect;
  if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
    proposed.left -= dx;
  else
    proposed.right += dx;
  if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
    proposed.top -= dy;
  else
    proposed.bottom += dy;
  const RECT before = proposed;
  SendMessageW(hwnd, WM_SIZING, edge, reinterpret_cast<LPARAM>(&proposed));
  // The edge opposite to the dragged one stays put.
  const bool anchored = (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT
                             ? proposed.right == before.right
                             : proposed.left == before.left) &&
                        (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT
                             ? proposed.bottom == before.bottom
                             : proposed.top == before.top);
  Check(anchored, "opposite edge anchored");
  const double scale = GetDpiForWindow(hwnd) / 96.0;
  return {(proposed.right - proposed.left - extra_width) / scale,
          (proposed.bottom - proposed.top - extra_height) / scale};
}
void CheckRatio(nativeapi::Size content, double ratio, const char* label) {
  std::cout << "content=" << content.width << 'x' << content.height << std::endl;
  // One device pixel of rounding on the derived side.
  Check(Near(content.width, content.height * ratio, 1.5 * ratio), label);
}
#endif

void Run(bool hidden_title_bar) {
  std::cout << "CASE hidden_title_bar=" << hidden_title_bar << std::endl;
  nativeapi::Window window;
  window.SetTitle("nativeapi aspect ratio regression");
  if (hidden_title_bar)
    window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
  window.SetContentSize({600, 400});
  window.Show();
  Settle();

  window.SetAspectRatio(16.0 / 9.0);
  Check(Near(window.GetAspectRatio(), 16.0 / 9.0, 1e-9), "getter");
  auto content = window.GetContentSize();
  Check(Near(content.width, 600) && Near(content.height, 400),
        "setting a ratio keeps the current size");

  // Programmatic sizes are not bent to the ratio.
  window.SetContentSize({700, 300});
  Settle();
  content = window.GetContentSize();
  std::cout << "content=" << content.width << 'x' << content.height << std::endl;
  Check(Near(content.width, 700) && Near(content.height, 300), "SetContentSize is exact");
  window.SetSize({820, 500}, false);
  Settle();
  const auto frame = window.GetSize();
  Check(Near(frame.width, 820) && Near(frame.height, 500), "SetSize is exact");

#ifdef __APPLE__
  CheckNative(window, 16.0 / 9.0, "AppKit ratio on the content");
  // Toggling full-size content must move the ratio to the matching property.
  window.SetTitleBarStyle(hidden_title_bar ? nativeapi::TitleBarStyle::Normal
                                           : nativeapi::TitleBarStyle::Hidden);
  Settle();
  CheckNative(window, 16.0 / 9.0, "ratio follows a title bar style change");
  window.SetTitleBarStyle(hidden_title_bar ? nativeapi::TitleBarStyle::Hidden
                                           : nativeapi::TitleBarStyle::Normal);
  Settle();
  CheckNative(window, 16.0 / 9.0, "ratio follows the style change back");
  {
    nativeapi::Window other(window.GetNativeObject());
    Check(Near(other.GetAspectRatio(), 16.0 / 9.0, 1e-9), "wrappers share the ratio");
  }
  window.SetAspectRatio(0);
  CheckNative(window, 0, "clearing removes both AppKit ratios");
  Check(window.GetAspectRatio() == 0, "getter cleared");
#else
  auto hwnd = static_cast<HWND>(window.GetNativeObject());
  // Another wrapper in the registry must not hide this wrapper's ratio.
  auto other = std::make_shared<nativeapi::Window>(hwnd);
  nativeapi::WindowRegistry::GetInstance().Add(window.GetId(), other);
  CheckRatio(Drag(hwnd, WMSZ_RIGHT, 200, 0), 16.0 / 9.0, "right edge: content ratio");
  CheckRatio(Drag(hwnd, WMSZ_BOTTOM, 0, 150), 16.0 / 9.0, "bottom edge: content ratio");
  CheckRatio(Drag(hwnd, WMSZ_TOPLEFT, 120, 40), 16.0 / 9.0, "top-left corner: content ratio");
  // Limits win over the ratio, and the ratio is kept where they allow it.
  window.SetMaximumSize({900, 0});
  auto limited = Drag(hwnd, WMSZ_BOTTOM, 0, 600);
  const auto frame_now = window.GetSize();
  std::cout << "frame=" << frame_now.width << 'x' << frame_now.height << std::endl;
  CheckRatio(limited, 16.0 / 9.0, "maximum width keeps the ratio");
  window.SetMaximumSize({-1, -1});
  window.SetAspectRatio(0);
  RECT window_rect = {};
  GetWindowRect(hwnd, &window_rect);
  RECT proposed = window_rect;
  proposed.right += 50;
  const RECT before = proposed;
  SendMessageW(hwnd, WM_SIZING, WMSZ_RIGHT, reinterpret_cast<LPARAM>(&proposed));
  Check(EqualRect(&proposed, &before), "cleared ratio leaves the drag alone");
  nativeapi::WindowRegistry::GetInstance().Remove(window.GetId());
#endif

  window.Hide();
#ifdef __APPLE__
  auto* native = (__bridge NSWindow*)window.GetNativeObject();
  [native setReleasedWhenClosed:NO];
  [native close];
#else
  DestroyWindow(hwnd);
#endif
  Settle();
}
}  // namespace

int main() {
#ifdef __APPLE__
  @autoreleasepool {
#else
  SetProcessDPIAware();
#endif
    nativeapi::Application::GetInstance();
    Run(false);
    Run(true);
#ifdef __APPLE__
  }
#endif
  return failures ? 1 : 0;
}
