// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "drag_source_c.h"

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
#include "../image.h"
#include "image_c.h"
#include "../window.h"
#include "window_c.h"
#include "../drag_source.h"

native_drag_source_t native_drag_source_create(void) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::DragSource>());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_create");
    return 0;
  }
}

bool native_drag_source_is_supported(void) {
  try {
    return nativeapi::DragSource::IsSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_is_supported");
    return false;
  }
}

void native_drag_source_set_file_paths(native_drag_source_t drag_source, native_string_list_t file_paths) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return;
  }
  try {
    std::vector<std::string> file_paths_cpp;
    for (long i = 0; i < file_paths.count; ++i) {
      file_paths_cpp.emplace_back(file_paths.items[i] ? file_paths.items[i] : "");
    }
    self->SetFilePaths(file_paths_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_set_file_paths");
    return;
  }
}

native_string_list_t native_drag_source_get_file_paths(native_drag_source_t drag_source) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    native_string_list_t empty = {};
    return empty;
  }
  try {
    return to_c_string_list(self->GetFilePaths());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_get_file_paths");
    native_string_list_t empty = {};
    return empty;
  }
}

void native_drag_source_set_text(native_drag_source_t drag_source, const char* text) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return;
  }
  try {
    std::optional<std::string> text_cpp;
    if (text) {
      text_cpp = std::string(text);
    }
    self->SetText(text_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_set_text");
    return;
  }
}

char* native_drag_source_get_text(native_drag_source_t drag_source) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return nullptr;
  }
  try {
    const auto cpp_result = self->GetText();
    return cpp_result ? to_c_str(*cpp_result) : nullptr;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_get_text");
    return nullptr;
  }
}

void native_drag_source_set_image(native_drag_source_t drag_source, native_image_t image) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return;
  }
  try {
    auto image_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Image>(image);
    self->SetImage(image_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_set_image");
    return;
  }
}

native_image_t native_drag_source_get_image(native_drag_source_t drag_source) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return 0;
  }
  try {
    return nativeapi::HandleTable::GetInstance().Insert(self->GetImage());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_get_image");
    return 0;
  }
}

void native_drag_source_set_drag_operation(native_drag_source_t drag_source, native_drag_operation_t operation) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return;
  }
  try {
    self->SetDragOperation(to_cpp_drag_operation(operation));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_set_drag_operation");
    return;
  }
}

native_drag_operation_t native_drag_source_get_drag_operation(native_drag_source_t drag_source) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return (native_drag_operation_t)NATIVE_DRAG_OPERATION_NONE;
  }
  try {
    return to_c_drag_operation(self->GetDragOperation());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_get_drag_operation");
    return (native_drag_operation_t)NATIVE_DRAG_OPERATION_NONE;
  }
}

bool native_drag_source_start_dragging(native_drag_source_t drag_source, native_window_t window) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return false;
  }
  try {
    auto window_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Window>(window);
    return self->StartDragging(window_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_start_dragging");
    return false;
  }
}

bool native_drag_source_is_dragging(native_drag_source_t drag_source) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return false;
  }
  try {
    return self->IsDragging();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_drag_source_is_dragging");
    return false;
  }
}

void native_drag_source_free(native_drag_source_t drag_source) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(drag_source);
}

native_listener_id_t native_drag_source_add_listener(native_drag_source_t drag_source, native_drag_source_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) {
    return 0;
  }
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return 0;
  }
  try {
    return static_cast<native_listener_id_t>(self->AddListener<nativeapi::DragSourceEvent>(
        [callback, holder](const nativeapi::DragSourceEvent& event) {
          native_drag_source_event_t c_event = {};
          if (!to_c_drag_source_event(event, &c_event)) {
            return;
          }
          callback(&c_event, holder->get());
          free_c_drag_source_event(&c_event);
        }));
  } catch (...) {
    return 0;
  }
}

bool native_drag_source_remove_listener(native_drag_source_t drag_source, native_listener_id_t listener_id) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::DragSource>(drag_source);
  if (!self) {
    return false;
  }
  try {
    return self->RemoveListener(static_cast<size_t>(listener_id));
  } catch (...) {
    return false;
  }
}

bool to_c_drag_source_event(const nativeapi::DragSourceEvent& event, native_drag_source_event_t* out) {
  if (!out) {
    return false;
  }
  *out = native_drag_source_event_t{};
  out->window_id = event.GetWindowId();
  out->position = to_c_point(event.GetPosition());
  if (const auto* typed = dynamic_cast<const nativeapi::DragSourceEndedEvent*>(&event)) {
    out->type = NATIVE_DRAG_SOURCE_EVENT_TYPE_ENDED;
    out->data.ended.operation = to_c_drag_operation(typed->GetOperation());
    return true;
  }
  return false;
}

void free_c_drag_source_event(native_drag_source_event_t* value) {
  if (!value) {
    return;
  }
}

