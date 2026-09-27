#include <windows.h>

#include <cmath>
#include <mutex>
#include <unordered_map>

#include "../../window_drag_session.h"
#include "dpi_utils_windows.h"

namespace nativeapi {

namespace {

// Thread timers have no user data, so the callback finds its session here.
// Sessions live on the UI thread, but the map is locked anyway so a session
// destroyed from another thread cannot race a tick.
std::mutex g_timer_mutex;
std::unordered_map<UINT_PTR, WindowDragSession*> g_timer_sessions;

}  // namespace

class WindowDragSession::Impl {
 public:
  UINT_PTR timer_id_ = 0;

  static void CALLBACK TimerProc(HWND, UINT, UINT_PTR timer_id, DWORD) {
    WindowDragSession* session = nullptr;
    {
      std::lock_guard<std::mutex> lock(g_timer_mutex);
      auto it = g_timer_sessions.find(timer_id);
      if (it != g_timer_sessions.end()) {
        session = it->second;
      }
    }
    if (session) {
      session->HandleTick();
    }
  }
};

WindowDragSession::WindowDragSession() : pimpl_(std::make_unique<Impl>()) {}

WindowDragSession::~WindowDragSession() {
  active_ = false;
  StopTicking();
}

void WindowDragSession::StartTicking() {
  if (pimpl_->timer_id_ != 0) {
    return;
  }
  // USER_TIMER_MINIMUM (10 ms) is the effective floor. WM_TIMER is also
  // dispatched by modal loops (menus, system move/size), so ticks keep coming.
  UINT_PTR timer_id = SetTimer(nullptr, 0, USER_TIMER_MINIMUM, &Impl::TimerProc);
  if (timer_id == 0) {
    return;
  }
  pimpl_->timer_id_ = timer_id;
  std::lock_guard<std::mutex> lock(g_timer_mutex);
  g_timer_sessions[timer_id] = this;
}

void WindowDragSession::StopTicking() {
  if (pimpl_->timer_id_ == 0) {
    return;
  }
  KillTimer(nullptr, pimpl_->timer_id_);
  {
    std::lock_guard<std::mutex> lock(g_timer_mutex);
    g_timer_sessions.erase(pimpl_->timer_id_);
  }
  pimpl_->timer_id_ = 0;
}

bool WindowDragSession::QueryPointer(Point& position, bool& primary_button_down) const {
  POINT cursor;
  if (!GetCursorPos(&cursor)) {
    return false;
  }
  position = PhysicalToLogicalPoint(cursor);

  // The primary button is the right one when the user swapped buttons.
  int primary = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
  primary_button_down = (GetAsyncKeyState(primary) & 0x8000) != 0;
  return true;
}

void WindowDragSession::MoveWindow(Window& window, Point /*cursor_position*/) const {
  HWND hwnd = static_cast<HWND>(window.GetNativeObject());
  if (!hwnd) {
    return;
  }
  // Work in physical pixels: the logical cursor position is relative to the
  // cursor's monitor, the anchor to the window's, and the two differ while the
  // window straddles monitors with different scale factors.
  POINT cursor;
  if (!GetCursorPos(&cursor)) {
    return;
  }
  double scale = GetScaleFactorForWindow(hwnd);
  if (scale <= 0.0) {
    scale = 1.0;
  }
  const int x = cursor.x - static_cast<int>(std::lround(anchor_.x * scale));
  const int y = cursor.y - static_cast<int>(std::lround(anchor_.y * scale));
  SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace nativeapi
