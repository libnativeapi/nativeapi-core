// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "window_drag_session_c.h"

#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "string_utils_c.h"
#include "user_data.h"
#include "../foundation/handle_table.h"
#include "../foundation/geometry.h"
#include "geometry_c.h"
#include "../window.h"
#include "window_c.h"
#include "../window_drag_session.h"

native_window_drag_session_t native_window_drag_session_create(void) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::WindowDragSession>());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_drag_session_create");
    return 0;
  }
}

bool native_window_drag_session_start(native_window_drag_session_t window_drag_session, native_window_t window, native_point_t anchor) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowDragSession>(window_drag_session);
  if (!self) {
    return false;
  }
  try {
    auto window_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Window>(window);
    auto anchor_cpp = to_cpp_point(anchor);
    return self->Start(window_cpp, anchor_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_drag_session_start");
    return false;
  }
}

void native_window_drag_session_cancel(native_window_drag_session_t window_drag_session) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowDragSession>(window_drag_session);
  if (!self) {
    return;
  }
  try {
    self->Cancel();
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_drag_session_cancel");
    return;
  }
}

bool native_window_drag_session_is_active(native_window_drag_session_t window_drag_session) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowDragSession>(window_drag_session);
  if (!self) {
    return false;
  }
  try {
    return self->IsActive();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_drag_session_is_active");
    return false;
  }
}

native_window_id_t native_window_drag_session_get_window_id(native_window_drag_session_t window_drag_session) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowDragSession>(window_drag_session);
  if (!self) {
    return 0;
  }
  try {
    return self->GetWindowId();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_drag_session_get_window_id");
    return 0;
  }
}

native_point_t native_window_drag_session_get_anchor(native_window_drag_session_t window_drag_session) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowDragSession>(window_drag_session);
  if (!self) {
    native_point_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetAnchor();
    return to_c_point(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_drag_session_get_anchor");
    native_point_t result = {};
    return result;
  }
}

void native_window_drag_session_free(native_window_drag_session_t window_drag_session) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(window_drag_session);
}

native_listener_id_t native_window_drag_session_add_listener(native_window_drag_session_t window_drag_session, native_window_drag_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) {
    return 0;
  }
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowDragSession>(window_drag_session);
  if (!self) {
    return 0;
  }
  try {
    return static_cast<native_listener_id_t>(self->AddListener<nativeapi::WindowDragEvent>(
        [callback, holder](const nativeapi::WindowDragEvent& event) {
          native_window_drag_event_t c_event = {};
          if (!to_c_window_drag_event(event, &c_event)) {
            return;
          }
          callback(&c_event, holder->get());
          free_c_window_drag_event(&c_event);
        }));
  } catch (...) {
    return 0;
  }
}

bool native_window_drag_session_remove_listener(native_window_drag_session_t window_drag_session, native_listener_id_t listener_id) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowDragSession>(window_drag_session);
  if (!self) {
    return false;
  }
  try {
    return self->RemoveListener(static_cast<size_t>(listener_id));
  } catch (...) {
    return false;
  }
}

bool to_c_window_drag_event(const nativeapi::WindowDragEvent& event, native_window_drag_event_t* out) {
  if (!out) {
    return false;
  }
  *out = native_window_drag_event_t{};
  out->window_id = event.GetWindowId();
  out->cursor_position = to_c_point(event.GetCursorPosition());
  if (const auto* typed = dynamic_cast<const nativeapi::WindowDragMovedEvent*>(&event)) {
    out->type = NATIVE_WINDOW_DRAG_EVENT_TYPE_MOVED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::WindowDragEndedEvent*>(&event)) {
    out->type = NATIVE_WINDOW_DRAG_EVENT_TYPE_ENDED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::WindowDragCancelledEvent*>(&event)) {
    out->type = NATIVE_WINDOW_DRAG_EVENT_TYPE_CANCELLED;
    (void)typed;
    return true;
  }
  return false;
}

void free_c_window_drag_event(native_window_drag_event_t* value) {
  if (!value) {
    return;
  }
}

