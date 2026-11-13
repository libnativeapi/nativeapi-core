#include "view_update_scheduler_windows.h"

#include <dwmapi.h>
#include <windows.h>
#include <algorithm>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../view_impl.h"

namespace nativeapi::detail {
namespace {
constexpr UINT kReady = WM_APP + 0x58;
constexpr wchar_t kClass[] = L"NativeApiViewUpdateScheduler";

// One scheduler per owning UI thread. The worker only wakes that thread; it
// never reads a View or runs client code. A high-resolution waitable timer is
// independent of WM_TIMER/WM_PAINT starvation during sustained mouse input.
class Scheduler {
 public:
  struct Entry {
    std::weak_ptr<View> view;
    uint64_t generation;
  };
  HWND window = nullptr;
  HANDLE timer = nullptr, stop = nullptr;
  std::thread worker;
  std::unordered_map<ViewId, Entry> pending;
  int64_t next_start = 0, frequency = 0, frame_period = 0, refresh_checked = 0,
          input_yield_started = 0;
  bool armed = false, delivering = false;

  Scheduler() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency = f.QuadPart;
    frame_period = frequency / 60;
    WNDCLASSW wc{};
    wc.lpfnWndProc = Procedure;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    window =
        CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
    timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                   TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!timer)
      timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!window || !timer || !stop)
      return;
    try {
      worker = std::thread([this] {
        HANDLE handles[] = {stop, timer};
        while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
          // A full application queue must not permanently strand the request.
          while (!PostMessageW(window, kReady, 0, 0)) {
            if (WaitForSingleObject(stop, 1) != WAIT_TIMEOUT)
              return;
          }
        }
      });
    } catch (...) { /* RequestUpdate reports scheduler failure. */
    }
  }
  ~Scheduler() {
    if (stop)
      SetEvent(stop);
    if (worker.joinable())
      worker.join();
    if (window)
      DestroyWindow(window);
    if (timer)
      CloseHandle(timer);
    if (stop)
      CloseHandle(stop);
  }
  static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCCREATE)
      SetWindowLongPtrW(
          hwnd, GWLP_USERDATA,
          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<Scheduler*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == kReady && self) {
      self->Deliver();
      return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
  }
  bool Request(const std::shared_ptr<View>& view) {
    if (!worker.joinable())
      return false;
    pending[view->GetId()] = {view, ViewInternal::Of(*view).update_generation};
    if (armed || delivering)
      return true;
    if (Arm())
      return true;
    pending.erase(view->GetId());
    return false;
  }
  bool Arm() {
    LARGE_INTEGER now, due;
    QueryPerformanceCounter(&now);
    if (now.QuadPart - refresh_checked >= frequency) {
      DWM_TIMING_INFO timing{};
      timing.cbSize = sizeof(timing);
      if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) && timing.qpcRefreshPeriod > 0 &&
          timing.qpcRefreshPeriod <= frequency) {
        frame_period = static_cast<int64_t>(timing.qpcRefreshPeriod);
      }
      refresh_checked = now.QuadPart;
    }
    // Preserve the active cadence instead of adding wake-up lateness to every
    // frame. After idle, start asynchronously without replaying missed ticks.
    if (next_start + frame_period < now.QuadPart)
      next_start = now.QuadPart;
    const auto remaining = std::max<int64_t>(0, next_start - now.QuadPart);
    due.QuadPart = -std::max<int64_t>(1000, remaining * 10000000 / frequency);
    armed = SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != FALSE;
    if (!armed)
      armed = PostMessageW(window, kReady, 0, 0) != FALSE;
    return armed;
  }
  void Deliver() {
    LARGE_INTEGER ready_time;
    QueryPerformanceCounter(&ready_time);
    // Input may become available after Application's pre-check but before this
    // posted message is retrieved. Yield without a nested pump so the host can
    // dispatch it and coalesce wheel/state changes before expensive rendering.
    if (HIWORD(GetQueueStatus(QS_INPUT))) {
      if (!input_yield_started)
        input_yield_started = ready_time.QuadPart;
      if (ready_time.QuadPart - input_yield_started < frequency / 125) {
        LARGE_INTEGER retry;
        retry.QuadPart = -10000;  // 1 ms, bounded to an 8 ms input slice.
        if (SetWaitableTimer(timer, &retry, 0, nullptr, nullptr, FALSE))
          return;
      }
    }
    input_yield_started = 0;
    armed = false;
    delivering = true;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const auto period = frame_period;
    next_start += period;
    if (next_start <= now.QuadPart)
      next_start += ((now.QuadPart - next_start) / period + 1) * period;
    auto ready = std::move(pending);
    pending.clear();
    std::vector<std::shared_ptr<View>> paint;
    for (auto& [id, entry] : ready) {
      if (auto view = entry.view.lock()) {
        if (ViewInternal::Of(*view).DeliverUpdate(entry.generation))
          paint.push_back(view);
      }
    }
    // Paint once after every client finished publication. Do not invalidate
    // unchanged content or disable redraw; only drain already-pending damage.
    for (auto& view : paint) {
      auto hwnd = static_cast<HWND>(view->GetNativeObject());
      if (IsWindow(hwnd) && IsWindowVisible(hwnd))
        RedrawWindow(hwnd, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    delivering = false;
    // Arm only after painting. A callback over budget must return to the input
    // queue before another posted update can take priority over native input.
    if (!pending.empty())
      Arm();
  }
};
}  // namespace

bool ScheduleViewUpdate(const std::shared_ptr<View>& view) {
  if (!view || !view->GetNativeObject())
    return false;
  auto hwnd = static_cast<HWND>(view->GetNativeObject());
  if (GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId())
    return false;
  thread_local Scheduler scheduler;
  return scheduler.Request(view);
}
}  // namespace nativeapi::detail
