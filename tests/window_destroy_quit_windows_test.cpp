// Verifies that destroying a secondary/transient NativeAPI Window on Windows
// does not post WM_QUIT, while closing the primary window of Application::Run(window)
// still exits its message loop cleanly.
#include <windows.h>
#include <iostream>
#include <memory>
#include "nativeapi.h"

namespace {
int failures = 0;

void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}

bool HasQueuedQuitMessage() {
  MSG msg{};
  return PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE) != FALSE;
}

void DrainQuitMessages() {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE) {
  }
}
}  // namespace

int main() {
  // 1. Destroying a Window outside Application::Run must not post WM_QUIT.
  {
    auto transient = std::make_shared<nativeapi::Window>();
    HWND hwnd = static_cast<HWND>(transient->GetNativeObject());
    Check(hwnd != nullptr && IsWindow(hwnd), "transient window created");
    Check(transient->Close(), "transient window Close() succeeds");
    Check(!IsWindow(hwnd), "transient window HWND destroyed");
    Check(!HasQueuedQuitMessage(), "destroying standalone window does not post WM_QUIT");
    DrainQuitMessages();
  }

  auto& app = nativeapi::Application::GetInstance();

  // 2. During Application::Run() (windowless / multi-window), closing a window
  //    must not exit the loop; only Application::Quit() exits with its code.
  {
    bool survived_window_close = false;
    auto window = std::make_shared<nativeapi::Window>();
    HWND hwnd = static_cast<HWND>(window->GetNativeObject());
    Check(hwnd != nullptr && IsWindow(hwnd), "Run() window created");

    nativeapi::RunOnMainThread([&] {
      Check(window->Close(), "window closed inside Run()");
      Check(!IsWindow(hwnd), "window HWND destroyed inside Run()");
      Check(!HasQueuedQuitMessage(), "closing window inside Run() does not post WM_QUIT");
      nativeapi::RunOnMainThread([&] {
        survived_window_close = true;
        app.Quit(7);
      });
    });

    const int exit_code = app.Run();
    Check(survived_window_close, "Run() loop kept pumping after window closed");
    Check(exit_code == 7, "Run() returned Quit() exit code (7)");
    DrainQuitMessages();
  }

  // 3. During Application::Run(primary), closing a secondary window must not
  //    exit the loop, whereas closing primary exits cleanly with code 0.
  {
    bool survived_secondary_close = false;
    auto primary = std::make_shared<nativeapi::Window>();
    primary->SetNonActivating(true);
    primary->SetBounds({-32000.0, -32000.0, 120.0, 80.0});
    auto secondary = std::make_shared<nativeapi::Window>();
    HWND primary_hwnd = static_cast<HWND>(primary->GetNativeObject());
    HWND secondary_hwnd = static_cast<HWND>(secondary->GetNativeObject());
    Check(primary_hwnd != nullptr && secondary_hwnd != nullptr,
          "primary and secondary windows created");

    nativeapi::RunOnMainThread([&] {
      Check(secondary->Close(), "secondary window closed inside Run(primary)");
      Check(!IsWindow(secondary_hwnd), "secondary HWND destroyed inside Run(primary)");
      Check(!HasQueuedQuitMessage(),
            "closing secondary window inside Run(primary) does not post WM_QUIT");
      nativeapi::RunOnMainThread([&] {
        survived_secondary_close = true;
        Check(primary->Close(), "primary window closed inside Run(primary)");
      });
    });

    const int exit_code = app.Run(primary);
    Check(survived_secondary_close, "Run(primary) survived secondary window close");
    Check(!IsWindow(primary_hwnd), "primary HWND destroyed");
    Check(exit_code == 0, "Run(primary) exited with 0 when primary closed");
    DrainQuitMessages();
  }

  return failures ? 1 : 0;
}
