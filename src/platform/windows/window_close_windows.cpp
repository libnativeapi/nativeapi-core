#include <windows.h>

#include <commctrl.h>
#include <algorithm>
#include <unordered_map>
#include "../../window_close_dispatch.h"

namespace nativeapi::detail {
namespace {
constexpr wchar_t kPolicy[] = L"NativeAPIWindowClosePolicy";
constexpr wchar_t kId[] = L"NativeAPIWindowId";
UINT CompletionMessage() {
  static UINT message = RegisterWindowMessageW(L"libnativeapi.WindowCloseCompletion");
  return message;
}
struct Policy : std::enable_shared_from_this<Policy> {
  HWND hwnd;
  WindowId id;
  DWORD thread;
  void* box = nullptr;
  std::shared_ptr<WindowCloseState> state;
  std::mutex mutex;
  std::vector<std::pair<uint64_t, std::function<void()>>> queue;
  uint64_t sequence = 0;
  std::atomic<bool> closed{false};
  bool bypass = false;
  bool required = false;
  bool session_ending = false;
  bool Valid() const {
    return !closed.load() && IsWindow(hwnd) && GetPropW(hwnd, kPolicy) == box &&
           static_cast<WindowId>(reinterpret_cast<uintptr_t>(GetPropW(hwnd, kId))) == id;
  }
  bool Post(std::function<void()> work) {
    if (!Valid())
      return false;
    if (GetCurrentThreadId() == thread) {
      work();
      return true;
    }
    uint64_t ticket;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (closed.load())
        return false;
      ticket = ++sequence;
      queue.emplace_back(ticket, std::move(work));
    }
    if (PostMessageW(hwnd, CompletionMessage(), static_cast<uint32_t>(id),
                     static_cast<uint32_t>(id >> 32)))
      return true;
    std::lock_guard<std::mutex> lock(mutex);
    queue.erase(std::remove_if(queue.begin(), queue.end(),
                               [ticket](const auto& entry) { return entry.first == ticket; }),
                queue.end());
    return false;
  }
  void Drain() {
    std::vector<std::pair<uint64_t, std::function<void()>>> work;
    {
      std::lock_guard<std::mutex> lock(mutex);
      work.swap(queue);
    }
    for (auto& entry : work)
      if (Valid())
        entry.second();
  }
  bool Request(UINT message, WPARAM wp, LPARAM lp) {
    if (!Valid() || required)
      return false;
    if (session_ending) {
      SendMessageW(hwnd, message, wp, lp);
      return true;
    }
    std::weak_ptr<Policy> weak = shared_from_this();
    return state->Request([weak, message, wp, lp] {
      auto policy = weak.lock();
      if (!policy || !policy->Valid())
        return;
      const bool previous = policy->bypass;
      policy->bypass = true;
      SendMessageW(policy->hwnd, message, wp, lp);
      policy->bypass = previous;
    });
  }
};
using Box = std::shared_ptr<Policy>;
LRESULT CALLBACK
CloseProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR subclass, DWORD_PTR data) try {
  auto* box = reinterpret_cast<Box*>(data);
  auto policy = *box;
  if (message == WM_NCDESTROY) {
    policy->closed = true;
    policy->state->Invalidate();
    {
      std::vector<std::pair<uint64_t, std::function<void()>>> discarded;
      {
        std::lock_guard<std::mutex> lock(policy->mutex);
        discarded.swap(policy->queue);
      }
    }
    if (GetPropW(hwnd, kPolicy) == box)
      RemovePropW(hwnd, kPolicy);
    RemoveWindowSubclass(hwnd, CloseProc, subclass);
    auto result = DefSubclassProc(hwnd, message, wp, lp);
    delete box;
    return result;
  }
  if (message == CompletionMessage()) {
    const auto id = static_cast<WindowId>(static_cast<uint32_t>(wp)) |
                    (static_cast<WindowId>(static_cast<uint32_t>(lp)) << 32);
    if (id == policy->id)
      policy->Drain();
    return 0;
  }
  if (policy->bypass)
    return DefSubclassProc(hwnd, message, wp, lp);
  if (message == WM_SYSCOMMAND && (wp & 0xfff0) == SC_CLOSE &&
      (policy->state->HasObservers() || policy->state->IsPending()) && !policy->session_ending) {
    (void)policy->Request(message, wp, lp);
    return 0;
  }
  if ((message == WM_CLOSE || message == WM_DESTROY || message == WM_QUERYENDSESSION ||
       (message == WM_ENDSESSION && wp)) &&
      !policy->required) {
    policy->required = true;
    if (message == WM_DESTROY)
      policy->closed = true;
    policy->state->NotifyRequired();
    auto result = DefSubclassProc(hwnd, message, wp, lp);
    policy->required = false;
    if (message == WM_QUERYENDSESSION)
      policy->session_ending = result != 0;
    return result;
  }
  if (message == WM_ENDSESSION && !wp)
    policy->session_ending = false;
  return DefSubclassProc(hwnd, message, wp, lp);
} catch (...) {
  if (message == WM_SYSCOMMAND && (wp & 0xfff0) == SC_CLOSE)
    return 0;
  return DefSubclassProc(hwnd, message, wp, lp);
}

std::shared_ptr<Policy> GetPolicy(void* native, WindowId id) {
  HWND hwnd = static_cast<HWND>(native);
  DWORD process;
  if (!hwnd || !IsWindow(hwnd) ||
      GetWindowThreadProcessId(hwnd, &process) != GetCurrentThreadId() ||
      process != GetCurrentProcessId() ||
      static_cast<WindowId>(reinterpret_cast<uintptr_t>(GetPropW(hwnd, kId))) != id)
    return nullptr;
  if (auto* box = static_cast<Box*>(GetPropW(hwnd, kPolicy)))
    return (*box)->Valid() ? *box : nullptr;
  auto policy = std::make_shared<Policy>();
  policy->hwnd = hwnd;
  policy->id = id;
  policy->thread = GetCurrentThreadId();
  auto box = std::make_unique<Box>(policy);
  policy->box = box.get();
  std::weak_ptr<Policy> weak = policy;
  policy->state = WindowCloseState::Create(
      id,
      [weak](auto work) {
        if (auto policy = weak.lock())
          return policy->Post(std::move(work));
        return false;
      },
      [weak] {
        auto policy = weak.lock();
        return policy && policy->Valid();
      });
  if (!CompletionMessage() || !SetPropW(hwnd, kPolicy, box.get()))
    return nullptr;
  if (!SetWindowSubclass(hwnd, CloseProc, reinterpret_cast<UINT_PTR>(&CloseProc),
                         reinterpret_cast<DWORD_PTR>(box.get()))) {
    RemovePropW(hwnd, kPolicy);
    return nullptr;
  }
  box.release();
  return policy;
}
}  // namespace
std::shared_ptr<WindowCloseState> GetNativeWindowCloseState(void* window, WindowId id) {
  auto policy = GetPolicy(window, id);
  return policy ? policy->state : nullptr;
}
bool RequestNativeWindowClose(void* window, WindowId id) {
  auto policy = GetPolicy(window, id);
  return policy && policy->Request(WM_CLOSE, 0, 0);
}
bool IsNativeWindowCloseSupported() {
  return true;
}
}  // namespace nativeapi::detail

namespace nativeapi {
namespace {
struct WindowTaskActor {
  HWND hwnd;
  WindowId identity;
  WNDPROC original;
  std::vector<std::function<void()>> jobs;
};
struct WindowTaskRegistry {
  std::mutex mutex;
  std::unordered_map<HWND, std::shared_ptr<WindowTaskActor>> actors;
};
WindowTaskRegistry& WindowTasks() {
  static auto* registry = new WindowTaskRegistry;
  return *registry;
}
UINT WindowTaskMessage() {
  static UINT message = RegisterWindowMessageW(L"libnativeapi.WindowOwnerTask");
  return message;
}
LRESULT CALLBACK WindowTaskProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  std::shared_ptr<WindowTaskActor> actor;
  std::vector<std::function<void()>> work;
  bool ours = false;
  {
    auto& registry = WindowTasks();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto entry = registry.actors.find(hwnd);
    if (entry != registry.actors.end()) {
      actor = entry->second;
      if (message == WindowTaskMessage()) {
        WindowId identity = static_cast<uint32_t>(wp) | (WindowId(static_cast<uint32_t>(lp)) << 32);
        ours = identity == actor->identity;
        if (ours)
          work.swap(actor->jobs);
      } else if (message == WM_NCDESTROY) {
        work.swap(actor->jobs);
        registry.actors.erase(entry);
        // Do not overwrite a newer host hook that forwards into this one.
        if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC)) == WindowTaskProc)
          SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(actor->original));
      }
    }
  }
  if (ours) {
    for (auto& job : work) {
      if (IsWindow(hwnd) &&
          reinterpret_cast<uintptr_t>(GetPropW(hwnd, L"NativeAPIWindowId")) == actor->identity) {
        try {
          job();
        } catch (...) {
        }
      }
    }
    return 0;
  }
  // Destruction drops guarded tasks outside the registry lock. Every host
  // message, including forced close and teardown, retains its original chain.
  return actor ? CallWindowProcW(actor->original, hwnd, message, wp, lp)
               : DefWindowProcW(hwnd, message, wp, lp);
}
}  // namespace
bool Window::DispatchWindowTask(std::function<void()> task) {
  HWND hwnd = static_cast<HWND>(GetNativeObject());
  DWORD process = 0;
  DWORD thread = GetWindowThreadProcessId(hwnd, &process);
  if (!thread || process != GetCurrentProcessId())
    return false;
  if (thread == GetCurrentThreadId()) {
    task();
    return true;
  }
  auto& registry = WindowTasks();
  std::lock_guard<std::mutex> lock(registry.mutex);
  const WindowId identity = GetId();
  if (!IsWindow(hwnd) ||
      reinterpret_cast<uintptr_t>(GetPropW(hwnd, L"NativeAPIWindowId")) != identity)
    return false;
  auto& actor = registry.actors[hwnd];
  if (!actor) {
    actor = std::make_shared<WindowTaskActor>();
    actor->hwnd = hwnd;
    actor->identity = identity;
    // Same-process SetWindowLongPtr is allowed from a worker; unlike a thread
    // hook this forwarding entry survives that registering worker's exit.
    SetLastError(0);
    actor->original = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowTaskProc)));
    if (!actor->original) {
      registry.actors.erase(hwnd);
      return false;
    }
  }
  if (actor->identity != identity)
    return false;
  actor->jobs.push_back(std::move(task));
  if (PostMessageW(hwnd, WindowTaskMessage(), static_cast<uint32_t>(identity),
                   static_cast<uint32_t>(identity >> 32)))
    return true;
  actor->jobs.pop_back();
  return false;
}
}  // namespace nativeapi
