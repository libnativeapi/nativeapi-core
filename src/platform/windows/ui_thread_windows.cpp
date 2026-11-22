#include "ui_thread_windows.h"

#include <cwchar>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nativeapi::detail {
namespace {

// Private message that drains one thread's queue window.
constexpr UINT kTaskMessage = WM_APP + 0x52;
constexpr wchar_t kQueueWindowClass[] = L"NativeApiUiThreadQueue";
constexpr wchar_t kWindowIdProperty[] = L"NativeAPIWindowId";

// Per-thread work queue. The window belongs to the thread the queue serves, so a
// message posted to it runs on that thread. `tasks` is guarded by g_mutex.
struct TaskQueue {
  HWND window = nullptr;
  bool wake_pending = false;
  std::vector<std::function<void()>> tasks;
};

std::mutex g_mutex;
// Keyed by the id of the thread that owns the queue window.
std::unordered_map<DWORD, TaskQueue> g_queues;
// Bootstraps in flight, so two callers do not install a hook each.
std::unordered_map<DWORD, HHOOK> g_bootstraps;

// The cached answer of FindUiThreadId() and the window it was read from.
std::mutex g_ui_mutex;
DWORD g_ui_thread = 0;
HWND g_ui_anchor = nullptr;
int g_ui_rank = 0;

void RunTasks(std::vector<std::function<void()>> tasks) {
  for (auto& task : tasks) {
    try {
      task();
    } catch (...) {
    }
  }
}

LRESULT CALLBACK QueueWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message != kTaskMessage) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  std::vector<std::function<void()>> tasks;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto queue = g_queues.find(GetCurrentThreadId());
    if (queue != g_queues.end()) {
      tasks.swap(queue->second.tasks);
      queue->second.wake_pending = false;
    }
  }
  RunTasks(std::move(tasks));
  return 0;
}

HWND CreateQueueWindow() {
  static std::once_flag registered;
  std::call_once(registered, [] {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = QueueWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kQueueWindowClass;
    RegisterClassExW(&wc);
  });
  return CreateWindowExW(0, kQueueWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                         GetModuleHandleW(nullptr), nullptr);
}

// Publish the calling thread's queue window and let it drain what was queued for
// it before it existed. Runs on the thread the queue serves, possibly from inside
// a message hook, so the tasks themselves are left to an ordinary message.
void OpenQueueOnCurrentThread(DWORD thread_id) {
  HWND window = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto& queue = g_queues[thread_id];
    if (!queue.window || !IsWindow(queue.window)) {
      queue.window = CreateQueueWindow();
    }
    window = queue.window;
    if (window && !queue.wake_pending && !queue.tasks.empty())
      queue.wake_pending = PostMessageW(window, kTaskMessage, 0, 0) != FALSE;
  }
}

// One-shot WH_GETMESSAGE callback: it runs on the thread whose queue needs to be
// opened, on the next message that thread reads. That is how a worker reaches a
// thread it has no window of, without a window procedure of its own to post to.
LRESULT CALLBACK BootstrapProc(int code, WPARAM wp, LPARAM lp) {
  if (code >= 0) {
    const DWORD thread_id = GetCurrentThreadId();
    HHOOK hook = nullptr;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      auto bootstrap = g_bootstraps.find(thread_id);
      if (bootstrap != g_bootstraps.end()) {
        hook = bootstrap->second;
        g_bootstraps.erase(bootstrap);
      }
    }
    // Unhook before opening the queue: this message has served its purpose.
    if (hook) {
      UnhookWindowsHookEx(hook);
    }
    OpenQueueOnCurrentThread(thread_id);
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

struct UiThreadSearch {
  DWORD process_id = 0;
  DWORD thread_id = 0;
  HWND anchor = nullptr;
  int rank = 0;
};

// Scores how strongly |hwnd| represents the application's UI thread:
//   0: Not an application window (child window, tool window not managed by
//      nativeapi, or a hidden OS/runtime helper window such as GDI+'s
//      background-thread "GDI+ Hook Window Class").
//   1: Hidden non-tool host window with non-empty geometry (e.g. a runner's
//      main window before its initial ShowWindow).
//   2: Hidden window known to nativeapi (carries NativeAPIWindowId, including
//      launcher panels or utility windows styled with WS_EX_TOOLWINDOW).
//   3: Visible application window.
int UiWindowRank(HWND hwnd) {
  if (!hwnd || GetAncestor(hwnd, GA_ROOT) != hwnd || (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)) {
    return 0;
  }
  const bool known = GetPropW(hwnd, kWindowIdProperty) != nullptr;
  const bool visible = IsWindowVisible(hwnd) != FALSE;
  if (known) {
    return visible ? 3 : 2;
  }
  if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
    return 0;
  }
  wchar_t class_name[64] = {};
  if (GetClassNameW(hwnd, class_name, 64) > 0) {
    if (std::wcscmp(class_name, L"GDI+ Hook Window Class") == 0 ||
        std::wcscmp(class_name, L"IME") == 0 ||
        std::wcscmp(class_name, L"MSCTFIME UI") == 0 ||
        std::wcscmp(class_name, L"CicMarshalWndClass") == 0 ||
        std::wcscmp(class_name, L"OleMainThreadWndClass") == 0 ||
        std::wcscmp(class_name, L"MediaContextNotificationWindow") == 0 ||
        std::wcscmp(class_name, L"DummyDWMListenerWindow") == 0) {
      return 0;
    }
  }
  if (visible) {
    return 3;
  }
  RECT rect = {};
  if (!GetWindowRect(hwnd, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) {
    return 0;
  }
  return 1;
}

BOOL CALLBACK FindUiThreadWindow(HWND hwnd, LPARAM data) {
  auto* search = reinterpret_cast<UiThreadSearch*>(data);
  DWORD process_id = 0;
  const DWORD thread_id = GetWindowThreadProcessId(hwnd, &process_id);
  if (!thread_id || process_id != search->process_id) {
    return TRUE;
  }
  const int rank = UiWindowRank(hwnd);
  if (rank <= search->rank) {
    return TRUE;
  }
  search->thread_id = thread_id;
  search->anchor = hwnd;
  search->rank = rank;
  return rank < 3 ? TRUE : FALSE;
}

}  // namespace

DWORD FindUiThreadId() {
  std::lock_guard<std::mutex> lock(g_ui_mutex);
  if (g_ui_rank >= 2 && g_ui_anchor && IsWindow(g_ui_anchor) &&
      GetWindowThreadProcessId(g_ui_anchor, nullptr) == g_ui_thread) {
    return g_ui_thread;
  }
  g_ui_anchor = nullptr;
  g_ui_thread = 0;
  g_ui_rank = 0;

  // The foreground window is the application's own whenever it is in use, and the
  // one the user's next message goes to.
  HWND foreground = GetForegroundWindow();
  if (foreground) {
    DWORD process_id = 0;
    const DWORD thread_id = GetWindowThreadProcessId(foreground, &process_id);
    if (thread_id && process_id == GetCurrentProcessId()) {
      const int rank = UiWindowRank(foreground);
      if (rank > 0) {
        g_ui_thread = thread_id;
        g_ui_anchor = foreground;
        g_ui_rank = rank;
        return g_ui_thread;
      }
    }
  }

  UiThreadSearch search;
  search.process_id = GetCurrentProcessId();
  EnumWindows(FindUiThreadWindow, reinterpret_cast<LPARAM>(&search));
  g_ui_thread = search.thread_id;
  g_ui_anchor = search.anchor;
  g_ui_rank = search.rank;
  return g_ui_thread;
}

bool PostToThread(DWORD thread_id, std::function<void()> task) {
  if (!task) {
    return true;
  }
  if (!thread_id) {
    return false;
  }

  // Everything below, including the post itself, happens under the lock: a
  // failed post has to take its task back out of the queue, and Win32 cannot
  // re-enter this module from PostMessage / SetWindowsHookEx.
  std::lock_guard<std::mutex> lock(g_mutex);
  auto& queue = g_queues[thread_id];
  if (queue.window && !IsWindow(queue.window)) {
    queue.window = nullptr;
    queue.wake_pending = false;
  }
  queue.tasks.push_back(std::move(task));

  if (!queue.window && thread_id == GetCurrentThreadId()) {
    queue.window = CreateQueueWindow();
  }
  if (queue.window) {
    if (queue.wake_pending)
      return true;
    if (PostMessageW(queue.window, kTaskMessage, 0, 0)) {
      queue.wake_pending = true;
      return true;
    }
    queue.tasks.pop_back();
    return false;
  }
  if (g_bootstraps.find(thread_id) != g_bootstraps.end()) {
    return true;  // The bootstrap in flight runs what is queued.
  }

  HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, BootstrapProc, nullptr, thread_id);
  if (hook && PostThreadMessageW(thread_id, WM_NULL, 0, 0)) {
    g_bootstraps[thread_id] = hook;
    return true;
  }
  if (hook) {
    UnhookWindowsHookEx(hook);
  }
  queue.tasks.pop_back();
  return false;
}

}  // namespace nativeapi::detail