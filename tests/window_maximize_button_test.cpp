// SetMaximizeButtonBounds() (#70). Windows: the window answers WM_NCHITTEST
// with HTMAXBUTTON over the app's button (Windows 11 shows snap layouts on
// it), a content child steps aside there, and the non-client mouse messages
// over it reach the content as client messages. Messages are sent to the
// windows directly, so no input; the window is shown without activation at
// the bottom of the Z order, since child hit testing skips hidden windows.
// Elsewhere: unsupported.
#include "nativeapi.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace nativeapi;

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
bool IsEmpty(nativeapi::Rectangle r) {
  return r.x == 0 && r.y == 0 && r.width == 0 && r.height == 0;
}

#ifdef _WIN32
struct Received {
  UINT message;
  WPARAM keys;
  int x, y;
};
std::vector<Received> received;

LRESULT CALLBACK ContentProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
      message == WM_MOUSELEAVE)
    received.push_back({message, wp, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))});
  return DefWindowProcW(hwnd, message, wp, lp);
}

// --serve: a window for a real-pointer check (tools/gui/core_window_snap_layout_test.ps1).
// Prints the button's screen rectangle, then logs what the content receives.
LRESULT CALLBACK LoggingContentProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  const char* name = message == WM_MOUSEMOVE     ? "WM_MOUSEMOVE"
                     : message == WM_MOUSELEAVE  ? "WM_MOUSELEAVE"
                     : message == WM_LBUTTONDOWN ? "WM_LBUTTONDOWN"
                     : message == WM_LBUTTONUP   ? "WM_LBUTTONUP"
                                                 : nullptr;
  // Moves in the title band (where the button is), each new point once.
  const int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
  static UINT last_message = 0;
  static int last_x = -1, last_y = -1;
  const bool repeat = message == last_message && x == last_x && y == last_y;
  if (name && !repeat && (message != WM_MOUSEMOVE || y < 100))
    std::cout << "CONTENT " << name << " " << x << " " << y << std::endl;
  last_message = message;
  last_x = x;
  last_y = y;
  return DefWindowProcW(hwnd, message, wp, lp);
}

int Serve() {
  SetProcessDPIAware();
  Window window;
  window.SetTitle("nativeapi snap layout");
  window.SetBounds({200, 200, 720, 420});
  window.SetTitleBarStyle(TitleBarStyle::Hidden);
  auto root = static_cast<HWND>(window.GetNativeObject());
  WNDCLASSW cls = {};
  cls.lpfnWndProc = LoggingContentProc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH));
  cls.lpszClassName = L"NativeApiSnapLayoutContent";
  RegisterClassW(&cls);
  RECT client;
  GetClientRect(root, &client);
  CreateWindowExW(0, cls.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, client.right,
                  client.bottom, root, nullptr, cls.hInstance, nullptr);
  window.Show();
  const double scale = GetDpiForWindow(root) / 96.0;
  const nativeapi::Rectangle button = {client.right / scale - 146, 0, 46, 32};
  if (!window.SetMaximizeButtonBounds(button)) {
    std::cout << "FAIL SetMaximizeButtonBounds" << std::endl;
    return 1;
  }
  POINT origin = {0, 0};
  ClientToScreen(root, &origin);
  // The button in the content window's own (physical) coordinates, which its
  // log uses.
  std::cout << "BUTTON_CONTENT " << std::lround(button.x * scale) << " "
            << std::lround(button.y * scale) << " " << std::lround(button.width * scale) << " "
            << std::lround(button.height * scale) << std::endl;
  std::cout << "READY " << std::lround(origin.x + button.x * scale) << " "
            << std::lround(origin.y + button.y * scale) << " " << std::lround(button.width * scale)
            << " " << std::lround(button.height * scale) << std::endl;
  const auto end = GetTickCount64() + 60000;
  while (GetTickCount64() < end && IsWindow(root)) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    std::cout << std::flush;
    Sleep(10);
  }
  return 0;
}

LPARAM ScreenPoint(HWND root, double scale, double x, double y) {
  POINT p = {static_cast<LONG>(std::lround(x * scale)), static_cast<LONG>(std::lround(y * scale))};
  ClientToScreen(root, &p);
  return MAKELPARAM(p.x, p.y);
}
#endif
}  // namespace

int main(int argc, char** argv) {
  Application::GetInstance();
#ifdef _WIN32
  if (argc > 1 && std::string(argv[1]) == "--serve") return Serve();
#else
  (void)argc;
  (void)argv;
#endif
#ifndef _WIN32
  Window window;
  Check(!Window::IsMaximizeButtonBoundsSupported(), "unsupported on this platform");
  Check(!window.SetMaximizeButtonBounds({10, 0, 46, 32}), "SetMaximizeButtonBounds returns false");
  Check(IsEmpty(window.GetMaximizeButtonBounds()), "GetMaximizeButtonBounds stays empty");
#else
  SetProcessDPIAware();
  Check(Window::IsMaximizeButtonBoundsSupported(), "supported on Windows");
  Window window;
  window.SetBounds({100, 100, 640, 400});
  window.SetTitleBarStyle(TitleBarStyle::Hidden);
  auto root = static_cast<HWND>(window.GetNativeObject());
  const double scale = GetDpiForWindow(root) / 96.0;

  // A content window covering the client area, as a Flutter view does.
  WNDCLASSW cls = {};
  cls.lpfnWndProc = ContentProc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"NativeApiMaximizeButtonContent";
  RegisterClassW(&cls);
  RECT client;
  GetClientRect(root, &client);
  HWND content = CreateWindowExW(0, cls.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0,
                                 client.right, client.bottom, root, nullptr, cls.hInstance, nullptr);
  ShowWindow(root, SW_SHOWNOACTIVATE);  // Child hit testing skips hidden windows.
  SetWindowPos(root, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

  // Logical content coordinates: a 46 x 32 button 100 px from the right edge.
  const double width = client.right / scale;
  const nativeapi::Rectangle button = {width - 146, 0, 46, 32};
  const LPARAM inside = ScreenPoint(root, scale, button.x + 20, 16);
  const LPARAM outside = ScreenPoint(root, scale, button.x - 40, 16);
  const LPARAM top_edge = ScreenPoint(root, scale, button.x + 20, 1);

  Check(SendMessageW(root, WM_NCHITTEST, 0, inside) != HTMAXBUTTON, "no button before it is set");
  Check(window.SetMaximizeButtonBounds(button), "SetMaximizeButtonBounds accepts the button");
  const nativeapi::Rectangle stored = window.GetMaximizeButtonBounds();
  Check(stored.x == button.x && stored.y == button.y && stored.width == button.width &&
            stored.height == button.height,
        "GetMaximizeButtonBounds returns it");
  Window other(root);
  Check(other.GetMaximizeButtonBounds().width == button.width, "shared by every wrapper");

  Check(SendMessageW(root, WM_NCHITTEST, 0, inside) == HTMAXBUTTON, "HTMAXBUTTON over the button");
  Check(SendMessageW(root, WM_NCHITTEST, 0, outside) != HTMAXBUTTON, "not beside it");
  Check(SendMessageW(root, WM_NCHITTEST, 0, top_edge) == HTTOP, "the resize border wins on top");
  Check(SendMessageW(content, WM_NCHITTEST, 0, inside) == HTTRANSPARENT,
        "the content window steps aside over the button");
  Check(SendMessageW(content, WM_NCHITTEST, 0, outside) == HTCLIENT,
        "the content window keeps the rest");

  // Non-client messages over the button reach the content as client ones.
  POINT expected = {static_cast<short>(LOWORD(inside)), static_cast<short>(HIWORD(inside))};
  ScreenToClient(content, &expected);
  received.clear();
  Check(SendMessageW(root, WM_NCMOUSEMOVE, HTMAXBUTTON, inside) == 0, "hover is handled");
  Check(received.size() == 1 && received[0].message == WM_MOUSEMOVE && received[0].x == expected.x &&
            received[0].y == expected.y,
        "hover reaches the content at the same point");
  received.clear();
  SendMessageW(root, WM_NCLBUTTONDOWN, HTMAXBUTTON, inside);
  Check(received.size() == 1 && received[0].message == WM_LBUTTONDOWN &&
            (received[0].keys & MK_LBUTTON),
        "a press reaches the content as WM_LBUTTONDOWN");
  received.clear();
  SendMessageW(root, WM_NCLBUTTONUP, HTMAXBUTTON, inside);
  Check(received.size() == 1 && received[0].message == WM_LBUTTONUP,
        "the release reaches the content as WM_LBUTTONUP");
  Check(!IsZoomed(root), "the system does not maximize on its own: the app's click does");
  received.clear();
  SendMessageW(root, WM_NCMOUSELEAVE, 0, 0);
  Check(received.size() == 1 && received[0].message == WM_MOUSELEAVE,
        "leaving the button reaches the content as WM_MOUSELEAVE");

  const double nan = std::numeric_limits<double>::quiet_NaN();
  Check(!window.SetMaximizeButtonBounds({nan, 0, 46, 32}), "a rectangle that is not finite is refused");
  Check(window.GetMaximizeButtonBounds().width == button.width, "and leaves the button as it was");
  Check(window.SetMaximizeButtonBounds({0, 0, 0, 0}), "an empty rectangle clears it");
  Check(IsEmpty(window.GetMaximizeButtonBounds()), "GetMaximizeButtonBounds is empty again");
  Check(SendMessageW(root, WM_NCHITTEST, 0, inside) != HTMAXBUTTON, "no HTMAXBUTTON once cleared");
  Check(SendMessageW(content, WM_NCHITTEST, 0, inside) == HTCLIENT,
        "the content window keeps the point again");
  DestroyWindow(root);
#endif
  std::cout << (failures ? "FAILED" : "OK") << std::endl;
  return failures ? 1 : 0;
}
