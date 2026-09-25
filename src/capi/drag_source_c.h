// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
#include "string_utils_c.h"
#include "geometry_c.h"
#include "image_c.h"
#include "window_c.h"

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  NATIVE_DRAG_OPERATION_NONE = 0,
  NATIVE_DRAG_OPERATION_COPY = 1,
  NATIVE_DRAG_OPERATION_MOVE = 2,
  NATIVE_DRAG_OPERATION_LINK = 3,
} native_drag_operation_t;

/// Opaque DragSource handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_DRAG_SOURCE rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_drag_source_t;

/// Never refers to a live DragSource.
#define NATIVE_INVALID_DRAG_SOURCE ((native_drag_source_t)0)

/// Which concrete DragSourceEvent arrived.
typedef enum {
  NATIVE_DRAG_SOURCE_EVENT_TYPE_ENDED = 0,
} native_drag_source_event_type_t;

/// One DragSourceEvent, tagged by its concrete type.
///
/// Valid only for the duration of the callback: anything it points at
/// is released as soon as the callback returns. Copy what you need.
typedef struct {
  native_drag_source_event_type_t type;
  native_window_id_t window_id;
  native_point_t position;
  union {
    struct {
      native_drag_operation_t operation;
    } ended;
  } data;
} native_drag_source_event_t;

typedef void (*native_drag_source_event_callback_t)(const native_drag_source_event_t* event, void* user_data);

/// Creates a DragSource instance; release it with native_drag_source_free().
FFI_PLUGIN_EXPORT
native_drag_source_t native_drag_source_create(void);

FFI_PLUGIN_EXPORT
bool native_drag_source_is_supported(void);

FFI_PLUGIN_EXPORT
void native_drag_source_set_file_paths(native_drag_source_t drag_source, native_string_list_t file_paths);

FFI_PLUGIN_EXPORT
native_string_list_t native_drag_source_get_file_paths(native_drag_source_t drag_source);

FFI_PLUGIN_EXPORT
void native_drag_source_set_text(native_drag_source_t drag_source, const char* text);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_drag_source_get_text(native_drag_source_t drag_source);

FFI_PLUGIN_EXPORT
void native_drag_source_set_image(native_drag_source_t drag_source, native_image_t image);

/// Caller owns the returned handle; release it with native_image_free().
FFI_PLUGIN_EXPORT
native_image_t native_drag_source_get_image(native_drag_source_t drag_source);

FFI_PLUGIN_EXPORT
void native_drag_source_set_drag_operation(native_drag_source_t drag_source, native_drag_operation_t operation);

FFI_PLUGIN_EXPORT
native_drag_operation_t native_drag_source_get_drag_operation(native_drag_source_t drag_source);

FFI_PLUGIN_EXPORT
bool native_drag_source_start_dragging(native_drag_source_t drag_source, native_window_t window);

FFI_PLUGIN_EXPORT
bool native_drag_source_is_dragging(native_drag_source_t drag_source);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_drag_source_free(native_drag_source_t drag_source);

/// Registers @p callback for every DragSourceEvent this DragSource emits.
/// @return the listener id, or NATIVE_INVALID_LISTENER_ID on failure.
FFI_PLUGIN_EXPORT
native_listener_id_t native_drag_source_add_listener(native_drag_source_t drag_source, native_drag_source_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data);

/// Unregisters a listener. Returns false if unknown.
FFI_PLUGIN_EXPORT
bool native_drag_source_remove_listener(native_drag_source_t drag_source, native_listener_id_t listener_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nativeapi {
class DragSourceEvent;
}  // namespace nativeapi

/// Fills @p out from @p event. Returns false when the event is not one
/// of the concrete types the C ABI knows about.
bool to_c_drag_source_event(const nativeapi::DragSourceEvent& event, native_drag_source_event_t* out);
/// Releases everything to_c_drag_source_event() allocated.
void free_c_drag_source_event(native_drag_source_event_t* value);

#endif

#ifdef __cplusplus
#include "../drag_source.h"
#include "string_utils_c.h"
#include "user_data.h"

// Conversion helpers between these C types and their C++ originals.

inline native_drag_operation_t to_c_drag_operation(nativeapi::DragOperation value);
inline nativeapi::DragOperation to_cpp_drag_operation(native_drag_operation_t value);

inline native_drag_operation_t to_c_drag_operation(nativeapi::DragOperation value) {
  switch (value) {
    case nativeapi::DragOperation::None:
      return NATIVE_DRAG_OPERATION_NONE;
    case nativeapi::DragOperation::Copy:
      return NATIVE_DRAG_OPERATION_COPY;
    case nativeapi::DragOperation::Move:
      return NATIVE_DRAG_OPERATION_MOVE;
    case nativeapi::DragOperation::Link:
      return NATIVE_DRAG_OPERATION_LINK;
    default:
      return NATIVE_DRAG_OPERATION_NONE;
  }
}

inline nativeapi::DragOperation to_cpp_drag_operation(native_drag_operation_t value) {
  switch (value) {
    case NATIVE_DRAG_OPERATION_NONE:
      return nativeapi::DragOperation::None;
    case NATIVE_DRAG_OPERATION_COPY:
      return nativeapi::DragOperation::Copy;
    case NATIVE_DRAG_OPERATION_MOVE:
      return nativeapi::DragOperation::Move;
    case NATIVE_DRAG_OPERATION_LINK:
      return nativeapi::DragOperation::Link;
    default:
      return nativeapi::DragOperation::None;
  }
}

#endif  // __cplusplus
