#include "window_message_dispatcher.h"
#include <algorithm>
#include <functional>
#include <vector>
#include "dpi_utils_windows.h"

namespace nativeapi {

WindowMessageDispatcher& WindowMessageDispatcher::GetInstance() {
  // Use heap allocation to avoid static destruction order issues
  // The instance is never destroyed to ensure it remains valid during
  // the entire program lifetime, including during static destruction
  static auto* instance = new WindowMessageDispatcher();
  return *instance;
}

WindowMessageDispatcher::~WindowMessageDispatcher() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Destroy host window if it exists
  if (host_window_) {
    DestroyWindow(host_window_);
    host_window_ = nullptr;
  }

  // Uninstall all hooks before destruction. Over a copy: UninstallHook() erases
  // from original_procs_.
  const auto hooked = original_procs_;
  for (const auto& [hwnd, _] : hooked) {
    UninstallHook(hwnd);
  }
  original_procs_.clear();
}

int WindowMessageDispatcher::RegisterHandler(WindowMessageHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);

  int id = next_id_++;
  handlers_[id] = {std::move(handler), HWND(0)};  // HWND(0) for global handler
  return id;
}

int WindowMessageDispatcher::RegisterHandler(HWND hwnd, WindowMessageHandler handler) {
  // Check if hook needs to be installed (outside of lock to avoid deadlock)
  bool needs_hook = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    needs_hook = (original_procs_.find(hwnd) == original_procs_.end());
  }

  // Install hook if needed (outside of lock)
  if (needs_hook) {
    InstallHook(hwnd);
  }

  // Register the handler (inside lock)
  std::lock_guard<std::mutex> lock(mutex_);
  int id = next_id_++;
  handlers_[id] = {std::move(handler), hwnd};

  return id;
}

bool WindowMessageDispatcher::UnregisterHandler(int id) {
  HWND target_hwnd = HWND(0);
  bool should_uninstall = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = handlers_.find(id);
    if (it == handlers_.end()) {
      return false;
    }

    target_hwnd = it->second.target_hwnd;
    handlers_.erase(it);

    // Check if this was the last handler for this window
    if (target_hwnd != HWND(0)) {
      bool has_other_handlers = std::any_of(
          handlers_.begin(), handlers_.end(),
          [target_hwnd](const auto& pair) { return pair.second.target_hwnd == target_hwnd; });

      should_uninstall = !has_other_handlers;
    }
  }

  // Uninstall hook if needed (outside of lock to avoid deadlock)
  if (should_uninstall) {
    UninstallHook(target_hwnd);
  }

  return true;
}

LRESULT CALLBACK WindowMessageDispatcher::DispatchWindowProc(HWND hwnd,
                                                             UINT msg,
                                                             WPARAM wparam,
                                                             LPARAM lparam) {
  auto& dispatcher = GetInstance();

  // Take the original window procedure and the handler IDs while holding the
  // lock. Only the IDs: a handler may destroy the object another handler
  // belongs to -- a Menu freed from a MenuOpenedEvent listener, say, while this
  // very message is being dispatched -- so each handler is read back under the
  // lock immediately before it is called. Calling a copy taken up front would
  // reach into freed memory from inside a window procedure, which Windows turns
  // into an immediate process kill (STATUS_FATAL_USER_CALLBACK_EXCEPTION).
  WNDPROC original_proc = nullptr;
  std::vector<int> ids;

  {
    std::lock_guard<std::mutex> lock(dispatcher.mutex_);

    // Get original window procedure
    auto proc_it = dispatcher.original_procs_.find(hwnd);
    if (proc_it == dispatcher.original_procs_.end()) {
      return DefWindowProc(hwnd, msg, wparam, lparam);
    }

    original_proc = proc_it->second;

    ids.reserve(dispatcher.handlers_.size());
    for (const auto& [id, entry] : dispatcher.handlers_) {
      if (entry.target_hwnd == HWND(0) || entry.target_hwnd == hwnd) {
        ids.push_back(id);
      }
    }
  }

  // Most recently registered first: IDs grow with each registration, and
  // handlers_ is unordered.
  std::sort(ids.begin(), ids.end(), std::greater<int>());

  // Call the handlers without holding the mutex, so they may register, remove
  // or emit freely. A handler unregistered since the snapshot is skipped.
  // (A handler removed from another thread between the lookup and the call can
  // still be reached; handlers are expected to live on the UI thread.)
  for (const int id : ids) {
    WindowMessageHandler handler;
    {
      std::lock_guard<std::mutex> lock(dispatcher.mutex_);
      auto entry = dispatcher.handlers_.find(id);
      if (entry == dispatcher.handlers_.end()) {
        continue;
      }
      if (entry->second.target_hwnd != HWND(0) && entry->second.target_hwnd != hwnd) {
        continue;
      }
      handler = entry->second.handler;
    }

    auto result = handler(hwnd, msg, wparam, lparam);
    if (result.has_value()) {
      return result.value();
    }
  }

  // No handler consumed the message, call original procedure
  return CallWindowProc(original_proc, hwnd, msg, wparam, lparam);
}

bool WindowMessageDispatcher::InstallHook(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) {
    return false;
  }

  // Get current window procedure
  WNDPROC current_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(hwnd, GWLP_WNDPROC));
  if (!current_proc) {
    return false;
  }

  // If the window already has DispatchWindowProc, don't install it again
  if (current_proc == DispatchWindowProc) {
    return true;  // Already installed
  }

  // Store original procedure
  original_procs_[hwnd] = current_proc;

  // Install our dispatcher as the new window procedure
  SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DispatchWindowProc));

  return true;
}

void WindowMessageDispatcher::UninstallHook(HWND hwnd) {
  auto it = original_procs_.find(hwnd);
  if (it == original_procs_.end()) {
    return;
  }

  // The host window keeps DispatchWindowProc for its whole life, so it also
  // keeps its entry here. Dropping it would make the window deaf for good:
  // DispatchWindowProc bails out early on a window it cannot find, and
  // InstallHook() would not add it back, since the procedure it sees installed
  // is already ours. That is what happened once every menu had been destroyed.
  if (hwnd == host_window_) {
    return;
  }

  // Restore original window procedure
  SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(it->second));

  // Remove from our tracking
  original_procs_.erase(it);
}

HWND WindowMessageDispatcher::GetHostWindow() {
  EnsureProcessDpiAwareness();

  // Check if host window already exists (without holding lock to avoid deadlock)
  if (host_window_ && IsWindow(host_window_)) {
    return host_window_;
  }

  // Create a hidden window class for hosting
  static const wchar_t* class_name = L"NativeApiHostWindow";

  WNDCLASSW wc = {};
  wc.lpfnWndProc = DispatchWindowProc;  // Use dispatcher for host window
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = class_name;

  // Register the window class (only once)
  static bool class_registered = false;
  if (!class_registered) {
    if (RegisterClassW(&wc)) {
      class_registered = true;
    } else {
      return nullptr;
    }
  }

  // Create the hidden host window (outside of lock to avoid deadlock)
  HWND new_host_window = CreateWindowExW(WS_EX_TOOLWINDOW,   // Extended style: tool window
                                         class_name,         // Window class
                                         L"NativeApi Host",  // Window title
                                         WS_OVERLAPPED,      // Window style: overlapped window
                                         0, 0,               // Position
                                         1, 1,               // Size (minimal)
                                         HWND_MESSAGE,       // Parent: message-only window
                                         nullptr,            // Menu
                                         GetModuleHandle(nullptr),  // Instance
                                         nullptr                    // Additional data
  );

  if (new_host_window) {
    // Ensure the window is hidden
    ShowWindow(new_host_window, SW_HIDE);

    // Now acquire lock to register the window
    std::lock_guard<std::mutex> lock(mutex_);

    // Double-check if another thread created the window while we were creating it
    if (host_window_ && IsWindow(host_window_)) {
      // Another thread won, destroy our window and use theirs
      DestroyWindow(new_host_window);
      return host_window_;
    }

    // Register the host window in original_procs_ with DefWindowProc as fallback
    host_window_ = new_host_window;
    original_procs_[host_window_] = DefWindowProcW;
  }

  return host_window_;
}

}  // namespace nativeapi
