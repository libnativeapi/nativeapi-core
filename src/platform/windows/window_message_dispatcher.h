#pragma once

#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace nativeapi {

/**
 * @brief Function type for handling Windows messages.
 *
 * @param hwnd Window handle receiving the message
 * @param msg Message identifier (WM_* constants)
 * @param wparam Message-specific parameter
 * @param lparam Message-specific parameter
 * @return std::optional<LRESULT> If a value is returned, the message is
 * considered handled. If std::nullopt is returned, the message continues to
 * other handlers.
 */
using WindowMessageHandler =
    std::function<std::optional<LRESULT>(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)>;

/**
 * @brief Singleton dispatcher for Windows message handling across multiple
 * windows.
 *
 * This class provides a centralized way to register message handlers for
 * Windows messages. It supports both global handlers (applied to all windows)
 * and window-specific handlers. The dispatcher automatically hooks into window
 * procedures using SetWindowLongPtr and restores original procedures when
 * handlers are unregistered.
 *
 * Thread Safety: All public methods are thread-safe.
 *
 * Usage Example:
 * ```cpp
 * auto& dispatcher = WindowMessageDispatcher::GetInstance();
 *
 * // Register global handler for all windows
 * int global_id = dispatcher.RegisterHandler([](HWND hwnd, UINT msg, WPARAM wp,
 * LPARAM lp) { if (msg == WM_SIZE) {
 *         // Handle window resize
 *         return std::make_optional<LRESULT>(0);
 *     }
 *     return std::nullopt; // Let other handlers process
 * });
 *
 * // Register window-specific handler
 * int window_id = dispatcher.RegisterHandler(specific_hwnd, [](HWND hwnd, UINT
 * msg, WPARAM wp, LPARAM lp) { if (msg == WM_CLOSE) {
 *         // Prevent window from closing
 *         return std::make_optional<LRESULT>(0);
 *     }
 *     return std::nullopt;
 * });
 *
 * // Unregister when done
 * dispatcher.UnregisterHandler(global_id);
 * dispatcher.UnregisterHandler(window_id);
 * ```
 */
class WindowMessageDispatcher {
 public:
  /**
   * @brief Get the singleton instance of the dispatcher.
   * @return Reference to the singleton WindowMessageDispatcher instance.
   */
  static WindowMessageDispatcher& GetInstance();

  /**
   * @brief Register a global message handler that applies to all windows.
   *
   * @param handler Function to call for Windows messages
   * @return int Handler ID for unregistration
   */
  int RegisterHandler(WindowMessageHandler handler);

  /**
   * @brief Register a message handler for a specific window.
   *
   * This method automatically installs a hook into the window's procedure
   * if not already installed. The hook is removed when the last handler
   * for the window is unregistered.
   *
   * @param hwnd Target window handle
   * @param handler Function to call for Windows messages
   * @return int Handler ID for unregistration
   */
  int RegisterHandler(HWND hwnd, WindowMessageHandler handler);

  /**
   * @brief Register a message handler for a specific window and subset of messages.
   *
   * Messages not listed in @p messages bypass mutex locking, handler lookup,
   * and allocation in the subclass procedure via a lock-free bitmask.
   *
   * @param hwnd Target window handle
   * @param messages Win32 message IDs this handler cares about (empty = all)
   * @param handler Function to call for matching Windows messages
   * @return int Handler ID for unregistration
   */
  int RegisterHandler(HWND hwnd,
                      std::initializer_list<UINT> messages,
                      WindowMessageHandler handler);

  /**
   * @brief Unregister a message handler by ID.
   *
   * @param id Handler ID returned from RegisterHandler
   * @return bool true if handler was found and removed, false otherwise
   */
  bool UnregisterHandler(int id);

  /**
   * @brief Get a host window for tray icons and menus.
   *
   * This method provides a hidden window that can be used as a parent
   * for tray icons and menus. The window is created once and reused.
   *
   * @return HWND Host window handle, or nullptr if creation failed
   */
  HWND GetHostWindow();

  /**
   * @brief Internal window procedure function used for message dispatching.
   *
   * This function is installed as the window procedure for windows that have
   * registered handlers. It processes messages through registered handlers
   * and falls back to the original window procedure if no handler consumes
   * the message.
   *
   * @param hwnd Window handle
   * @param msg Message identifier
   * @param wparam Message parameter
   * @param lparam Message parameter
   * @return LRESULT Message processing result
   */
  static LRESULT CALLBACK DispatchWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

 private:
  WindowMessageDispatcher() = default;
  ~WindowMessageDispatcher();

  /**
   * @brief Entry for a registered message handler.
   */
  struct HandlerEntry {
    WindowMessageHandler handler;  ///< The handler function
    HWND target_hwnd;              ///< Target window (HWND(0) for global handlers)
    std::vector<UINT> messages;    ///< Empty = all messages; otherwise filter list
  };

  /**
   * @brief Per-window dispatch state passed directly as SetWindowSubclass dwRefData.
   */
  struct WindowState {
    static constexpr size_t kMaskWords = 16;  // Covers standard messages 0..1023.
    std::array<std::atomic<uint64_t>, kMaskWords> msg_mask{};
    std::atomic<uint32_t> wildcard_count{0};
    // Protected by WindowMessageDispatcher::mutex_, kept sorted descending by ID.
    std::vector<int> handler_ids;
  };

  static LRESULT CALLBACK SubclassWindowProc(HWND hwnd,
                                             UINT msg,
                                             WPARAM wparam,
                                             LPARAM lparam,
                                             UINT_PTR subclass_id,
                                             DWORD_PTR ref_data);

  void RebuildWindowMaskLocked(WindowState& state);
  void EnsureHookInstalled(HWND hwnd, const std::shared_ptr<WindowState>& state);
  void ScheduleHookUninstall(HWND hwnd, const std::shared_ptr<WindowState>& state);

  ///< Mutex for thread safety
  mutable std::mutex mutex_;
  ///< Registered handlers by ID
  std::unordered_map<int, HandlerEntry> handlers_;
  ///< Global handler IDs (sorted descending)
  std::vector<int> global_handler_ids_;
  ///< Lock-free count of global handlers for the fast path
  std::atomic<uint32_t> global_handler_count_{0};
  ///< Per-window subclass state
  std::unordered_map<HWND, std::shared_ptr<WindowState>> window_states_;
  ///< Next available handler ID
  int next_id_ = 1;
  ///< Host window for tray icons and menus
  HWND host_window_ = nullptr;
};

}  // namespace nativeapi
