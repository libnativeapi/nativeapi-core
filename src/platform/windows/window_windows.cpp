#ifdef NATIVEAPI_ENABLE_WINUI3
#include "window_winui3_windows.h"
#endif
#include <dwmapi.h>
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <iostream>
#include <optional>
#include <unordered_map>
#include "../../foundation/id_allocator.h"
#include "../../window.h"
#include "../../window_shape.h"
#include "../../window_manager.h"
#include "../../window_registry.h"
#include "dpi_utils_windows.h"
#include "string_utils_windows.h"
#include "window_message_dispatcher.h"
#include "window_shape_shadow_windows.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")

namespace nativeapi {

// Property name for storing window ID in HWND
static const wchar_t* kWindowIdProperty = L"NativeAPIWindowId";
// Set while the title bar is hidden. Kept on the HWND, like the style itself, so every
// wrapper of the window agrees and the frame handling below outlives any one wrapper.
static const wchar_t* kTitleBarHiddenProperty = L"NativeAPITitleBarHidden";
// The other two window flags the system cannot be asked about afterwards, kept on the
// HWND for the same reason.
static const wchar_t* kNoShadowProperty = L"NativeAPINoShadow";
static const wchar_t* kHiddenFromTaskbarProperty = L"NativeAPIHiddenFromTaskbar";
// The translucent background color of the window, as 0x1AARRGGBB (the leading 1 tells
// a transparent black from "no property"). Set while the window is see-through.
static const wchar_t* kTranslucentBackgroundProperty = L"NativeAPITranslucentBackground";
// The visual effect in force, as the VisualEffect value (None is "no property"). On the
// HWND like the flags above, and because WindowProc has to see it.
static const wchar_t* kVisualEffectProperty = L"NativeAPIVisualEffect";
// The opaque background color of a window of the library's own class, as 0x1RRGGBB.
// WindowProc paints it: the class brush would be every such window's background at once.
static const wchar_t* kBackgroundColorProperty = L"NativeAPIBackgroundColor";

// SetWindowCompositionAttribute is how the shell itself makes windows see-through. It is
// exported by user32 but not declared in the SDK.
namespace {

enum AccentState {
  kAccentDisabled = 0,
  kAccentEnableGradient = 1,
  kAccentEnableTransparentGradient = 2,
  kAccentEnableBlurBehind = 3,
  kAccentEnableAcrylicBlurBehind = 4,
};

struct AccentPolicy {
  int accent_state;
  int accent_flags;
  DWORD gradient_color;  // 0xAABBGGRR
  int animation_id;
};

struct WindowCompositionAttributeData {
  int attribute;
  PVOID data;
  SIZE_T size;
};

constexpr int kWcaAccentPolicy = 19;

// accent_flags 2 has the gradient color drawn, which is all a see-through background is.
// The blur-behind accents come out nearly black with it and want 0.
bool SetAccentPolicy(HWND hwnd, AccentState state, DWORD gradient_color, int accent_flags = 2) {
  using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);
  static const auto set_attribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
  if (!set_attribute) {
    return false;
  }
  AccentPolicy policy = {state, accent_flags, gradient_color, 0};
  WindowCompositionAttributeData data = {kWcaAccentPolicy, &policy, sizeof(policy)};
  return set_attribute(hwnd, &data) != FALSE;
}

// The accent a see-through background color asks for. A visual effect takes the accent
// over while it is active, and hands it back through here.
void ApplyBackgroundAccent(HWND hwnd) {
  const auto stored = reinterpret_cast<uintptr_t>(GetPropW(hwnd, kTranslucentBackgroundProperty));
  if (!stored) {
    SetAccentPolicy(hwnd, kAccentDisabled, 0);
    return;
  }
  // 0x1AARRGGBB to 0xAABBGGRR
  const DWORD gradient = static_cast<DWORD>((stored & 0xFF000000) | ((stored & 0xFF) << 16) |
                                            (stored & 0xFF00) | ((stored >> 16) & 0xFF));
  SetAccentPolicy(hwnd, kAccentEnableTransparentGradient, gradient);
}

// DWMWA_SYSTEMBACKDROP_TYPE and DWM_SYSTEMBACKDROP_TYPE, which SDKs before 10.0.22621
// do not declare.
constexpr DWORD kDwmwaSystemBackdropType = 38;
enum SystemBackdrop : int {
  kBackdropNone = 1,
  kBackdropMica = 2,
  kBackdropAcrylic = 3,
  kBackdropMicaAlt = 4,
};

constexpr DWORD kBuildAcrylicAccent = 17134;   // Windows 10 1803
constexpr DWORD kBuildSystemBackdrop = 22621;  // Windows 11 22H2

// GetVersionExW lies to manifest-less processes; RtlGetVersion reports the real build.
DWORD WindowsBuildNumber() {
  static const DWORD build = [] {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto rtl_get_version =
        ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    RTL_OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    if (!rtl_get_version || rtl_get_version(&info) != 0) {
      return DWORD{0};
    }
    return info.dwBuildNumber;
  }();
  return build;
}

// How an effect is drawn on the version that is running: as one of the compositor's
// system backdrops, as an accent, or not at all (both zero).
struct VisualEffectRecipe {
  SystemBackdrop backdrop = kBackdropNone;
  AccentState accent = kAccentDisabled;
  bool IsAvailable() const { return backdrop != kBackdropNone || accent != kAccentDisabled; }
};

VisualEffectRecipe RecipeFor(VisualEffect effect) {
  const DWORD build = WindowsBuildNumber();
  VisualEffectRecipe recipe;
  switch (effect) {
    case VisualEffect::None:
      break;
    case VisualEffect::Blur:
      // The accent blur is drawn behind the client area only, and the caption DWM
      // draws over it stays opaque - a transparent caption color does not help, it
      // shows white. A system backdrop covers the whole window, caption included, so
      // it is the better match wherever there is one, even though the nearest kind is
      // the acrylic one.
      if (build >= kBuildSystemBackdrop) {
        recipe.backdrop = kBackdropAcrylic;
      } else {
        recipe.accent = kAccentEnableBlurBehind;
      }
      break;
    case VisualEffect::Acrylic:
    case VisualEffect::Hud:
    case VisualEffect::Popover:
    case VisualEffect::Menu:
      if (build >= kBuildSystemBackdrop) {
        recipe.backdrop = kBackdropAcrylic;
      } else if (build >= kBuildAcrylicAccent) {
        recipe.accent = kAccentEnableAcrylicBlurBehind;
      }
      break;
    case VisualEffect::Mica:
      if (build >= kBuildSystemBackdrop) recipe.backdrop = kBackdropMica;
      break;
    case VisualEffect::MicaAlt:
      if (build >= kBuildSystemBackdrop) recipe.backdrop = kBackdropMicaAlt;
      break;
  }
  return recipe;
}

// -1 on every side turns the whole client area into the compositor's frame. A system
// backdrop is drawn on that frame and nowhere else. A blur-behind accent is the other way
// round: it is drawn behind the client area, and the frame, where there is any, covers it.
// A see-through background color is an accent too, but only ever shown through a child
// that covers the client area (a Flutter view), and that has been found to need the frame.
void UpdateFrameExtent(HWND hwnd) {
  const auto effect = static_cast<VisualEffect>(
      reinterpret_cast<uintptr_t>(GetPropW(hwnd, kVisualEffectProperty)));
  const bool whole = effect != VisualEffect::None
                         ? RecipeFor(effect).backdrop != kBackdropNone
                         : GetPropW(hwnd, kTranslucentBackgroundProperty) != nullptr;
  const int extent = whole ? -1 : 0;
  MARGINS margins = {extent, extent, extent, extent};
  DwmExtendFrameIntoClientArea(hwnd, &margins);
}

}  // namespace

// What a window looked like before it was made full screen, so that leaving full screen
// restores exactly that. Windows has no full-screen window state of its own — a full
// screen window is an ordinary window without a frame, sized to the monitor — so the
// library has to remember which of its windows are in that state. Reading it back from
// the geometry instead would make anything that moves or resizes the window look like
// leaving full screen, and the frame would then never be restored.
struct FullScreenState {
  WINDOWPLACEMENT placement;
  LONG_PTR style;
  LONG_PTR ex_style;
};
static std::unordered_map<HWND, FullScreenState> g_full_screen_windows;

// The frame bits taken away while a window is full screen, and put back afterwards.
static constexpr LONG_PTR kFullScreenRemovedStyle = WS_CAPTION | WS_THICKFRAME;
static constexpr LONG_PTR kFullScreenRemovedExStyle =
    WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;

// Adds or removes the window's taskbar button. The shell owns that button, so this is
// the only way to change it on a window that is already on screen; the window style
// alternative (WS_EX_TOOLWINDOW) only takes effect while the window is hidden and also
// drops it from Alt+Tab.
static void ApplyTaskbarVisibility(HWND hwnd, bool is_visible) {
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  ITaskbarList* taskbar = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&taskbar)))) {
    if (SUCCEEDED(taskbar->HrInit())) {
      if (is_visible)
        taskbar->AddTab(hwnd);
      else
        taskbar->DeleteTab(hwnd);
    }
    taskbar->Release();
  }
  // RPC_E_CHANGED_MODE says the thread already belongs to another apartment, which is
  // then not ours to leave.
  if (SUCCEEDED(com))
    CoUninitialize();
}

#ifndef NATIVEAPI_ENABLE_WINUI3
// Keep the native sizing/caption styles, but give the top non-client band to the
// content (WM_NCCALCSIZE) when the title bar is hidden. The sizing frame remains,
// the top edge stays resizable through hit testing: the window answers HTTOP there,
// and child windows covering the content let the hit test through to it.

// The resize hit code for a screen point in the top band of `root`, or 0.
static LRESULT TopResizeHit(HWND root, LPARAM point) {
  if (!GetPropW(root, kTitleBarHiddenProperty) || IsZoomed(root) ||
      !(GetWindowLongPtrW(root, GWL_STYLE) & WS_THICKFRAME))
    return 0;
  RECT window;
  GetWindowRect(root, &window);
  // The sizing frame is as thick at the top as on the left, where it is still in place.
  POINT client_origin = {0, 0};
  ClientToScreen(root, &client_origin);
  const int frame = client_origin.x - window.left;
  // GET_X_LPARAM / GET_Y_LPARAM, without <windowsx.h>: its IsMaximized() and
  // IsMinimized() macros would rename Window's methods of the same name.
  const int x = static_cast<short>(LOWORD(point));
  const int y = static_cast<short>(HIWORD(point));
  if (y < window.top || y >= window.top + frame || x < window.left || x >= window.right)
    return 0;
  if (x < window.left + frame) return HTTOPLEFT;
  if (x >= window.right - frame) return HTTOPRIGHT;
  return HTTOP;
}

// Subclass of the child windows of a window with a hidden title bar. A child that
// covers the content (a Flutter view, for one) is hit-tested before its parent;
// in the top resize band it steps aside so the parent can answer HTTOP.
static LRESULT CALLBACK TopEdgeChildProc(HWND child, UINT message, WPARAM wp, LPARAM lp,
                                         UINT_PTR subclass_id, DWORD_PTR) {
  if (message == WM_NCHITTEST) {
    // HTTRANSPARENT passes the hit test on to windows of the same thread only.
    HWND root = GetAncestor(child, GA_ROOT);
    if (root && TopResizeHit(root, lp) != 0 &&
        GetWindowThreadProcessId(root, nullptr) == GetCurrentThreadId())
      return HTTRANSPARENT;
  } else if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(child, TopEdgeChildProc, subclass_id);
  }
  return DefSubclassProc(child, message, wp, lp);
}

static BOOL CALLBACK AttachTopEdgeChild(HWND child, LPARAM) {
  // Fails for windows of other threads, which cannot be subclassed; that is fine.
  SetWindowSubclass(child, TopEdgeChildProc, 1, 0);
  return TRUE;
}

static std::optional<LRESULT> HandleHiddenTitleBarFrame(HWND hwnd, UINT message, WPARAM wp,
                                                        LPARAM lp) {
  if (message == WM_PARENTNOTIFY && LOWORD(wp) == WM_CREATE) {
    if (GetPropW(hwnd, kTitleBarHiddenProperty)) AttachTopEdgeChild(reinterpret_cast<HWND>(lp), 0);
    return std::nullopt;
  }
  if (!GetPropW(hwnd, kTitleBarHiddenProperty)) return std::nullopt;
  if (message == WM_NCCALCSIZE && wp) {
    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
    const LONG top = params->rgrc[0].top;
    const LRESULT result = DefSubclassProc(hwnd, message, wp, lp);
    if (!IsZoomed(hwnd)) {
      params->rgrc[0].top = top;
      return result;
    }
    // The maximized frame extends beyond the work area by its resize borders.
    // Fit the captionless client to the work area, excluding those borders.
    MONITORINFO monitor = {sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
      params->rgrc[0] = monitor.rcWork;
    return result;
  }
  if (message == WM_NCHITTEST) {
    const LRESULT hit = DefSubclassProc(hwnd, message, wp, lp);
    if (hit != HTCLIENT) return hit;
    const LRESULT top = TopResizeHit(hwnd, lp);
    return top != 0 ? top : hit;
  }
  return std::nullopt;
}
#endif

// Registry entries follow the HWND lifetime, not any one C++ wrapper.
static LRESULT CALLBACK WindowLifetimeProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                           UINT_PTR subclass_id, DWORD_PTR reference) {
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, WindowLifetimeProc, subclass_id);
    RemovePropW(hwnd, kWindowIdProperty);
    RemovePropW(hwnd, kTitleBarHiddenProperty);
    RemovePropW(hwnd, kNoShadowProperty);
    delete static_cast<WindowShadow*>(RemovePropW(hwnd, shape_shadow::kConfig));
    RemovePropW(hwnd, kHiddenFromTaskbarProperty);
    RemovePropW(hwnd, kVisualEffectProperty);
    RemovePropW(hwnd, kBackgroundColorProperty);
    RemovePropW(hwnd, kTranslucentBackgroundProperty);
    g_full_screen_windows.erase(hwnd);
    const auto result = DefSubclassProc(hwnd, message, wp, lp);
    WindowRegistry::GetInstance().Remove(static_cast<WindowId>(reference));
    return result;
  }
  if (message == WM_WINDOWPOSCHANGED) {
    const auto* pos = reinterpret_cast<const WINDOWPOS*>(lp);
    const LRESULT result = DefSubclassProc(hwnd, message, wp, lp);
    // The shell gives a window a fresh taskbar button every time it is shown, so a
    // window that is meant to stay out of the taskbar has to leave it again.
    if (pos && (pos->flags & SWP_SHOWWINDOW) && GetPropW(hwnd, kHiddenFromTaskbarProperty))
      ApplyTaskbarVisibility(hwnd, false);
    return result;
  }
#ifndef NATIVEAPI_ENABLE_WINUI3
  if (message == WM_NCCALCSIZE || message == WM_NCHITTEST || message == WM_PARENTNOTIFY) {
    if (auto handled = HandleHiddenTitleBarFrame(hwnd, message, wp, lp)) return *handled;
  }
#endif
  return DefSubclassProc(hwnd, message, wp, lp);
}
static void TrackWindowLifetime(HWND hwnd, WindowId id) {
  SetWindowSubclass(hwnd, WindowLifetimeProc, reinterpret_cast<UINT_PTR>(&WindowLifetimeProc), id);
}

// Forward declaration
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Private implementation class
class Window::Impl {
 public:
  Impl(HWND hwnd, WindowId id)
      : hwnd_(hwnd), window_id_(id) {}
  HWND hwnd_;
  WindowId window_id_;
  Size min_size_{0, 0};
  Size max_size_{0, 0};
  int min_max_handler_id_ = 0;
  double aspect_ratio_ = 0.0;
  int aspect_ratio_handler_id_ = 0;
  bool always_on_bottom_ = false;
  int always_on_bottom_handler_id_ = 0;
  // Recorded only: keyboard focus is per window on Windows, see Window::SetNonActivating().
  bool non_activating_ = false;
};

// Custom window procedure to handle window messages
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_ERASEBKGND:
      // The compositor draws a visual effect where the client area is black with no
      // alpha, which is what GDI's black is. The class brush would paint over it.
      if (GetPropW(hwnd, kVisualEffectProperty)) {
        RECT client;
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client,
                 static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return 1;
      }
      if (const auto stored =
              reinterpret_cast<uintptr_t>(GetPropW(hwnd, kBackgroundColorProperty))) {
        RECT client;
        GetClientRect(hwnd, &client);
        HBRUSH brush = CreateSolidBrush(
            RGB((stored >> 16) & 0xFF, (stored >> 8) & 0xFF, stored & 0xFF));
        FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
        DeleteObject(brush);
        return 1;
      }
      return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    case WM_WINDOWPOSCHANGING: {
      // Intercept visibility changes BEFORE they happen (pre-show/hide "swizzle")
      WINDOWPOS* pos = reinterpret_cast<WINDOWPOS*>(lParam);
      if (pos) {
        // Get window ID from window's custom property (stored during window creation)
        HANDLE prop_handle = GetPropW(hwnd, kWindowIdProperty);
        if (prop_handle) {
          WindowId window_id = static_cast<WindowId>(reinterpret_cast<uintptr_t>(prop_handle));
          if (window_id != IdAllocator::kInvalidId) {
            auto& manager = WindowManager::GetInstance();
            bool hook_handled = false;

            if (pos->flags & SWP_SHOWWINDOW) {
              if (manager.HasWillShowHook()) {
                manager.HandleWillShow(window_id);
                hook_handled = true;
              }
            }
            if (pos->flags & SWP_HIDEWINDOW) {
              if (manager.HasWillHideHook()) {
                manager.HandleWillHide(window_id);
                hook_handled = true;
              }
            }

            // If hook handled it, cancel the visibility change
            if (hook_handled) {
              pos->flags &= ~(SWP_SHOWWINDOW | SWP_HIDEWINDOW);
            }
          }
        }
      }
      return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    case WM_SHOWWINDOW:
      return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  }
}

Window::Window() {
  // Create a new window with default settings
  HINSTANCE hInstance = GetModuleHandle(nullptr);

  // Register window class if not already registered
  static bool class_registered = false;
  static std::wstring wclass_name = StringToWString("NativeAPIWindow");

  if (!class_registered) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = wclass_name.c_str();
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    if (RegisterClassW(&wc)) {
      class_registered = true;
    } else {
      DWORD error = GetLastError();
      if (error != ERROR_CLASS_ALREADY_EXISTS) {
        std::cerr << "Failed to register window class. Error: " << error << std::endl;
        // Allocate ID even for failed window creation to maintain consistency
        WindowId id = IdAllocator::Allocate<Window>();
        pimpl_ = std::make_unique<Impl>(nullptr, id);
        return;
      }
      class_registered = true;
    }
  }

  // Create the window
  DWORD style = WS_OVERLAPPEDWINDOW;
  DWORD exStyle = 0;

  HWND hwnd = CreateWindowExW(exStyle, wclass_name.c_str(), L"", style, CW_USEDEFAULT,
                              CW_USEDEFAULT, 800, 600, nullptr, nullptr, hInstance, nullptr);

  if (!hwnd) {
    std::cerr << "Failed to create window. Error: " << GetLastError() << std::endl;
    // Allocate ID even for failed window creation to maintain consistency
    WindowId id = IdAllocator::Allocate<Window>();
    pimpl_ = std::make_unique<Impl>(nullptr, id);
    return;
  }

  // Allocate window ID using IdAllocator
  WindowId id = IdAllocator::Allocate<Window>();
  if (id == IdAllocator::kInvalidId) {
    std::cerr << "Failed to allocate window ID" << std::endl;
    DestroyWindow(hwnd);
    pimpl_ = std::make_unique<Impl>(nullptr, IdAllocator::kInvalidId);
    return;
  }

  // Store window ID as a custom property in HWND for easy retrieval in WindowProc
  SetPropW(hwnd, kWindowIdProperty, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(id)));

  // Create the instance with allocated ID
  pimpl_ = std::make_unique<Impl>(hwnd, id);
  TrackWindowLifetime(hwnd, id);

  // Note: Window registration in WindowRegistry is now handled by WindowManager::GetAll()
  // which uses EnumWindows to discover and register all windows dynamically
}

Window::Window(void* native_window) {
  HWND hwnd = static_cast<HWND>(native_window);

  if (!hwnd) {
    // Allocate ID even for null window to maintain consistency
    WindowId id = IdAllocator::Allocate<Window>();
    pimpl_ = std::make_unique<Impl>(nullptr, id);
    return;
  }

  // Check if window already has an ID stored as a custom property
  HANDLE prop_handle = GetPropW(hwnd, kWindowIdProperty);
  WindowId id = IdAllocator::kInvalidId;

  if (prop_handle) {
    id = static_cast<WindowId>(reinterpret_cast<uintptr_t>(prop_handle));
  }

  if (id == IdAllocator::kInvalidId || id == 0) {
    // Allocate new ID if window doesn't have one
    id = IdAllocator::Allocate<Window>();
    if (id == IdAllocator::kInvalidId) {
      std::cerr << "Failed to allocate window ID" << std::endl;
      pimpl_ = std::make_unique<Impl>(nullptr, IdAllocator::kInvalidId);
      return;
    }
    // Store the ID as a custom property in HWND
    SetPropW(hwnd, kWindowIdProperty, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(id)));
  }

  pimpl_ = std::make_unique<Impl>(hwnd, id);
  TrackWindowLifetime(hwnd, id);

  // Note: Window registration in WindowRegistry is now handled by WindowManager::GetAll()
  // which uses EnumWindows to discover and register all windows dynamically
}

Window::~Window() {
  if (pimpl_ && pimpl_->window_id_ != IdAllocator::kInvalidId) {
    // Unregister WM_GETMINMAXINFO handler if registered
    if (pimpl_->min_max_handler_id_ != 0 && pimpl_->hwnd_) {
      WindowMessageDispatcher::GetInstance().UnregisterHandler(
          pimpl_->min_max_handler_id_);
    }
    if (pimpl_->aspect_ratio_handler_id_ != 0 && pimpl_->hwnd_) {
      WindowMessageDispatcher::GetInstance().UnregisterHandler(
          pimpl_->aspect_ratio_handler_id_);
    }
    if (pimpl_->always_on_bottom_handler_id_ != 0 && pimpl_->hwnd_) {
      WindowMessageDispatcher::GetInstance().UnregisterHandler(
          pimpl_->always_on_bottom_handler_id_);
    }


  }
}

void Window::Focus() {
  if (pimpl_->hwnd_) {
    SetForegroundWindow(pimpl_->hwnd_);
    SetFocus(pimpl_->hwnd_);
  }
}

void Window::Blur() {
  if (pimpl_->hwnd_) {
    SetFocus(nullptr);
  }
}

bool Window::IsFocused() const {
  return pimpl_->hwnd_ && GetForegroundWindow() == pimpl_->hwnd_;
}

void Window::Show() {
  if (pimpl_->hwnd_) {
    ShowWindow(pimpl_->hwnd_, SW_SHOW);
    SetForegroundWindow(pimpl_->hwnd_);
  }
}

void Window::ShowInactive() {
  if (pimpl_->hwnd_) {
    ShowWindow(pimpl_->hwnd_, SW_SHOWNOACTIVATE);
  }
}

void Window::Hide() {
  if (pimpl_->hwnd_) {
    ShowWindow(pimpl_->hwnd_, SW_HIDE);
  }
}

bool Window::IsVisible() const {
  return pimpl_->hwnd_ && IsWindowVisible(pimpl_->hwnd_);
}

void Window::Maximize() {
  if (pimpl_->hwnd_ && !IsMaximized()) {
    ShowWindow(pimpl_->hwnd_, SW_MAXIMIZE);
  }
}

void Window::Unmaximize() {
  if (pimpl_->hwnd_ && IsMaximized()) {
    ShowWindow(pimpl_->hwnd_, SW_RESTORE);
  }
}

bool Window::IsMaximized() const {
  if (!pimpl_->hwnd_)
    return false;
  WINDOWPLACEMENT wp = {};
  wp.length = sizeof(WINDOWPLACEMENT);
  GetWindowPlacement(pimpl_->hwnd_, &wp);
  return wp.showCmd == SW_MAXIMIZE;
}

void Window::Minimize() {
  if (pimpl_->hwnd_ && !IsMinimized()) {
    ShowWindow(pimpl_->hwnd_, SW_MINIMIZE);
  }
}

void Window::Restore() {
  if (pimpl_->hwnd_ && IsMinimized()) {
    ShowWindow(pimpl_->hwnd_, SW_RESTORE);
  }
}

bool Window::IsMinimized() const {
  if (!pimpl_->hwnd_)
    return false;
  // GetWindowPlacement() reports a minimized window as SW_SHOWMINIMIZED, never
  // as SW_MINIMIZE, so ask the window itself.
  return IsIconic(pimpl_->hwnd_) != FALSE;
}

void Window::SetFullScreen(bool is_full_screen) {
  if (!pimpl_->hwnd_)
    return;

  HWND hwnd = pimpl_->hwnd_;
  const auto saved = g_full_screen_windows.find(hwnd);

  if (is_full_screen) {
    if (saved != g_full_screen_windows.end())
      return;

    MONITORINFO monitor = {sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
      return;

    FullScreenState state = {};
    state.placement.length = sizeof(state.placement);
    GetWindowPlacement(hwnd, &state.placement);
    state.style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    state.ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    g_full_screen_windows[hwnd] = state;

    SetWindowLongPtrW(hwnd, GWL_STYLE, state.style & ~kFullScreenRemovedStyle);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, state.ex_style & ~kFullScreenRemovedExStyle);
    SetWindowPos(hwnd, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
                 monitor.rcMonitor.right - monitor.rcMonitor.left,
                 monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return;
  }

  if (saved == g_full_screen_windows.end())
    return;

  const FullScreenState state = saved->second;
  g_full_screen_windows.erase(saved);
  // Put back the bits that were taken away, rather than the whole style word: the
  // window may have been reshaped while it was full screen (the title bar hidden, say)
  // and that is not this method's to undo.
  SetWindowLongPtrW(hwnd, GWL_STYLE,
                    GetWindowLongPtrW(hwnd, GWL_STYLE) | (state.style & kFullScreenRemovedStyle));
  SetWindowLongPtrW(
      hwnd, GWL_EXSTYLE,
      GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | (state.ex_style & kFullScreenRemovedExStyle));
  SetWindowPlacement(hwnd, &state.placement);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}

bool Window::IsFullScreen() const {
  return pimpl_->hwnd_ && g_full_screen_windows.count(pimpl_->hwnd_) != 0;
}

void Window::SetBounds(Rectangle bounds) {
  if (pimpl_->hwnd_) {
    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;
    SetWindowPos(pimpl_->hwnd_, nullptr,
                 static_cast<int>(std::lround(bounds.x * scale)),
                 static_cast<int>(std::lround(bounds.y * scale)),
                 static_cast<int>(std::lround(bounds.width * scale)),
                 static_cast<int>(std::lround(bounds.height * scale)), SWP_NOZORDER);
  }
}

Rectangle Window::GetBounds() const {
  Rectangle bounds = {0, 0, 0, 0};
  if (pimpl_->hwnd_) {
    RECT rect;
    GetWindowRect(pimpl_->hwnd_, &rect);
    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;
    bounds.x = static_cast<double>(rect.left) / scale;
    bounds.y = static_cast<double>(rect.top) / scale;
    bounds.width = static_cast<double>(rect.right - rect.left) / scale;
    bounds.height = static_cast<double>(rect.bottom - rect.top) / scale;
  }
  return bounds;
}

void Window::SetSize(Size size, bool animate) {
  if (pimpl_->hwnd_) {
    // Windows doesn't have built-in animation for window resizing
    // Animation would require custom implementation
    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;
    SetWindowPos(pimpl_->hwnd_, nullptr, 0, 0,
                 static_cast<int>(std::lround(size.width * scale)),
                 static_cast<int>(std::lround(size.height * scale)),
                 SWP_NOMOVE | SWP_NOZORDER);
  }
}

Size Window::GetSize() const {
  Size size = {0, 0};
  if (pimpl_->hwnd_) {
    RECT rect;
    GetWindowRect(pimpl_->hwnd_, &rect);
    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;
    size.width = static_cast<double>(rect.right - rect.left) / scale;
    size.height = static_cast<double>(rect.bottom - rect.top) / scale;
  }
  return size;
}

void Window::SetContentSize(Size size) {
  if (pimpl_->hwnd_) {
    RECT windowRect, clientRect;
    GetWindowRect(pimpl_->hwnd_, &windowRect);
    GetClientRect(pimpl_->hwnd_, &clientRect);

    // Calculate the difference between window and client area
    int borderWidth = (windowRect.right - windowRect.left) - clientRect.right;
    int borderHeight = (windowRect.bottom - windowRect.top) - clientRect.bottom;

    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;
    SetWindowPos(pimpl_->hwnd_, nullptr, 0, 0,
                 static_cast<int>(std::lround(size.width * scale)) + borderWidth,
                 static_cast<int>(std::lround(size.height * scale)) + borderHeight,
                 SWP_NOMOVE | SWP_NOZORDER);
  }
}

Size Window::GetContentSize() const {
  Size size = {0, 0};
  if (pimpl_->hwnd_) {
    RECT rect;
    GetClientRect(pimpl_->hwnd_, &rect);
    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;
    size.width = static_cast<double>(rect.right) / scale;
    size.height = static_cast<double>(rect.bottom) / scale;
  }
  return size;
}

void Window::SetContentBounds(Rectangle bounds) {
  if (pimpl_->hwnd_) {
    RECT windowRect, clientRect;
    GetWindowRect(pimpl_->hwnd_, &windowRect);
    GetClientRect(pimpl_->hwnd_, &clientRect);

    // Calculate the difference between window and client area
    int borderWidth = (windowRect.right - windowRect.left) - clientRect.right;
    int borderHeight = (windowRect.bottom - windowRect.top) - clientRect.bottom;

    // Get current client area position in screen coordinates
    POINT clientTopLeft = {0, 0};
    ClientToScreen(pimpl_->hwnd_, &clientTopLeft);

    // Calculate the offset from window top-left to client top-left
    int offsetX = clientTopLeft.x - windowRect.left;
    int offsetY = clientTopLeft.y - windowRect.top;

    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;

    // Calculate window position so that client area is at bounds position
    int windowX = static_cast<int>(std::lround(bounds.x * scale)) - offsetX;
    int windowY = static_cast<int>(std::lround(bounds.y * scale)) - offsetY;
    int windowWidth = static_cast<int>(std::lround(bounds.width * scale)) + borderWidth;
    int windowHeight = static_cast<int>(std::lround(bounds.height * scale)) + borderHeight;

    SetWindowPos(pimpl_->hwnd_, nullptr, windowX, windowY, windowWidth, windowHeight, SWP_NOZORDER);
  }
}

Rectangle Window::GetContentBounds() const {
  Rectangle bounds = {0, 0, 0, 0};
  if (pimpl_->hwnd_) {
    RECT clientRect;
    GetClientRect(pimpl_->hwnd_, &clientRect);

    // Convert client rect to screen coordinates (physical pixels)
    POINT topLeft = {clientRect.left, clientRect.top};
    POINT bottomRight = {clientRect.right, clientRect.bottom};
    ClientToScreen(pimpl_->hwnd_, &topLeft);
    ClientToScreen(pimpl_->hwnd_, &bottomRight);

    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;

    // Return logical pixels (DIP) by dividing by scale
    bounds.x = static_cast<double>(topLeft.x) / scale;
    bounds.y = static_cast<double>(topLeft.y) / scale;
    bounds.width = static_cast<double>(bottomRight.x - topLeft.x) / scale;
    bounds.height = static_cast<double>(bottomRight.y - topLeft.y) / scale;
  }
  return bounds;
}

// Helper function: resolves the nativeapi Window that owns an HWND via the
// WindowId property stored on it. Returns nullptr for foreign windows.
static std::shared_ptr<Window> WindowFromHwnd(HWND hwnd) {
  HANDLE prop_handle = GetPropW(hwnd, kWindowIdProperty);
  if (!prop_handle) {
    return nullptr;
  }
  WindowId window_id = static_cast<WindowId>(reinterpret_cast<uintptr_t>(prop_handle));
  if (window_id == IdAllocator::kInvalidId) {
    return nullptr;
  }
  return WindowRegistry::GetInstance().Get(window_id);
}

// Helper function: registers a WM_SIZING handler that keeps user-driven
// resizing at the window's aspect ratio. Returns the handler ID.
//
// The ratio applies to the client area, as on macOS and Linux: the non-client
// size (title bar, borders; zero when the client takes the whole frame) is taken
// off the tracking rectangle first and added back afterwards. Like the min/max
// handler, it reads the state of the wrapper that installed it, whose destructor
// unregisters it; WindowRegistry may hold a different wrapper for the HWND.
static int RegisterAspectRatioHandler(HWND hwnd,
                                      int existing_handler_id,
                                      const double* aspect_ratio,
                                      const Size* minimum,
                                      const Size* maximum) {
  if (existing_handler_id != 0) {
    return existing_handler_id;
  }
  if (!hwnd || !IsWindow(hwnd)) {
    return 0;
  }
  return WindowMessageDispatcher::GetInstance().RegisterHandler(
      hwnd,
      [aspect_ratio, minimum, maximum](HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam) -> std::optional<LRESULT> {
        const double ratio = *aspect_ratio;
        RECT* rect = reinterpret_cast<RECT*>(lparam);
        if (msg != WM_SIZING || ratio <= 0.0 || !rect) {
          return std::nullopt;
        }
        RECT window_rect = {};
        RECT client_rect = {};
        if (!GetWindowRect(hwnd, &window_rect) || !GetClientRect(hwnd, &client_rect)) {
          return std::nullopt;
        }
        const LONG extra_width =
            (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
        const LONG extra_height =
            (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);

        // Minimum and maximum sizes are frame sizes (WM_GETMINMAXINFO) and still
        // win over the ratio; where one clips the derived side, derive the other
        // side back from it so the ratio holds whenever the limits allow.
        double scale = GetScaleFactorForWindow(hwnd);
        if (scale <= 0.0) {
          scale = 1.0;
        }
        auto limit = [scale](double value, double fallback) {
          return value > 0 ? static_cast<LONG>(std::lround(value * scale))
                           : static_cast<LONG>(fallback);
        };
        const LONG min_width = limit(minimum->width, 0);
        const LONG min_height = limit(minimum->height, 0);
        const LONG max_width = limit(maximum->width, LONG_MAX);
        const LONG max_height = limit(maximum->height, LONG_MAX);
        auto height_for = [&](LONG width) {
          return static_cast<LONG>(std::lround((width - extra_width) / ratio)) + extra_height;
        };
        auto width_for = [&](LONG height) {
          return static_cast<LONG>(std::lround((height - extra_height) * ratio)) + extra_width;
        };

        LONG width = rect->right - rect->left;
        LONG height = rect->bottom - rect->top;
        // Pure vertical edges derive width from height; everything else derives
        // height from width.
        if (wparam == WMSZ_TOP || wparam == WMSZ_BOTTOM) {
          width = width_for(height);
          const LONG clamped = std::clamp(width, min_width, max_width);
          if (clamped != width) {
            width = clamped;
            height = height_for(width);
          }
        } else {
          height = height_for(width);
          const LONG clamped = std::clamp(height, min_height, max_height);
          if (clamped != height) {
            height = clamped;
            width = width_for(height);
          }
        }
        width = std::clamp(width, min_width, max_width);
        height = std::clamp(height, min_height, max_height);

        // Grow away from the edge being dragged so the opposite edge stays put.
        switch (wparam) {
          case WMSZ_LEFT:
          case WMSZ_BOTTOMLEFT:
            rect->left = rect->right - width;
            rect->bottom = rect->top + height;
            break;
          case WMSZ_TOPLEFT:
            rect->left = rect->right - width;
            rect->top = rect->bottom - height;
            break;
          case WMSZ_TOP:
          case WMSZ_TOPRIGHT:
            rect->right = rect->left + width;
            rect->top = rect->bottom - height;
            break;
          default:  // WMSZ_RIGHT, WMSZ_BOTTOM, WMSZ_BOTTOMRIGHT
            rect->right = rect->left + width;
            rect->bottom = rect->top + height;
            break;
        }
        return std::make_optional<LRESULT>(TRUE);
      });
}

// Helper function: registers a WM_WINDOWPOSCHANGING handler that pins the
// window to the bottom of the Z order for as long as IsAlwaysOnBottom() holds.
// Returns the handler ID.
static int RegisterAlwaysOnBottomHandler(HWND hwnd, int existing_handler_id) {
  if (existing_handler_id != 0) {
    return existing_handler_id;
  }
  if (!hwnd || !IsWindow(hwnd)) {
    return 0;
  }
  auto& dispatcher = WindowMessageDispatcher::GetInstance();
  return dispatcher.RegisterHandler(
      hwnd,
      [](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) -> std::optional<LRESULT> {
        if (msg != WM_WINDOWPOSCHANGING) {
          return std::nullopt;
        }
        auto window = WindowFromHwnd(hwnd);
        WINDOWPOS* pos = reinterpret_cast<WINDOWPOS*>(lparam);
        if (window && pos && window->IsAlwaysOnBottom()) {
          pos->hwndInsertAfter = HWND_BOTTOM;
          pos->flags &= ~SWP_NOZORDER;
        }
        // Let the original procedure see the (possibly adjusted) message.
        return std::nullopt;
      });
}

// Helper function: registers a WM_GETMINMAXINFO handler for the given HWND
// via WindowMessageDispatcher if not already registered. Returns the handler ID.
static int RegisterMinMaxInfoHandler(HWND hwnd,
                                     int existing_handler_id,
                                     const Size* minimum,
                                     const Size* maximum) {
  if (existing_handler_id != 0) {
    return existing_handler_id;
  }
  if (!hwnd || !IsWindow(hwnd)) {
    return 0;
  }
  // A directly constructed Window need not be in WindowRegistry; enumeration
  // may also register a different wrapper for this HWND. Read the state of the
  // wrapper that installed this handler. Its destructor unregisters the handler
  // before destroying the Impl that owns these sizes.
  return WindowMessageDispatcher::GetInstance().RegisterHandler(
      hwnd,
      [minimum, maximum](HWND hwnd, UINT msg, WPARAM, LPARAM lparam) -> std::optional<LRESULT> {
        if (msg != WM_GETMINMAXINFO || !lparam) {
          return std::nullopt;
        }
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        double scale = GetScaleFactorForWindow(hwnd);
        if (scale <= 0.0) {
          scale = 1.0;
        }
        // Each axis can be bounded independently. Leave unconstrained fields
        // as supplied by Windows, including the system's normal tracking limits.
        if (minimum->width > 0) {
          info->ptMinTrackSize.x = static_cast<LONG>(std::lround(minimum->width * scale));
        }
        if (minimum->height > 0) {
          info->ptMinTrackSize.y = static_cast<LONG>(std::lround(minimum->height * scale));
        }
        if (maximum->width > 0) {
          info->ptMaxTrackSize.x = static_cast<LONG>(std::lround(maximum->width * scale));
        }
        if (maximum->height > 0) {
          info->ptMaxTrackSize.y = static_cast<LONG>(std::lround(maximum->height * scale));
        }
        return 0;
      });
}

void Window::SetMinimumSize(Size size) {
  pimpl_->min_size_ = size;

  if (pimpl_->hwnd_) {
    pimpl_->min_max_handler_id_ = RegisterMinMaxInfoHandler(
        pimpl_->hwnd_, pimpl_->min_max_handler_id_, &pimpl_->min_size_, &pimpl_->max_size_);

    // Trigger the window to re-evaluate its size constraints
    SetWindowPos(pimpl_->hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
  }
}

Size Window::GetMinimumSize() const {
  return pimpl_->min_size_;
}

void Window::SetMaximumSize(Size size) {
  pimpl_->max_size_ = size;

  if (pimpl_->hwnd_) {
    pimpl_->min_max_handler_id_ = RegisterMinMaxInfoHandler(
        pimpl_->hwnd_, pimpl_->min_max_handler_id_, &pimpl_->min_size_, &pimpl_->max_size_);

    // Trigger the window to re-evaluate its size constraints
    SetWindowPos(pimpl_->hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
  }
}

void Window::SetAspectRatio(double aspect_ratio) {
  pimpl_->aspect_ratio_ = aspect_ratio > 0.0 ? aspect_ratio : 0.0;
  if (pimpl_->hwnd_ && pimpl_->aspect_ratio_ > 0.0) {
    pimpl_->aspect_ratio_handler_id_ = RegisterAspectRatioHandler(
        pimpl_->hwnd_, pimpl_->aspect_ratio_handler_id_, &pimpl_->aspect_ratio_,
        &pimpl_->min_size_, &pimpl_->max_size_);
  }
}

double Window::GetAspectRatio() const {
  return pimpl_->aspect_ratio_;
}

Size Window::GetMaximumSize() const {
  return pimpl_->max_size_;
}

void Window::SetResizable(bool is_resizable) {
  if (pimpl_->hwnd_) {
    LONG style = GetWindowLong(pimpl_->hwnd_, GWL_STYLE);
    if (is_resizable) {
      style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    } else {
      style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    SetWindowLong(pimpl_->hwnd_, GWL_STYLE, style);
    SetWindowPos(pimpl_->hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
}

bool Window::IsResizable() const {
  if (!pimpl_->hwnd_)
    return false;
  LONG style = GetWindowLong(pimpl_->hwnd_, GWL_STYLE);
  return (style & WS_THICKFRAME) != 0;
}

void Window::SetMovable(bool is_movable) {
  // Windows doesn't have a direct way to disable window movement
  // This would require custom window procedure handling
}

bool Window::IsMovable() const {
  // Windows windows are movable by default
  return true;
}

void Window::SetMinimizable(bool is_minimizable) {
  if (pimpl_->hwnd_) {
    LONG style = GetWindowLong(pimpl_->hwnd_, GWL_STYLE);
    if (is_minimizable) {
      style |= WS_MINIMIZEBOX;
    } else {
      style &= ~WS_MINIMIZEBOX;
    }
    SetWindowLong(pimpl_->hwnd_, GWL_STYLE, style);
    SetWindowPos(pimpl_->hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
}

bool Window::IsMinimizable() const {
  if (!pimpl_->hwnd_)
    return false;
  LONG style = GetWindowLong(pimpl_->hwnd_, GWL_STYLE);
  return (style & WS_MINIMIZEBOX) != 0;
}

void Window::SetMaximizable(bool is_maximizable) {
  if (pimpl_->hwnd_) {
    LONG style = GetWindowLong(pimpl_->hwnd_, GWL_STYLE);
    if (is_maximizable) {
      style |= WS_MAXIMIZEBOX;
    } else {
      style &= ~WS_MAXIMIZEBOX;
    }
    SetWindowLong(pimpl_->hwnd_, GWL_STYLE, style);
    SetWindowPos(pimpl_->hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
}

bool Window::IsMaximizable() const {
  if (!pimpl_->hwnd_)
    return false;
  LONG style = GetWindowLong(pimpl_->hwnd_, GWL_STYLE);
  return (style & WS_MAXIMIZEBOX) != 0;
}

void Window::SetFullScreenable(bool is_full_screenable) {
  // This is a concept more relevant to macOS
  // On Windows, any window can potentially go fullscreen
}

bool Window::IsFullScreenable() const {
  return true;  // All Windows windows can go fullscreen
}

void Window::SetClosable(bool is_closable) {
  if (pimpl_->hwnd_) {
    LONG style = GetWindowLong(pimpl_->hwnd_, GWL_STYLE);
    if (is_closable) {
      style |= WS_SYSMENU;
    } else {
      style &= ~WS_SYSMENU;
    }
    SetWindowLong(pimpl_->hwnd_, GWL_STYLE, style);
    SetWindowPos(pimpl_->hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
}

bool Window::IsClosable() const {
  if (!pimpl_->hwnd_)
    return false;
  LONG style = GetWindowLong(pimpl_->hwnd_, GWL_STYLE);
  return (style & WS_SYSMENU) != 0;
}

void Window::SetWindowControlButtonsVisible(bool is_visible) {
  // TODO: Implement for Windows
  // This would involve custom window chrome or DWM frame manipulation
}

bool Window::IsWindowControlButtonsVisible() const {
  // TODO: Implement for Windows
  return true;  // Default to visible
}

void Window::SetAlwaysOnTop(bool is_always_on_top) {
  if (is_always_on_top) {
    pimpl_->always_on_bottom_ = false;
  }
  if (pimpl_->hwnd_) {
    SetWindowPos(pimpl_->hwnd_, is_always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE);
  }
}

bool Window::IsAlwaysOnTop() const {
  if (!pimpl_->hwnd_)
    return false;
  LONG exStyle = GetWindowLong(pimpl_->hwnd_, GWL_EXSTYLE);
  return (exStyle & WS_EX_TOPMOST) != 0;
}

void Window::SetAlwaysOnBottom(bool is_always_on_bottom) {
  pimpl_->always_on_bottom_ = is_always_on_bottom;
  if (!pimpl_->hwnd_) {
    return;
  }
  if (is_always_on_bottom) {
    pimpl_->always_on_bottom_handler_id_ =
        RegisterAlwaysOnBottomHandler(pimpl_->hwnd_, pimpl_->always_on_bottom_handler_id_);
  }
  // HWND_NOTOPMOST also clears WS_EX_TOPMOST, so this doubles as the "vice versa"
  // half of the always-on-top exclusivity.
  SetWindowPos(pimpl_->hwnd_, is_always_on_bottom ? HWND_BOTTOM : HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool Window::IsAlwaysOnBottom() const {
  return pimpl_->always_on_bottom_;
}

bool Window::SetParentWindow(std::shared_ptr<Window> parent) {
  HWND hwnd = pimpl_->hwnd_;
  if (!hwnd || !IsWindow(hwnd)) {
    return false;
  }
  HWND parent_hwnd = nullptr;
  if (parent) {
    parent_hwnd = static_cast<HWND>(parent->GetNativeObject());
    if (!parent_hwnd || !IsWindow(parent_hwnd)) {
      return false;
    }
    // Neither itself nor one of its own descendants
    for (HWND ancestor = parent_hwnd; ancestor; ancestor = GetWindow(ancestor, GW_OWNER)) {
      if (ancestor == hwnd) {
        return false;
      }
    }
  }
  // For a top-level window GWLP_HWNDPARENT is the owner, not a parent in the
  // WS_CHILD sense. The previous value may legitimately be 0, so the error has
  // to be read from the thread.
  SetLastError(0);
  if (SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(parent_hwnd)) == 0 &&
      GetLastError() != 0) {
    return false;
  }
  if (parent_hwnd && IsWindowVisible(hwnd)) {
    // The owner only takes effect in the Z order the next time it is computed
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  return true;
}

std::shared_ptr<Window> Window::GetParentWindow() const {
  HWND hwnd = pimpl_->hwnd_;
  HWND owner = (hwnd && IsWindow(hwnd)) ? GetWindow(hwnd, GW_OWNER) : nullptr;
  if (!owner) {
    return nullptr;
  }
  // The wrapper takes the ID the native window already carries, which is how
  // the registered Window for it, if there is one, is found.
  auto wrapper = std::make_shared<Window>(static_cast<void*>(owner));
  auto registered = WindowManager::GetInstance().Get(wrapper->GetId());
  return registered ? registered : wrapper;
}

void Window::SetNonActivating(bool is_non_activating) {
  // Windows keeps keyboard focus per window, so a non-activating window has no
  // observable difference here. Record the flag so IsNonActivating() round-trips.
  pimpl_->non_activating_ = is_non_activating;
}

bool Window::IsNonActivating() const {
  return pimpl_->non_activating_;
}

void Window::SetPosition(Point point) {
  if (pimpl_->hwnd_) {
    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;
    SetWindowPos(pimpl_->hwnd_, nullptr,
                 static_cast<int>(std::lround(point.x * scale)),
                 static_cast<int>(std::lround(point.y * scale)),
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  }
}

Point Window::GetPosition() const {
  Point point = {0, 0};
  if (pimpl_->hwnd_) {
    RECT rect;
    GetWindowRect(pimpl_->hwnd_, &rect);
    double scale = GetScaleFactorForWindow(pimpl_->hwnd_);
    if (scale <= 0.0)
      scale = 1.0;
    point.x = static_cast<double>(rect.left) / scale;
    point.y = static_cast<double>(rect.top) / scale;
  }
  return point;
}

void Window::Center() {
  if (!pimpl_->hwnd_)
    return;

  // A full screen window already fills the monitor. Centering it in the work area
  // would push it off the screen by half the taskbar's height.
  if (IsFullScreen())
    return;

  // Get the current window size
  RECT windowRect;
  GetWindowRect(pimpl_->hwnd_, &windowRect);
  int windowWidth = windowRect.right - windowRect.left;
  int windowHeight = windowRect.bottom - windowRect.top;

  // Get the monitor that the window is currently on
  HMONITOR monitor = MonitorFromWindow(pimpl_->hwnd_, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfo(monitor, &mi);

  // Calculate the center position on the monitor's work area
  // All values here are in physical pixels (GetWindowRect and rcWork), so no DPI scaling needed
  int centerX = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - windowWidth) / 2;
  int centerY = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - windowHeight) / 2;

  // Set the window position to center
  SetWindowPos(pimpl_->hwnd_, nullptr, centerX, centerY, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::SetTitle(std::string title) {
  if (pimpl_->hwnd_) {
    std::wstring wtitle = StringToWString(title);
    SetWindowTextW(pimpl_->hwnd_, wtitle.c_str());
  }
}

std::string Window::GetTitle() const {
  if (!pimpl_->hwnd_)
    return "";

  int length = GetWindowTextLengthW(pimpl_->hwnd_);
  if (length == 0)
    return "";

  std::wstring wtitle(length + 1, L'\0');
  GetWindowTextW(pimpl_->hwnd_, &wtitle[0], length + 1);
  wtitle.resize(length);
  return WStringToString(wtitle);
}

void Window::SetTitleBarStyle(TitleBarStyle style) {
  if (!pimpl_->hwnd_)
    return;

#ifdef NATIVEAPI_ENABLE_WINUI3
  if (!SetWinUI3TitleBarStyle(pimpl_->hwnd_, style)) return;
#else
  LONG_PTR flags = GetWindowLongPtrW(pimpl_->hwnd_, GWL_STYLE);
  // Keep the native caption style even when its non-client area is hidden by
  // HandleHiddenTitleBarFrame. Removing it makes Windows maximize the outer
  // window over the entire monitor, which also hides the taskbar. Clamping only
  // the client area in WM_NCCALCSIZE cannot fix the shell's fullscreen detection.
  flags |= WS_CAPTION;
  SetWindowLongPtrW(pimpl_->hwnd_, GWL_STYLE, flags);
#endif
  // Read by WM_NCCALCSIZE during the frame change below.
  if (style == TitleBarStyle::Hidden) {
    SetPropW(pimpl_->hwnd_, kTitleBarHiddenProperty, reinterpret_cast<HANDLE>(1));
#ifndef NATIVEAPI_ENABLE_WINUI3
    // Existing children; later ones are attached as they are created (WM_PARENTNOTIFY).
    EnumChildWindows(pimpl_->hwnd_, AttachTopEdgeChild, 0);
#endif
  } else {
    RemovePropW(pimpl_->hwnd_, kTitleBarHiddenProperty);
  }

  // Get current window rect
  RECT rect;
  GetWindowRect(pimpl_->hwnd_, &rect);

  // Apply DWM frame extension based on style
  UpdateFrameExtent(pimpl_->hwnd_);

  // Trigger frame change to apply the new style
  SetWindowPos(pimpl_->hwnd_, nullptr, rect.left, rect.top, 0, 0,
               SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
  SetHasShadow(HasShadow());
}

TitleBarStyle Window::GetTitleBarStyle() const {
  if (pimpl_->hwnd_ && GetPropW(pimpl_->hwnd_, kTitleBarHiddenProperty))
    return TitleBarStyle::Hidden;
  return TitleBarStyle::Normal;
}

// The client area can be given the caption band (WM_NCCALCSIZE), but the caption buttons
// DWM keeps drawing there answer HTCLIENT once it is client area, so they stop working:
// they would have to be hit-tested and drawn by hand. TitleBarStyle::Hidden plus chrome
// of the application's own is the way to a full-bleed window here.
bool Window::SetContentUnderTitleBar(bool is_content_under_title_bar) {
  return false;
}

bool Window::IsContentUnderTitleBar() const {
  return false;
}

bool Window::IsContentUnderTitleBarSupported() {
  return false;
}

void Window::SetHasShadow(bool has_shadow) {
  if (!pimpl_->hwnd_)
    return;

  // The shadow is part of the frame the desktop compositor draws around the window, so
  // it goes away with the rest of that frame. On a window that still has a title bar
  // the caption is drawn there too and the compositor keeps both.
  DWMNCRENDERINGPOLICY policy = has_shadow && GetTitleBarStyle() != TitleBarStyle::Hidden
                                     ? DWMNCRP_USEWINDOWSTYLE : DWMNCRP_DISABLED;
  if (FAILED(DwmSetWindowAttribute(pimpl_->hwnd_, DWMWA_NCRENDERING_POLICY, &policy,
                                   sizeof(policy))))
    return;

  if (has_shadow)
    RemovePropW(pimpl_->hwnd_, kNoShadowProperty);
  else
    SetPropW(pimpl_->hwnd_, kNoShadowProperty, reinterpret_cast<HANDLE>(1));
  if (!has_shadow) {
    shape_shadow::Clear(pimpl_->hwnd_);
  } else {
    if (GetTitleBarStyle() == TitleBarStyle::Hidden) shape_shadow::Refresh(pimpl_->hwnd_);
    else shape_shadow::Clear(pimpl_->hwnd_);
  }
}

bool Window::SetCustomShadow(std::shared_ptr<WindowShadow> shadow) {
  HWND hwnd = pimpl_->hwnd_;
  if (!IsWindow(hwnd) || (shadow && GetTitleBarStyle() != TitleBarStyle::Hidden)) return false;
  auto* copy = shadow ? new WindowShadow(*shadow) : nullptr;
  auto* previous = static_cast<WindowShadow*>(GetPropW(hwnd, shape_shadow::kConfig));
  if (copy && !SetPropW(hwnd, shape_shadow::kConfig, copy)) { delete copy; return false; }
  if (!copy) RemovePropW(hwnd, shape_shadow::kConfig);
  delete previous;
  SetHasShadow(HasShadow());
  return true;
}
std::shared_ptr<WindowShadow> Window::GetCustomShadow() const {
  const auto* options = static_cast<WindowShadow*>(GetPropW(pimpl_->hwnd_, shape_shadow::kConfig));
  return options ? std::make_shared<WindowShadow>(*options) : nullptr;
}

bool Window::HasShadow() const {
  return pimpl_->hwnd_ && !GetPropW(pimpl_->hwnd_, kNoShadowProperty);
}

void Window::SetOpacity(float opacity) {
  if (pimpl_->hwnd_) {
    LONG exStyle = GetWindowLong(pimpl_->hwnd_, GWL_EXSTYLE);

    if (opacity < 1.0f) {
      // Enable layered window and set opacity
      SetWindowLong(pimpl_->hwnd_, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
      SetLayeredWindowAttributes(pimpl_->hwnd_, 0, static_cast<BYTE>(opacity * 255), LWA_ALPHA);
    } else {
      // Disable layered window
      SetWindowLong(pimpl_->hwnd_, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
    }
  }
}

float Window::GetOpacity() const {
  if (!pimpl_->hwnd_)
    return 1.0f;

  LONG exStyle = GetWindowLong(pimpl_->hwnd_, GWL_EXSTYLE);
  if (exStyle & WS_EX_LAYERED) {
    BYTE alpha;
    if (GetLayeredWindowAttributes(pimpl_->hwnd_, nullptr, &alpha, nullptr)) {
      return alpha / 255.0f;
    }
  }
  return 1.0f;
}

bool Window::SetVisualEffect(VisualEffect effect) {
  HWND hwnd = pimpl_->hwnd_;
  if (!hwnd || !IsWindow(hwnd))
    return false;

  const VisualEffectRecipe recipe = RecipeFor(effect);
  if (effect != VisualEffect::None && !recipe.IsAvailable())
    return false;

  // The backdrop first: it is the step that can fail, and nothing has changed yet if
  // it does. Versions without system backdrops reject the attribute, which only
  // matters when one was asked for.
  int backdrop = recipe.backdrop;
  const HRESULT result =
      DwmSetWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdrop, sizeof(backdrop));
  if (FAILED(result) && recipe.backdrop != kBackdropNone)
    return false;

  if (effect == VisualEffect::None) {
    RemovePropW(hwnd, kVisualEffectProperty);
    ApplyBackgroundAccent(hwnd);
  } else {
    SetPropW(hwnd, kVisualEffectProperty,
             reinterpret_cast<HANDLE>(static_cast<uintptr_t>(effect)));
    // An acrylic accent with no color at all is not drawn on Windows 10; the faintest is.
    SetAccentPolicy(hwnd, recipe.accent,
                    recipe.accent == kAccentEnableAcrylicBlurBehind ? 0x01000000 : 0, 0);
  }
  UpdateFrameExtent(hwnd);
  InvalidateRect(hwnd, nullptr, TRUE);
  return true;
}

VisualEffect Window::GetVisualEffect() const {
  if (!pimpl_->hwnd_)
    return VisualEffect::None;
  return static_cast<VisualEffect>(
      reinterpret_cast<uintptr_t>(GetPropW(pimpl_->hwnd_, kVisualEffectProperty)));
}

bool Window::IsVisualEffectSupported(VisualEffect effect) {
  return effect == VisualEffect::None || RecipeFor(effect).IsAvailable();
}

void Window::SetBackgroundColor(const Color& color) {
  if (!pimpl_->hwnd_)
    return;

  if (color.a < 255) {
    // A brush cannot be translucent. The compositor draws the color instead, behind
    // whatever the window and its children leave transparent - a Flutter view clears
    // to transparent, so this is all it takes to see the desktop through it.
    const uintptr_t stored = (uintptr_t{1} << 32) | (static_cast<uintptr_t>(color.a) << 24) |
                             (static_cast<uintptr_t>(color.r) << 16) |
                             (static_cast<uintptr_t>(color.g) << 8) | color.b;
    SetPropW(pimpl_->hwnd_, kTranslucentBackgroundProperty, reinterpret_cast<HANDLE>(stored));
    RemovePropW(pimpl_->hwnd_, kBackgroundColorProperty);
    UpdateFrameExtent(pimpl_->hwnd_);
    // While a visual effect is active the accent is its; the color waits in the property.
    if (!GetPropW(pimpl_->hwnd_, kVisualEffectProperty))
      ApplyBackgroundAccent(pimpl_->hwnd_);
    InvalidateRect(pimpl_->hwnd_, nullptr, TRUE);
    return;
  }
  if (GetPropW(pimpl_->hwnd_, kTranslucentBackgroundProperty)) {
    RemovePropW(pimpl_->hwnd_, kTranslucentBackgroundProperty);
    if (!GetPropW(pimpl_->hwnd_, kVisualEffectProperty))
      ApplyBackgroundAccent(pimpl_->hwnd_);
    UpdateFrameExtent(pimpl_->hwnd_);
  }

  // A window of the library's own class is painted by WindowProc, one color per window.
  if (reinterpret_cast<WNDPROC>(GetClassLongPtrW(pimpl_->hwnd_, GCLP_WNDPROC)) == WindowProc) {
    const uintptr_t stored = (uintptr_t{1} << 24) | (static_cast<uintptr_t>(color.r) << 16) |
                             (static_cast<uintptr_t>(color.g) << 8) | color.b;
    SetPropW(pimpl_->hwnd_, kBackgroundColorProperty, reinterpret_cast<HANDLE>(stored));
    InvalidateRect(pimpl_->hwnd_, nullptr, TRUE);
    return;
  }

  // Anyone else's window only has its class brush to go by.
  COLORREF colorRef = RGB(color.r, color.g, color.b);
  HBRUSH brush = CreateSolidBrush(colorRef);
  
  // Get old brush to delete it later
  HBRUSH oldBrush = reinterpret_cast<HBRUSH>(
    SetClassLongPtr(pimpl_->hwnd_, GCLP_HBRBACKGROUND, 
                    reinterpret_cast<LONG_PTR>(brush)));
  
  // Delete old brush if it's not a system brush
  if (oldBrush && oldBrush != GetStockObject(NULL_BRUSH) &&
      oldBrush != GetStockObject(WHITE_BRUSH) &&
      oldBrush != GetStockObject(BLACK_BRUSH) &&
      oldBrush != GetStockObject(GRAY_BRUSH) &&
      oldBrush != GetStockObject(LTGRAY_BRUSH) &&
      oldBrush != GetStockObject(DKGRAY_BRUSH)) {
    DeleteObject(oldBrush);
  }
  
  // Force window to redraw with new background color
  InvalidateRect(pimpl_->hwnd_, nullptr, TRUE);
}

Color Window::GetBackgroundColor() const {
  if (!pimpl_->hwnd_)
    return Color::White;

  if (HANDLE translucent = GetPropW(pimpl_->hwnd_, kTranslucentBackgroundProperty)) {
    const uintptr_t stored = reinterpret_cast<uintptr_t>(translucent);
    return Color::FromRGBA(static_cast<unsigned char>((stored >> 16) & 0xFF),
                           static_cast<unsigned char>((stored >> 8) & 0xFF),
                           static_cast<unsigned char>(stored & 0xFF),
                           static_cast<unsigned char>((stored >> 24) & 0xFF));
  }

  if (const auto stored =
          reinterpret_cast<uintptr_t>(GetPropW(pimpl_->hwnd_, kBackgroundColorProperty))) {
    return Color::FromRGBA(static_cast<unsigned char>((stored >> 16) & 0xFF),
                           static_cast<unsigned char>((stored >> 8) & 0xFF),
                           static_cast<unsigned char>(stored & 0xFF), 255);
  }

  // Get the background brush from the window class
  HBRUSH brush = reinterpret_cast<HBRUSH>(
    GetClassLongPtr(pimpl_->hwnd_, GCLP_HBRBACKGROUND));
  
  if (!brush || brush == GetStockObject(NULL_BRUSH)) {
    return Color::White;
  }
  
  // Get the brush color using GetObject
  LOGBRUSH logBrush;
  if (GetObject(brush, sizeof(LOGBRUSH), &logBrush) == 0) {
    return Color::White;
  }
  
  // Extract RGB values from COLORREF
  COLORREF colorRef = logBrush.lbColor;
  return Color::FromRGBA(
    GetRValue(colorRef),
    GetGValue(colorRef),
    GetBValue(colorRef),
    255  // Windows doesn't store alpha in solid brush
  );
}

void Window::SetVisibleOnAllWorkspaces(bool is_visible_on_all_workspaces) {
  // Windows doesn't have the same concept of workspaces as macOS
  // This would require integration with virtual desktop APIs
}

bool Window::IsVisibleOnAllWorkspaces() const {
  return false;  // Not supported on Windows by default
}

void Window::SetVisibleInTaskbar(bool is_visible_in_taskbar) {
  if (!pimpl_->hwnd_)
    return;

  if (is_visible_in_taskbar)
    RemovePropW(pimpl_->hwnd_, kHiddenFromTaskbarProperty);
  else
    SetPropW(pimpl_->hwnd_, kHiddenFromTaskbarProperty, reinterpret_cast<HANDLE>(1));
  ApplyTaskbarVisibility(pimpl_->hwnd_, is_visible_in_taskbar);
}

bool Window::IsVisibleInTaskbar() const {
  return pimpl_->hwnd_ && !GetPropW(pimpl_->hwnd_, kHiddenFromTaskbarProperty);
}

void Window::SetIgnoreMouseEvents(bool is_ignore_mouse_events) {
  if (pimpl_->hwnd_) {
    LONG exStyle = GetWindowLong(pimpl_->hwnd_, GWL_EXSTYLE);
    if (is_ignore_mouse_events) {
      exStyle |= WS_EX_TRANSPARENT;
    } else {
      exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLong(pimpl_->hwnd_, GWL_EXSTYLE, exStyle);
  }
}

bool Window::IsIgnoreMouseEvents() const {
  if (!pimpl_->hwnd_)
    return false;
  LONG exStyle = GetWindowLong(pimpl_->hwnd_, GWL_EXSTYLE);
  return (exStyle & WS_EX_TRANSPARENT) != 0;
}

void Window::SetFocusable(bool is_focusable) {
  // Windows focusability is typically controlled by window style
  // This is a simplified implementation
}

bool Window::IsFocusable() const {
  if (!pimpl_->hwnd_)
    return false;
  LONG style = GetWindowLong(pimpl_->hwnd_, GWL_STYLE);
  return (style & WS_DISABLED) == 0;
}

// Hands the mouse gesture in progress to the system frame, as if the press had landed on the
// given non-client area (HTCAPTION moves, HTLEFT... resize).
static void StartSystemFrameDrag(HWND hwnd, WPARAM hit_test) {
  // Gesture recognizers can report a drag start after the button is already up (a plain
  // click); the modal loop entered then would glue the window to the cursor until the next
  // click.
  const int primary = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
  if ((GetAsyncKeyState(primary) & 0x8000) == 0) {
    return;
  }
  // The caller is inside a mouse-down handler, which typically holds capture (Flutter's view
  // does); release it, or the system frame never gets the drag.
  ReleaseCapture();
  POINT cursor;
  GetCursorPos(&cursor);
  PostMessage(hwnd, WM_NCLBUTTONDOWN, hit_test, MAKELPARAM(cursor.x, cursor.y));
}

void Window::StartDragging() {
  if (pimpl_->hwnd_) {
    StartSystemFrameDrag(pimpl_->hwnd_, HTCAPTION);
  }
}

void Window::StartResizing(ResizeEdge edge) {
  if (!pimpl_->hwnd_) {
    return;
  }
  WPARAM hit_test;
  switch (edge) {
    case ResizeEdge::Top:
      hit_test = HTTOP;
      break;
    case ResizeEdge::Left:
      hit_test = HTLEFT;
      break;
    case ResizeEdge::Right:
      hit_test = HTRIGHT;
      break;
    case ResizeEdge::Bottom:
      hit_test = HTBOTTOM;
      break;
    case ResizeEdge::TopLeft:
      hit_test = HTTOPLEFT;
      break;
    case ResizeEdge::TopRight:
      hit_test = HTTOPRIGHT;
      break;
    case ResizeEdge::BottomLeft:
      hit_test = HTBOTTOMLEFT;
      break;
    case ResizeEdge::BottomRight:
    default:
      hit_test = HTBOTTOMRIGHT;
      break;
  }
  StartSystemFrameDrag(pimpl_->hwnd_, hit_test);
}

WindowId Window::GetId() const {
  if (!pimpl_) {
    return IdAllocator::kInvalidId;
  }
  return pimpl_->window_id_;
}

void* Window::GetNativeObjectInternal() const {
  return pimpl_ ? reinterpret_cast<void*>(pimpl_->hwnd_) : nullptr;
}

bool Window::SetShape(std::shared_ptr<WindowShape> shape) {
  HWND hwnd = pimpl_->hwnd_;
  if (!IsWindow(hwnd)) return false;
  if (!shape) {
    if (!SetWindowRgn(hwnd, nullptr, TRUE)) return false;
    if (HasShadow() && GetTitleBarStyle() == TitleBarStyle::Hidden) shape_shadow::Refresh(hwnd);
    else shape_shadow::Clear(hwnd);
    // A changed top-level region can invalidate child composition surfaces
    // without requesting their paint (e.g. Flutter's view). Repaint the whole
    // hierarchy so retained content is visible without another user interaction.
    // Let WM_PAINT run normally: a synchronous paint here can precede the
    // embedding framework's pending layout/frame update.
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    return true;
  }
  if (shape->GetPointCount() < 3 || GetTitleBarStyle() != TitleBarStyle::Hidden) return false;
  const double scale = GetScaleFactorForWindow(hwnd);
  RECT frame;
  POINT origin = {0, 0};
  if (!GetWindowRect(hwnd, &frame) || !ClientToScreen(hwnd, &origin)) return false;
  std::vector<POINT> points;
  for (size_t i = 0; i < shape->GetPointCount(); ++i) {
    const auto p = shape->GetPointAt(i);
    points.push_back({static_cast<LONG>(std::lround(p.x * scale)) + origin.x - frame.left,
                      static_cast<LONG>(std::lround(p.y * scale)) + origin.y - frame.top});
  }
  HRGN region = CreatePolygonRgn(points.data(), static_cast<int>(points.size()), ALTERNATE);
  if (!region) return false;
  if (!SetWindowRgn(hwnd, region, TRUE)) {
    DeleteObject(region);
    return false;
  }
  // The system owns region after a successful SetWindowRgn. Read back a copy
  // rather than using that transferred handle to generate the soft shadow.
  if (HasShadow()) {
    HRGN copy = CreateRectRgn(0, 0, 0, 0);
    if (copy && GetWindowRgn(hwnd, copy) != ERROR) shape_shadow::Update(hwnd, copy, scale);
    if (copy) DeleteObject(copy);
  }
  RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
  return true;
}

bool Window::IsShaped() const {
  if (!IsWindow(pimpl_->hwnd_)) return false;
  HRGN region = CreateRectRgn(0, 0, 0, 0);
  if (!region) return false;
  const int result = GetWindowRgn(pimpl_->hwnd_, region);
  DeleteObject(region);
  return result != ERROR;
}

bool Window::IsShapeSupported() { return true; }

bool Window::SetInputShape(std::shared_ptr<WindowShape> shape) { return false; }
bool Window::IsInputShaped() const { return false; }
bool Window::IsInputShapeSupported() { return false; }

}  // namespace nativeapi

namespace nativeapi {
bool Window::SetTitleBarColors(const Color& background, const Color& foreground) {
#ifdef NATIVEAPI_ENABLE_WINUI3
  return SetWinUI3TitleBarColors(pimpl_->hwnd_, background, foreground);
#else
  return false;
#endif
}
bool Window::ResetTitleBarColors() {
#ifdef NATIVEAPI_ENABLE_WINUI3
  return ResetWinUI3TitleBarColors(pimpl_->hwnd_);
#else
  return false;
#endif
}
}
