// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "drop_target_c.h"

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
#include "../drag_source.h"
#include "drag_source_c.h"
#include "../drop_target.h"

native_drop_target_t native_drop_target_create(native_window_t window) {
  try {
    auto window_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Window>(window);
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::DropTarget>(window_cpp));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drop_target_create");
    return 0;
  }
}

bool native_drop_target_is_supported(void) {
  try {
    return nativeapi::DropTarget::IsSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drop_target_is_supported");
    return false;
  }
}

native_window_id_t native_drop_target_get_window_id(native_drop_target_t drop_target) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DropTarget>(drop_target);
  if (!self) {
    return 0;
  }
  try {
    return self->GetWindowId();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drop_target_get_window_id");
    return 0;
  }
}

void native_drop_target_set_drop_operation(native_drop_target_t drop_target, native_drag_operation_t operation) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DropTarget>(drop_target);
  if (!self) {
    return;
  }
  try {
    self->SetDropOperation(to_cpp_drag_operation(operation));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drop_target_set_drop_operation");
    return;
  }
}

native_drag_operation_t native_drop_target_get_drop_operation(native_drop_target_t drop_target) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DropTarget>(drop_target);
  if (!self) {
    return (native_drag_operation_t)0;
  }
  try {
    return to_c_drag_operation(self->GetDropOperation());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drop_target_get_drop_operation");
    return (native_drag_operation_t)0;
  }
}

bool native_drop_target_is_active(native_drop_target_t drop_target) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DropTarget>(drop_target);
  if (!self) {
    return false;
  }
  try {
    return self->IsActive();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drop_target_is_active");
    return false;
  }
}

void native_drop_target_free(native_drop_target_t drop_target) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(drop_target);
}

native_listener_id_t native_drop_target_add_listener(native_drop_target_t drop_target, native_drop_target_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) {
    return 0;
  }
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DropTarget>(drop_target);
  if (!self) {
    return 0;
  }
  try {
    return static_cast<native_listener_id_t>(self->AddListener<nativeapi::DropTargetEvent>(
        [callback, holder](const nativeapi::DropTargetEvent& event) {
          native_drop_target_event_t c_event = {};
          if (!to_c_drop_target_event(event, &c_event)) {
            return;
          }
          callback(&c_event, holder->get());
          free_c_drop_target_event(&c_event);
        }));
  } catch (...) {
    return 0;
  }
}

bool native_drop_target_remove_listener(native_drop_target_t drop_target, native_listener_id_t listener_id) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DropTarget>(drop_target);
  if (!self) {
    return false;
  }
  try {
    return self->RemoveListener(static_cast<size_t>(listener_id));
  } catch (...) {
    return false;
  }
}

bool to_c_drop_target_event(const nativeapi::DropTargetEvent& event, native_drop_target_event_t* out) {
  if (!out) {
    return false;
  }
  *out = native_drop_target_event_t{};
  out->window_id = event.GetWindowId();
  out->position = to_c_point(event.GetPosition());
  if (const auto* typed = dynamic_cast<const nativeapi::DropTargetEnteredEvent*>(&event)) {
    out->type = NATIVE_DROP_TARGET_EVENT_TYPE_ENTERED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::DropTargetMovedEvent*>(&event)) {
    out->type = NATIVE_DROP_TARGET_EVENT_TYPE_MOVED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::DropTargetExitedEvent*>(&event)) {
    out->type = NATIVE_DROP_TARGET_EVENT_TYPE_EXITED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::DropTargetDroppedEvent*>(&event)) {
    out->type = NATIVE_DROP_TARGET_EVENT_TYPE_DROPPED;
    out->data.dropped.file_paths = to_c_string_list(typed->GetFilePaths());
    out->data.dropped.text = to_c_str(typed->GetText());
    return true;
  }
  return false;
}

void free_c_drop_target_event(native_drop_target_event_t* value) {
  if (!value) {
    return;
  }
  if (value->type == NATIVE_DROP_TARGET_EVENT_TYPE_DROPPED) {
    native_string_list_free(&value->data.dropped.file_paths);
    free_c_str(value->data.dropped.text);
    value->data.dropped.text = nullptr;
  }
}

