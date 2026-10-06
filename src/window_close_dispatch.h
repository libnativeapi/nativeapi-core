#pragma once

// Private native-window producer protocol. Does not enter API_HEADERS.
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "window.h"

namespace nativeapi::detail {
struct WindowEventContext;
struct WindowEventRegistration {
  std::shared_ptr<WindowEventContext> context;
  ~WindowEventRegistration();
};

class WindowCloseState : public std::enable_shared_from_this<WindowCloseState> {
 public:
  using Post = std::function<bool(std::function<void()>)>;
  static std::shared_ptr<WindowCloseState> Create(WindowId id,
                                                  Post post,
                                                  std::function<bool()> valid);
  ~WindowCloseState();
  std::shared_ptr<WindowEventRegistration> Subscribe(
      std::function<void(const WindowEvent&)> sink,
      std::shared_ptr<std::atomic<bool>> active = nullptr,
      std::function<bool()> wants_close = nullptr);
  bool HasObservers() const;
  bool IsAlive() const;
  bool IsPending() const;
  // Must start on target UI. Completion may arrive from any worker.
  bool Request(std::function<void()> action);
  bool DispatchTask(std::function<void()> task);
  void NotifyRequired();
  void Invalidate(bool native_destroyed = true);
  void EmitObserved(const WindowEvent& event);

 private:
  struct Attempt;
  WindowCloseState(WindowId id, Post post, std::function<bool()> valid);
  void Complete(const std::shared_ptr<Attempt>& attempt, bool accepted);
  WindowId id_;
  Post post_;
  std::function<bool()> valid_;
  mutable std::mutex mutex_;
  bool alive_ = true;
  std::shared_ptr<Attempt> attempt_;
  std::vector<std::weak_ptr<WindowEventContext>> observers_;
};

void EmitObservedWindowEvent(const WindowEvent& event);
// All six platform definitions; no native handles are captured by common code.
std::shared_ptr<WindowCloseState> GetNativeWindowCloseState(void* window, WindowId id);
bool RequestNativeWindowClose(void* window, WindowId id);
bool IsNativeWindowCloseSupported();
// Reinstall per-class methods after macOS changes a window's runtime class.
#if defined(__APPLE__)
void RefreshNativeWindowCloseHooks(void* window);
#endif
}  // namespace nativeapi::detail
