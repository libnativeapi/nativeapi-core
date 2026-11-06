// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../window_drag_session.h"

namespace nativeapi {

class WindowDragSession::Impl {};

WindowDragSession::WindowDragSession() : pimpl_(std::make_unique<Impl>()) {}

WindowDragSession::~WindowDragSession() {
  ShutdownEmitter();
}

void WindowDragSession::StartTicking() {}

void WindowDragSession::StopTicking() {}

bool WindowDragSession::QueryPointer(Point&, bool&) const {
  return false;
}

void WindowDragSession::MoveWindow(Window& window, Point cursor_position) const {
  window.SetPosition({cursor_position.x - anchor_.x, cursor_position.y - anchor_.y});
}

}  // namespace nativeapi
