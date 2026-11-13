#include "../../foundation/dispatcher_common.h"
#include "../../foundation/dispatcher_platform.h"

#include "ui_thread_windows.h"

#include <windows.h>

#include <deque>
#include <mutex>

namespace nativeapi {
namespace dispatcher_platform {

namespace {

// One wake-up for a bounded batch of queued callbacks.
constexpr UINT kDispatchMessage = WM_APP + 0x51;
constexpr wchar_t kDispatchWindowClass[] = L"NativeApiDispatcherWindow";

HWND g_dispatch_window = nullptr;
std::once_flag g_dispatch_window_once;

// The Win32 id of the thread that loaded this library, or of the one SetMainThread()
// declared. dispatcher_common.h holds the same thread as a std::thread::id; this
// copy can be compared with the ids Win32 hands out.
DWORD g_captured_thread_id = GetCurrentThreadId();
// SetMainThread() is an explicit statement about the host's loop and wins over the
// detection below.
bool g_main_thread_declared = false;

/**
 * The thread that owns this process's windows, when it is not the captured thread.
 *
 * Win32 only reads a thread's message queue on that thread, and only the thread
 * that owns a window is dispatched to for it. A Flutter app built with the default
 * thread policy loads this library from its UI isolate, which never reads a Win32
 * message, so deferring to it would lose the delivery without a trace. The
 * window-owning thread is where that work has to run instead; with no window of
 * this process yet, the captured thread is all there is.
 */
DWORD UiThreadIfNotCaptured() {
  if (g_main_thread_declared) {
    return 0;
  }
  const DWORD ui_thread = detail::FindUiThreadId();
  return ui_thread && ui_thread != g_captured_thread_id ? ui_thread : 0;
}

struct DispatchQueue {
  std::mutex mutex;
  std::deque<std::function<void()>> tasks;
  bool wake_pending = false;
};
DispatchQueue& WorkQueue() {
  // C ABI finalizers may still post during static teardown. Match the lifetime
  // of the process-owned dispatch window, which is not destroyed by this module.
  static auto* queue = new DispatchQueue();
  return *queue;
}
LRESULT CALLBACK DispatchWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg != kDispatchMessage)
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  auto& queue = WorkQueue();
  LARGE_INTEGER frequency, started, now;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&started);
  for (;;) {
    std::function<void()> work;
    {
      std::lock_guard<std::mutex> lock(queue.mutex);
      if (queue.tasks.empty()) {
        queue.wake_pending = false;
        return 0;
      }
      work = std::move(queue.tasks.front());
      queue.tasks.pop_front();
    }
    // Never run or destroy a client callback under the queue lock.
    try {
      work();
    } catch (...) {
    }
    work = {};
    QueryPerformanceCounter(&now);
    if (now.QuadPart - started.QuadPart < frequency.QuadPart / 500)
      continue;
    {
      std::lock_guard<std::mutex> lock(queue.mutex);
      if (queue.tasks.empty()) {
        queue.wake_pending = false;
        return 0;
      }
      if (PostMessageW(hwnd, kDispatchMessage, 0, 0))
        return 0;
    }
    // If the OS queue is full, finish accepted work rather than strand it.
    started = now;
  }
}

/**
 * Creates the message-only window that receives dispatched work.
 *
 * MUST run on the main thread: a window's messages are delivered to the message
 * queue of the thread that created it, so creating it anywhere else would defeat
 * the entire purpose. Callers are responsible for the thread check.
 *
 * Deliberately lazy rather than created during static initialization — under a
 * DLL build that would run inside the loader lock, where creating a window can
 * deadlock.
 */
void EnsureDispatchWindowOnMainThread() {
  std::call_once(g_dispatch_window_once, [] {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DispatchWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kDispatchWindowClass;
    RegisterClassExW(&wc);

    g_dispatch_window = CreateWindowExW(0, kDispatchWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                        nullptr, wc.hInstance, nullptr);
  });
}

}  // namespace

bool PlatformIsMainThread() {
  const DWORD ui_thread = UiThreadIfNotCaptured();
  if (ui_thread) {
    return GetCurrentThreadId() == ui_thread;
  }
  const bool is_main = dispatcher_internal::IsMainThreadByCapturedId();
  if (is_main) {
    // Prime the dispatch window while we are provably on the right thread, so
    // that a later post from a worker thread has somewhere to go.
    EnsureDispatchWindowOnMainThread();
  }
  return is_main;
}

void PlatformSetMainThread() {
  dispatcher_internal::CaptureCallerAsMainThread();
  g_captured_thread_id = GetCurrentThreadId();
  g_main_thread_declared = true;
  EnsureDispatchWindowOnMainThread();
}

bool PlatformIsMainThreadDispatchSupported() {
  return true;
}

bool PlatformRunOnMainThread(std::function<void()> fn) {
  if (!fn) {
    return true;
  }

  // The library was loaded on a thread that Win32 never dispatches to: queue the
  // work where the message loop actually runs.
  const DWORD ui_thread = UiThreadIfNotCaptured();
  if (ui_thread) {
    return detail::PostToThread(ui_thread, std::move(fn));
  }

  if (dispatcher_internal::IsMainThreadByCapturedId()) {
    EnsureDispatchWindowOnMainThread();
  }

  if (!g_dispatch_window) {
    // A worker thread asked to dispatch before the main thread ever touched the
    // library, so there is no window yet and we must not create one here.
    // Report the failure instead of silently dropping the work; callers should
    // call SetMainThread() during startup to make this impossible.
    return false;
  }

  auto& queue = WorkQueue();
  std::function<void()> rejected;
  {
    std::lock_guard<std::mutex> lock(queue.mutex);
    queue.tasks.push_back(std::move(fn));
    if (queue.wake_pending)
      return true;
    if (PostMessageW(g_dispatch_window, kDispatchMessage, 0, 0)) {
      queue.wake_pending = true;
      return true;
    }
    rejected = std::move(queue.tasks.back());
    queue.tasks.pop_back();
  }
  return false;
}

bool PlatformRunMainThreadLoopFor(int timeout_ms) {
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(timeout_ms);
  MSG msg;
  for (;;) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (timeout_ms <= 0 || GetTickCount() >= deadline) {
      return true;
    }
    // Sleep until a message arrives or the budget expires, whichever is first.
    MsgWaitForMultipleObjects(0, nullptr, FALSE, deadline - GetTickCount(), QS_ALLINPUT);
  }
}

}  // namespace dispatcher_platform
}  // namespace nativeapi
