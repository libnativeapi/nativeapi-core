#include "window_message_dispatcher.h"
#include <commctrl.h>
#include <algorithm>
#include <functional>
#include <iterator>
#include <vector>
#include "dpi_utils_windows.h"
#include "ui_thread_windows.h"

namespace nativeapi {

namespace {

UINT_PTR DispatcherSubclassId() {
  static int tag = 0;
  return reinterpret_cast<UINT_PTR>(&tag);
}

bool MatchesMessageFilter(const std::vector<UINT>& messages, UINT msg) {
  if (messages.empty()) {
    return true;
  }
  return std::find(messages.begin(), messages.end(), msg) != messages.end();
}

}  // namespace

WindowMessageDispatcher& WindowMessageDispatcher::GetInstance() {
  // Use heap allocation to avoid static destruction order issues
  // The instance is never destroyed to ensure it remains valid during
  // the entire program lifetime, including during static destruction
  static auto* instance = new WindowMessageDispatcher();
  return *instance;
}

WindowMessageDispatcher::~WindowMessageDispatcher() {
  std::unordered_map<HWND, std::shared_ptr<WindowState>> states;
  HWND host = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    host = host_window_;
    host_window_ = nullptr;
    states.swap(window_states_);
    handlers_.clear();
    global_handler_ids_.clear();
    global_handler_count_.store(0, std::memory_order_relaxed);
  }

  for (const auto& [hwnd, state] : states) {
    if (hwnd && IsWindow(hwnd)) {
      RemoveWindowSubclass(hwnd, SubclassWindowProc, DispatcherSubclassId());
    }
  }
  if (host && IsWindow(host)) {
    DestroyWindow(host);
  }
}

void WindowMessageDispatcher::RebuildWindowMaskLocked(WindowState& state) {
  std::array<uint64_t, WindowState::kMaskWords> mask{};
  uint32_t wildcards = 0;
  for (const int id : state.handler_ids) {
    auto it = handlers_.find(id);
    if (it == handlers_.end()) {
      continue;
    }
    const auto& msgs = it->second.messages;
    if (msgs.empty()) {
      ++wildcards;
      continue;
    }
    bool all_in_mask = true;
    for (const UINT m : msgs) {
      if (m < WindowState::kMaskWords * 64) {
        mask[m >> 6] |= (uint64_t{1} << (m & 63));
      } else {
        all_in_mask = false;
      }
    }
    if (!all_in_mask) {
      ++wildcards;
    }
  }
  for (size_t i = 0; i < WindowState::kMaskWords; ++i) {
    state.msg_mask[i].store(mask[i], std::memory_order_relaxed);
  }
  state.wildcard_count.store(wildcards, std::memory_order_relaxed);
}

int WindowMessageDispatcher::RegisterHandler(WindowMessageHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);

  int id = next_id_++;
  handlers_[id] = {std::move(handler), HWND(0), {}};  // HWND(0) for global handler
  global_handler_ids_.insert(global_handler_ids_.begin(), id);
  global_handler_count_.store(static_cast<uint32_t>(global_handler_ids_.size()),
                              std::memory_order_relaxed);
  return id;
}

int WindowMessageDispatcher::RegisterHandler(HWND hwnd, WindowMessageHandler handler) {
  return RegisterHandler(hwnd, {}, std::move(handler));
}

int WindowMessageDispatcher::RegisterHandler(HWND hwnd,
                                             std::initializer_list<UINT> messages,
                                             WindowMessageHandler handler) {
  std::shared_ptr<WindowState> state;
  bool needs_hook = false;
  int id = 0;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = next_id_++;
    handlers_[id] = {std::move(handler), hwnd, std::vector<UINT>(messages)};

    auto it = window_states_.find(hwnd);
    if (it == window_states_.end()) {
      state = std::make_shared<WindowState>();
      window_states_[hwnd] = state;
      needs_hook = true;
    } else {
      state = it->second;
    }
    // Keep handler_ids sorted descending by ID (most recently registered first).
    state->handler_ids.insert(state->handler_ids.begin(), id);
    RebuildWindowMaskLocked(*state);
  }

  if (needs_hook) {
    EnsureHookInstalled(hwnd, state);
  }

  return id;
}

bool WindowMessageDispatcher::UnregisterHandler(int id) {
  HWND target_hwnd = HWND(0);
  std::shared_ptr<WindowState> state_to_uninstall;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = handlers_.find(id);
    if (it == handlers_.end()) {
      return false;
    }

    target_hwnd = it->second.target_hwnd;
    handlers_.erase(it);

    if (target_hwnd == HWND(0)) {
      auto git = std::find(global_handler_ids_.begin(), global_handler_ids_.end(), id);
      if (git != global_handler_ids_.end()) {
        global_handler_ids_.erase(git);
      }
      global_handler_count_.store(static_cast<uint32_t>(global_handler_ids_.size()),
                                  std::memory_order_relaxed);
    } else {
      auto sit = window_states_.find(target_hwnd);
      if (sit != window_states_.end()) {
        auto& ids = sit->second->handler_ids;
        auto id_it = std::find(ids.begin(), ids.end(), id);
        if (id_it != ids.end()) {
          ids.erase(id_it);
        }
        RebuildWindowMaskLocked(*sit->second);
        if (ids.empty()) {
          state_to_uninstall = sit->second;
        }
      }
    }
  }

  if (state_to_uninstall) {
    ScheduleHookUninstall(target_hwnd, state_to_uninstall);
  }

  return true;
}

LRESULT CALLBACK WindowMessageDispatcher::SubclassWindowProc(HWND hwnd,
                                                             UINT msg,
                                                             WPARAM wparam,
                                                             LPARAM lparam,
                                                             UINT_PTR subclass_id,
                                                             DWORD_PTR ref_data) {
  auto& dispatcher = GetInstance();
  auto* state = reinterpret_cast<WindowState*>(ref_data);

  const bool has_global = dispatcher.global_handler_count_.load(std::memory_order_relaxed) > 0;
  const bool has_window_match =
      state != nullptr &&
      (state->wildcard_count.load(std::memory_order_relaxed) > 0 ||
       (msg < WindowState::kMaskWords * 64 &&
        (state->msg_mask[msg >> 6].load(std::memory_order_relaxed) &
         (uint64_t{1} << (msg & 63))) != 0));

  // Fast path: no global handler and no per-window handler cares about `msg`.
  if (msg != WM_NCDESTROY && !has_global && !has_window_match) {
    return DefSubclassProc(hwnd, msg, wparam, lparam);
  }

  // Slow path: snapshot candidate handler IDs (already sorted descending by ID).
  // Only the IDs are snapshotted: a handler may destroy the object another
  // handler belongs to while this message is being dispatched, so each handler
  // is read back under the lock immediately before it is invoked.
  std::vector<int> ids;
  if (has_global || has_window_match) {
    std::lock_guard<std::mutex> lock(dispatcher.mutex_);
    if (!has_global) {
      if (state) {
        ids = state->handler_ids;
      }
    } else if (!state || state->handler_ids.empty()) {
      ids = dispatcher.global_handler_ids_;
    } else {
      ids.reserve(state->handler_ids.size() + dispatcher.global_handler_ids_.size());
      std::merge(state->handler_ids.begin(), state->handler_ids.end(),
                 dispatcher.global_handler_ids_.begin(), dispatcher.global_handler_ids_.end(),
                 std::back_inserter(ids), std::greater<int>());
    }
  }

  std::optional<LRESULT> handled_result;
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
      if (!MatchesMessageFilter(entry->second.messages, msg)) {
        continue;
      }
      handler = entry->second.handler;
    }

    auto result = handler(hwnd, msg, wparam, lparam);
    if (result.has_value()) {
      handled_result = result;
      break;
    }
  }

  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, SubclassWindowProc, subclass_id);
    std::lock_guard<std::mutex> lock(dispatcher.mutex_);
    auto sit = dispatcher.window_states_.find(hwnd);
    if (sit != dispatcher.window_states_.end() && sit->second.get() == state) {
      dispatcher.window_states_.erase(sit);
    }
  }

  if (handled_result.has_value()) {
    return handled_result.value();
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK WindowMessageDispatcher::DispatchWindowProc(HWND hwnd,
                                                             UINT msg,
                                                             WPARAM wparam,
                                                             LPARAM lparam) {
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void WindowMessageDispatcher::EnsureHookInstalled(HWND hwnd,
                                                  const std::shared_ptr<WindowState>& state) {
  if (!hwnd || !IsWindow(hwnd)) {
    return;
  }
  const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
  if (!thread) {
    return;
  }
  auto install = [this, hwnd, state]() {
    if (!IsWindow(hwnd)) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto sit = window_states_.find(hwnd);
      if (sit == window_states_.end() || sit->second != state) {
        return;
      }
    }
    SetWindowSubclass(hwnd, SubclassWindowProc, DispatcherSubclassId(),
                      reinterpret_cast<DWORD_PTR>(state.get()));
  };
  if (thread == GetCurrentThreadId()) {
    install();
  } else {
    detail::PostToThread(thread, std::move(install));
  }
}

void WindowMessageDispatcher::ScheduleHookUninstall(HWND hwnd,
                                                    const std::shared_ptr<WindowState>& state) {
  if (!hwnd) {
    return;
  }
  auto uninstall = [this, hwnd, state]() {
    bool should_remove = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto sit = window_states_.find(hwnd);
      if (sit != window_states_.end() && sit->second == state && state->handler_ids.empty()) {
        should_remove = true;
      }
    }
    if (!should_remove) {
      return;
    }
    if (IsWindow(hwnd)) {
      RemoveWindowSubclass(hwnd, SubclassWindowProc, DispatcherSubclassId());
    }
    // Re-check under the lock before erasing, in case a handler was added
    // while RemoveWindowSubclass was running, and keep `state` alive until
    // after RemoveWindowSubclass has returned on the window's thread.
    std::lock_guard<std::mutex> lock(mutex_);
    auto sit = window_states_.find(hwnd);
    if (sit != window_states_.end() && sit->second == state && state->handler_ids.empty()) {
      window_states_.erase(sit);
    }
  };

  const DWORD thread = IsWindow(hwnd) ? GetWindowThreadProcessId(hwnd, nullptr) : 0;
  if (thread == 0 || thread == GetCurrentThreadId()) {
    uninstall();
  } else if (!detail::PostToThread(thread, uninstall)) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto sit = window_states_.find(hwnd);
    if (sit != window_states_.end() && sit->second == state && state->handler_ids.empty()) {
      window_states_.erase(sit);
    }
  }
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
  wc.lpfnWndProc = DefWindowProcW;
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

    host_window_ = new_host_window;
  }

  return host_window_;
}

}  // namespace nativeapi
