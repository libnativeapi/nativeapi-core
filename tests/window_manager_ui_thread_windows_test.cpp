// WindowManager first touched from a thread that never reads Win32 messages — the
// UI isolate of a Flutter app built with the default thread policy — still observes
// the windows another thread owns: the install is marshaled there. Real desktop,
// real message loop, no input.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include "nativeapi.h"

namespace {

int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}

// Events are delivered on the window-owning thread; the assertions below run on the
// thread that only ever waits for them.
std::mutex g_mutex;
std::condition_variable g_received;
nativeapi::WindowId g_created = 0;
nativeapi::WindowId g_resized = 0;
nativeapi::WindowId g_closed = 0;

template <typename Predicate>
bool WaitFor(Predicate predicate, int timeout_ms = 5000) {
  std::unique_lock<std::mutex> lock(g_mutex);
  return g_received.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
}

// The id the manager reports for a native window, read from the thread under test.
nativeapi::WindowId IdOf(HWND hwnd) {
  for (const auto& window : nativeapi::WindowManager::GetInstance().GetAll()) {
    if (window->GetNativeObject() == hwnd) {
      return window->GetId();
    }
  }
  return 0;
}

}  // namespace

int main() {
  std::mutex step_mutex;
  std::condition_variable step_changed;
  int step = 0;
  auto Advance = [&](int next) {
    {
      std::lock_guard<std::mutex> lock(step_mutex);
      step = next;
    }
    step_changed.notify_all();
  };

  HWND window_a = nullptr;
  HWND window_b = nullptr;
  std::atomic<bool> platform_ready{false};
  std::atomic<DWORD> platform_thread{0};

  // The platform thread: owns the windows, runs the message loop Win32 dispatches
  // to, and never calls the nativeapi API first.
  std::thread platform([&] {
    platform_thread = GetCurrentThreadId();
    WNDCLASSW cls = {};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"NativeApiUiThreadTestWindow";
    RegisterClassW(&cls);

    window_a = CreateWindowW(cls.lpszClassName, L"nativeapi ui thread A", WS_OVERLAPPEDWINDOW, 40,
                             40, 300, 200, nullptr, nullptr, cls.hInstance, nullptr);
    ShowWindow(window_a, SW_SHOWNORMAL);
    platform_ready = true;

    bool created_b = false, resized_a = false;
    for (;;) {
      MSG message;
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      int current = 0;
      {
        std::lock_guard<std::mutex> lock(step_mutex);
        current = step;
      }
      if (current == 4) {
        break;
      }
      if (current == 1 && !created_b) {
        window_b = CreateWindowW(cls.lpszClassName, L"nativeapi ui thread B", WS_OVERLAPPEDWINDOW,
                                 400, 40, 300, 200, nullptr, nullptr, cls.hInstance, nullptr);
        ShowWindow(window_b, SW_SHOWNORMAL);
        created_b = true;
      } else if (current == 2 && !resized_a) {
        SetWindowPos(window_a, nullptr, 60, 60, 360, 260, SWP_NOZORDER);
        resized_a = true;
      } else if (current == 3) {
        DestroyWindow(window_a);
        window_a = nullptr;
      }
      Sleep(1);
    }
    if (window_b) {
      DestroyWindow(window_b);
    }
  });

  while (!platform_ready.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  Check(platform_thread.load() != GetCurrentThreadId(), "the UI isolate is not the window thread");

  // First touch of the manager happens here, away from the window thread. It has to
  // end up observing from the thread the windows belong to.
  auto& manager = nativeapi::WindowManager::GetInstance();
  manager.AddListener<nativeapi::WindowCreatedEvent>([](const auto& event) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_created = event.GetWindowId();
    }
    g_received.notify_all();
  });
  manager.AddListener<nativeapi::WindowResizedEvent>([](const auto& event) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_resized = event.GetWindowId();
    }
    g_received.notify_all();
  });
  manager.AddListener<nativeapi::WindowClosedEvent>([](const auto& event) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_closed = event.GetWindowId();
    }
    g_received.notify_all();
  });

  // Show/hide are WinEvents: they only arrive where the hook can be delivered.
  Advance(1);
  const bool created = WaitFor([] { return g_created != 0; });
  Check(created, "WindowCreatedEvent reaches the UI isolate thread");
  const nativeapi::WindowId id_a = IdOf(window_a);
  const nativeapi::WindowId id_b = created ? IdOf(window_b) : 0;
  Check(created && g_created == id_b, "the created window is the one that was shown");
  const auto current = manager.GetCurrent();
  Check(current && (current->GetId() == id_a || current->GetId() == id_b),
        "GetCurrent resolves the process window from a non-UI thread");
  for (const auto& listed : manager.GetAll()) {
    DWORD listed_pid = 0;
    GetWindowThreadProcessId(static_cast<HWND>(listed->GetNativeObject()), &listed_pid);
    Check(listed_pid == GetCurrentProcessId(), "GetAll only returns windows of this process");
  }

  // Moved/resized come from the subclass, which only the owning thread can install.
  Advance(2);
  Check(WaitFor([] { return g_resized != 0; }), "WindowResizedEvent reaches the UI isolate thread");
  Check(g_resized == id_a, "the resized window is the one that changed");

  Advance(3);
  Check(WaitFor([] { return g_closed != 0; }), "WindowClosedEvent reaches the UI isolate thread");
  Check(g_closed == id_a, "the closed window is the one that was destroyed");

  Advance(4);
  platform.join();

  // A window created hidden on the platform thread (the Flutter runner's
  // anti-white-flash startup pattern) is still found by GetCurrent() from a
  // non-UI thread before it is shown, and keeps that ID once shown.
  std::atomic<HWND> hidden_hwnd{nullptr};
  std::atomic<bool> show_hidden{false}, close_hidden{false};
  std::thread hidden_platform([&] {
    WNDCLASSW cls = {};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"NativeApiUiThreadHiddenTestWindow";
    RegisterClassW(&cls);
    HWND hwnd = CreateWindowW(cls.lpszClassName, L"nativeapi hidden startup", WS_OVERLAPPEDWINDOW,
                              80, 80, 300, 200, nullptr, nullptr, cls.hInstance, nullptr);
    hidden_hwnd = hwnd;
    bool shown = false;
    while (!close_hidden.load()) {
      MSG message;
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      if (show_hidden.load() && !shown) {
        ShowWindow(hwnd, SW_SHOWNORMAL);
        shown = true;
      }
      Sleep(1);
    }
    DestroyWindow(hwnd);
  });
  while (!hidden_hwnd.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto hidden_current = manager.GetCurrent();
  Check(hidden_current && hidden_current->GetNativeObject() == hidden_hwnd.load(),
        "GetCurrent resolves the process's hidden window from a non-UI thread");
  show_hidden = true;
  while (!IsWindowVisible(hidden_hwnd.load())) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto shown_current = manager.GetCurrent();
  Check(shown_current && hidden_current && shown_current->GetId() == hidden_current->GetId(),
        "the hidden window wrapped earlier keeps its ID once shown");
  close_hidden = true;
  hidden_platform.join();

  std::cout << (failures ? "FAILED " : "OK ") << failures << " failure(s)" << std::endl;
  return failures ? 1 : 0;
}
