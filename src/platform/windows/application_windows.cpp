// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dwmapi.h>
// clang-format on
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../../application.h"
#include "../../foundation/dispatcher.h"
#include "../../menu.h"
#include "../../window_manager.h"
#include "application_theme_windows.h"
#include "string_utils_windows.h"

#pragma comment(lib, "dwmapi.lib")

// DWMWA_USE_IMMERSIVE_DARK_MODE was 19 before Windows 10 20H1 and is 20 since.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19

namespace nativeapi {

// Renders a small red circle with white text, suitable for
// ITaskbarList3::SetOverlayIcon. The caller owns the returned HICON.
static HICON CreateBadgeIcon(const std::wstring& text) {
  const int size = GetSystemMetrics(SM_CXSMICON) > 0 ? GetSystemMetrics(SM_CXSMICON) : 16;

  HDC screen_dc = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen_dc);

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = size;
  bmi.bmiHeader.biHeight = -size;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP color = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!color || !bits) {
    if (color)
      DeleteObject(color);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen_dc);
    return nullptr;
  }
  HGDIOBJ old_bitmap = SelectObject(dc, color);

  // Background: the DIB starts zeroed, i.e. transparent black.
  HBRUSH brush = CreateSolidBrush(RGB(0xE5, 0x39, 0x35));
  HPEN pen = static_cast<HPEN>(GetStockObject(NULL_PEN));
  HGDIOBJ old_brush = SelectObject(dc, brush);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  Ellipse(dc, 0, 0, size + 1, size + 1);

  const int glyphs = static_cast<int>(text.size());
  const int font_height = -(size * (glyphs > 2 ? 5 : 7) / 10);
  HFONT font = CreateFontW(font_height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  HGDIOBJ old_font = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(0xFF, 0xFF, 0xFF));
  RECT text_rect = {0, 0, size, size};
  DrawTextW(dc, text.c_str(), -1, &text_rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);

  // GDI leaves the alpha channel at zero; anything that was painted is opaque.
  uint32_t* pixels = static_cast<uint32_t*>(bits);
  for (int i = 0; i < size * size; ++i) {
    if (pixels[i] & 0x00FFFFFF) {
      pixels[i] |= 0xFF000000;
    }
  }

  SelectObject(dc, old_font);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_bitmap);
  DeleteObject(font);
  DeleteObject(brush);

  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  ICONINFO icon_info = {};
  icon_info.fIcon = TRUE;
  icon_info.hbmMask = mask;
  icon_info.hbmColor = color;
  HICON icon = CreateIconIndirect(&icon_info);

  DeleteObject(mask);
  DeleteObject(color);
  DeleteDC(dc);
  ReleaseDC(nullptr, screen_dc);
  return icon;
}

// Reads the user's system-wide light/dark preference.
static bool SystemPrefersDark() {
  DWORD light = 1;
  DWORD size = sizeof(light);
  LSTATUS status = RegGetValueW(
      HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
  return status == ERROR_SUCCESS && light == 0;
}

class Application::Impl {
 public:
  Impl(Application* app) : app_(app), hinstance_(GetModuleHandle(nullptr)) {}
  ~Impl() {
    if (badge_icon_) {
      DestroyIcon(badge_icon_);
    }
    if (taskbar_) {
      taskbar_->Release();
    }
  }

  bool Initialize() {
    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
      return false;
    }

    return true;
  }

  int Run() {
    MSG msg = {};
    int exit_code = 0;

    while (true) {
      // Get message from the message queue
      BOOL result = GetMessage(&msg, nullptr, 0, 0);

      if (result == -1) {
        // Error occurred
        exit_code = -1;
        break;
      } else if (result == 0) {
        // WM_QUIT received
        exit_code = static_cast<int>(msg.wParam);
        break;
      } else {
        // Translate and dispatch the message
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }

    return exit_code;
  }

  int Run(std::shared_ptr<Window> window) {
    if (!window) {
      return -1;
    }

    // Set the window as primary window
    app_->SetPrimaryWindow(window);

    // Show the window
    window->Show();
    window->Focus();

    // Start the message loop
    MSG msg = {};
    int exit_code = 0;

    while (true) {
      // Get message from the message queue
      BOOL result = GetMessage(&msg, nullptr, 0, 0);

      if (result == -1) {
        // Error occurred
        exit_code = -1;
        break;
      } else if (result == 0) {
        // WM_QUIT received
        exit_code = static_cast<int>(msg.wParam);
        break;
      } else {
        // Translate and dispatch the message
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }

    return exit_code;
  }

  void Quit(int exit_code) {
    if (!app_->running_) {
      // Someone else runs the loop (a Flutter runner, a host pumping it by
      // hand): Run() will not return to emit this, and the host ends the
      // process on WM_QUIT.
      ApplicationExitingEvent event(exit_code);
      app_->Emit(event);
    }
    PostQuitMessage(exit_code);
  }

  bool SetIcon(const std::string& icon_path) {
    if (icon_path.empty()) {
      return false;
    }

    // Convert to wide string
    std::wstring wide_path(icon_path.begin(), icon_path.end());

    // Load icon from file using LoadImageW for wide strings
    HICON icon = static_cast<HICON>(
        LoadImageW(nullptr, wide_path.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));

    if (!icon) {
      return false;
    }

    // Set application icon
    SetClassLongPtr(GetConsoleWindow(), GCLP_HICON, reinterpret_cast<LONG_PTR>(icon));

    return true;
  }

  bool SetDockIconVisible(bool visible) {
    // Windows doesn't have a dock, so this is a no-op
    return true;
  }

  bool SetProgressBar(double progress) {
    HWND hwnd = TaskbarTargetWindow();
    ITaskbarList3* taskbar = Taskbar();
    if (!hwnd || !taskbar) {
      return false;
    }
    HRESULT hr;
    if (progress < 0) {
      hr = taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
    } else if (progress > 1) {
      hr = taskbar->SetProgressState(hwnd, TBPF_INDETERMINATE);
    } else {
      hr = taskbar->SetProgressState(hwnd, TBPF_NORMAL);
      if (SUCCEEDED(hr)) {
        hr = taskbar->SetProgressValue(hwnd, static_cast<ULONGLONG>(progress * 100), 100);
      }
    }
    return SUCCEEDED(hr);
  }

  bool SetBadgeLabel(const std::string& label) {
    HWND hwnd = TaskbarTargetWindow();
    ITaskbarList3* taskbar = Taskbar();
    if (!hwnd || !taskbar) {
      return false;
    }

    HICON icon = nullptr;
    std::wstring description;
    if (!label.empty()) {
      description = StringToWString(label);
      icon = CreateBadgeIcon(description.substr(0, 3));
      if (!icon) {
        return false;
      }
    }

    HRESULT hr = taskbar->SetOverlayIcon(hwnd, icon, icon ? description.c_str() : nullptr);

    // The taskbar copies the icon, so the previous one can go now.
    if (badge_icon_) {
      DestroyIcon(badge_icon_);
    }
    badge_icon_ = icon;
    return SUCCEEDED(hr);
  }

  bool SetBrightness(Brightness brightness) {
    application_brightness.store(brightness);
    BOOL dark;
    switch (brightness) {
      case Brightness::Light:
        dark = FALSE;
        break;
      case Brightness::Dark:
        dark = TRUE;
        break;
      case Brightness::System:
      default:
        dark = SystemPrefersDark() ? TRUE : FALSE;
        break;
    }

    struct ThemeUpdate {
      BOOL dark;
      bool ok = true;
    } update{dark};
    // Enumerate this application's actual top-level HWNDs directly. GetAll()
    // includes other processes and creates wrappers; message-only menu hosts
    // have no DWM frame and must not participate in this update.
    EnumWindows(
        [](HWND hwnd, LPARAM data) -> BOOL {
          DWORD process_id = 0;
          GetWindowThreadProcessId(hwnd, &process_id);
          if (process_id != GetCurrentProcessId())
            return TRUE;
          auto& update = *reinterpret_cast<ThemeUpdate*>(data);
          HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &update.dark,
                                             sizeof(update.dark));
          if (FAILED(hr)) {
            hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1,
                                       &update.dark, sizeof(update.dark));
          }
          update.ok = update.ok && SUCCEEDED(hr);
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&update));
    return update.ok;
  }

  bool SetMenuBar(std::shared_ptr<Menu> menu) {
    if (!menu) {
      return false;
    }

    // Get the primary window
    auto primary_window = app_->GetPrimaryWindow();
    if (!primary_window) {
      return false;
    }

    // Get the native window handle
    HWND hwnd = static_cast<HWND>(primary_window->GetNativeObject());
    if (!hwnd) {
      return false;
    }

    // Get the native menu handle
    HMENU hmenu = static_cast<HMENU>(menu->GetNativeObject());
    if (!hmenu) {
      return false;
    }

    // Set the menu for the window
    SetMenu(hwnd, hmenu);

    return true;
  }

  void CleanupEventMonitoring() {
    // Clean up Windows-specific event monitoring
    if (mutex_) {
      CloseHandle(mutex_);
      mutex_ = nullptr;
    }

    CoUninitialize();
  }

 private:
  Application* app_;
  HINSTANCE hinstance_;
  HANDLE mutex_ = nullptr;
  ITaskbarList3* taskbar_ = nullptr;
  HICON badge_icon_ = nullptr;

  // Taskbar progress and overlays are per window; use the primary window, or
  // the first known window when none has been designated.
  HWND TaskbarTargetWindow() {
    auto window = app_->GetPrimaryWindow();
    if (!window) {
      auto all = WindowManager::GetInstance().GetAll();
      if (!all.empty()) {
        window = all.front();
      }
    }
    if (!window) {
      return nullptr;
    }
    HWND hwnd = static_cast<HWND>(window->GetNativeObject());
    return (hwnd && IsWindow(hwnd)) ? hwnd : nullptr;
  }

  ITaskbarList3* Taskbar() {
    if (taskbar_) {
      return taskbar_;
    }
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&taskbar_));
    if (FAILED(hr) || !taskbar_) {
      taskbar_ = nullptr;
      return nullptr;
    }
    if (FAILED(taskbar_->HrInit())) {
      taskbar_->Release();
      taskbar_ = nullptr;
    }
    return taskbar_;
  }
};

Application::Application()
    : initialized_(true), running_(false), exit_code_(0), pimpl_(std::make_unique<Impl>(this)) {
  // Perform platform-specific initialization automatically
  pimpl_->Initialize();

  // Emit application started event
  Emit<ApplicationStartedEvent>();
}

Application::~Application() {
  // Clean up platform-specific event monitoring
  pimpl_->CleanupEventMonitoring();
}

int Application::Run() {
  // Run() is on the main thread by definition: prime the dispatcher here, so
  // a Quit() from a worker thread has a window to post to.
  (void)IsMainThread();
  running_ = true;

  // Start the platform-specific main event loop
  int result = pimpl_->Run();

  running_ = false;

  // Emit exit event
  Emit<ApplicationExitingEvent>(result);

  return result;
}

int Application::Run(std::shared_ptr<Window> window) {
  if (!window) {
    return -1;  // Invalid window
  }

  // See Run() above.
  (void)IsMainThread();
  running_ = true;

  // Start the platform-specific main event loop with window
  int result = pimpl_->Run(window);

  running_ = false;

  // Emit exit event
  Emit<ApplicationExitingEvent>(result);

  return result;
}

void Application::Quit(int exit_code) {
  // The loop, and every listener, lives on the main thread; a quit requested
  // from another thread is carried over there.
  if (!IsMainThread() && RunOnMainThread([this, exit_code] { Quit(exit_code); })) {
    return;
  }

  exit_code_ = exit_code;

  // A QuitRequested listener may itself call Quit(): record its exit code and
  // let the outer call finish, instead of recursing.
  static bool announcing = false;
  if (announcing) {
    return;
  }
  announcing = true;
  Emit<ApplicationQuitRequestedEvent>();
  announcing = false;

  // Request platform-specific quit, with the last exit code asked for
  pimpl_->Quit(exit_code_);
}

bool Application::IsRunning() const {
  return running_;
}

bool Application::IsSingleInstance() const {
  return false;
}

bool Application::SetIcon(const std::string& icon_path) {
  return pimpl_->SetIcon(icon_path);
}

bool Application::SetDockIconVisible(bool visible) {
  return pimpl_->SetDockIconVisible(visible);
}

bool Application::SetProgressBar(double progress) {
  return pimpl_->SetProgressBar(progress);
}

bool Application::SetBadgeLabel(const std::string& label) {
  return pimpl_->SetBadgeLabel(label);
}

bool Application::SetBrightness(Brightness brightness) {
  return pimpl_->SetBrightness(brightness);
}

bool Application::SetMenuBar(std::shared_ptr<Menu> menu) {
  return pimpl_->SetMenuBar(menu);
}

std::shared_ptr<Window> Application::GetPrimaryWindow() const {
  return primary_window_;
}

void Application::SetPrimaryWindow(std::shared_ptr<Window> window) {
  primary_window_ = window;
}

std::vector<std::shared_ptr<Window>> Application::GetAllWindows() const {
  auto& window_manager = WindowManager::GetInstance();
  return window_manager.GetAll();
}

}  // namespace nativeapi
