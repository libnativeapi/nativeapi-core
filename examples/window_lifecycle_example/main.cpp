#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "nativeapi.h"

// Only the desktop platforms have a native window to close; iOS, Android and
// OpenHarmony (the latter two also define __linux__) build it as a no-op.
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_OSX
#define NATIVEAPI_EXAMPLE_COCOA 1
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)
#define NATIVEAPI_EXAMPLE_GTK 1
#include <gtk/gtk.h>
#endif

using nativeapi::Window;
using nativeapi::WindowClosedEvent;
using nativeapi::WindowCreatedEvent;
using nativeapi::WindowEvent;
using nativeapi::WindowId;
using nativeapi::WindowManager;

// Window has no Close() yet, so the native window is closed the way its owner
// (an embedding framework, the user) would.
static void CloseNatively(const std::shared_ptr<Window>& window) {
  void* native = window->GetNativeObject();
#if defined(NATIVEAPI_EXAMPLE_COCOA)
  [(__bridge NSWindow*)native close];
#elif defined(_WIN32)
  DestroyWindow(static_cast<HWND>(native));
#elif defined(NATIVEAPI_EXAMPLE_GTK)
  gtk_widget_destroy(GTK_WIDGET(native));
#else
  (void)native;
#endif
}

// Shows when WindowCreatedEvent and WindowClosedEvent are emitted, and checks it:
// the exit code is the number of expectations that did not hold.
//
// It drives the windows itself and needs no input.
int main() {
  nativeapi::SetMainThread();
  WindowManager& window_manager = WindowManager::GetInstance();

  std::map<WindowId, int> created;
  std::map<WindowId, int> closed;
  window_manager.AddListener<WindowEvent>([&](const WindowEvent& event) {
    std::cout << "  event: " << event.GetTypeName() << " window=" << event.GetWindowId()
              << std::endl;
    if (dynamic_cast<const WindowCreatedEvent*>(&event)) {
      created[event.GetWindowId()]++;
    } else if (dynamic_cast<const WindowClosedEvent*>(&event)) {
      closed[event.GetWindowId()]++;
    }
  });

  int failures = 0;
  auto expect = [&](const std::string& what, int actual, int expected) {
    bool ok = actual == expected;
    std::cout << (ok ? "PASS " : "FAIL ") << what << ": " << actual << " (expected " << expected
              << ")" << std::endl;
    if (!ok) {
      failures++;
    }
  };

  std::shared_ptr<Window> shown;
  std::shared_ptr<Window> never_shown;
  WindowId shown_id = 0;
  WindowId never_shown_id = 0;

  // Each step runs on the main thread, inside the real application loop, and
  // the loop then gets some time to deliver what the step caused.
  std::vector<std::function<void()>> steps = {
      [&] {
        std::cout << "create a window without showing it" << std::endl;
        shown = std::make_shared<Window>();
        shown->SetTitle("Lifecycle - shown");
        shown->SetSize({480, 320}, false);
        shown->Center();
        shown_id = shown->GetId();
      },
      [&] {
        expect("created before the first show", created[shown_id], 0);
        std::cout << "show it" << std::endl;
        shown->Show();
      },
      [&] {
        expect("created after the first show", created[shown_id], 1);
        std::cout << "hide it" << std::endl;
        shown->Hide();
      },
      [&] {
        std::cout << "show it again" << std::endl;
        shown->Show();
      },
      [&] {
        expect("created after the second show", created[shown_id], 1);
        expect("closed after hiding", closed[shown_id], 0);
        std::cout << "create a second window and close it without ever showing it" << std::endl;
        never_shown = std::make_shared<Window>();
        never_shown->SetTitle("Lifecycle - never shown");
        never_shown_id = never_shown->GetId();
        CloseNatively(never_shown);
      },
      [&] {
        expect("created for the window never shown", created[never_shown_id], 0);
        expect("closed for the window never shown", closed[never_shown_id], 0);
        std::cout << "close the first window" << std::endl;
        CloseNatively(shown);
      },
      [&] {
        expect("closed after closing", closed[shown_id], 1);
        expect("created after closing", created[shown_id], 1);
        std::cout << (failures == 0 ? "ALL PASS" : "FAILED") << std::endl;
        // Not Quit(): how a platform's loop ends must not decide the exit code.
        std::cout.flush();
        std::_Exit(failures);
      },
  };

  std::thread driver([&] {
    for (auto& step : steps) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      nativeapi::RunOnMainThread(step);
    }
  });
  driver.detach();

  // On Windows destroying any window of the library ends the loop (its window
  // procedure posts WM_QUIT), so enter it again: the last step ends the process.
  while (true) {
    nativeapi::Application::GetInstance().Run();
  }
}
