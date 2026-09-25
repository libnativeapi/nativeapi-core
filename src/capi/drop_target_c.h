// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
#include "drag_source_c.h"
#include "geometry_c.h"
#include "window_c.h"

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque DropTarget handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_DROP_TARGET rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_drop_target_t;

/// Never refers to a live DropTarget.
#define NATIVE_INVALID_DROP_TARGET ((native_drop_target_t)0)

/// Which concrete DropTargetEvent arrived.
typedef enum {
  NATIVE_DROP_TARGET_EVENT_TYPE_ENTERED = 0,
  NATIVE_DROP_TARGET_EVENT_TYPE_MOVED = 1,
  NATIVE_DROP_TARGET_EVENT_TYPE_EXITED = 2,
  NATIVE_DROP_TARGET_EVENT_TYPE_DROPPED = 3,
} native_drop_target_event_type_t;

/// One DropTargetEvent, tagged by its concrete type.
///
/// Valid only for the duration of the callback: anything it points at
/// is released as soon as the callback returns. Copy what you need.
typedef struct {
  native_drop_target_event_type_t type;
  native_window_id_t window_id;
  native_point_t position;
  union {
    struct {
      native_string_list_t file_paths;
      char* text;
    } dropped;
  } data;
} native_drop_target_event_t;

typedef void (*native_drop_target_event_callback_t)(const native_drop_target_event_t* event, void* user_data);

/// Creates a DropTarget instance; release it with native_drop_target_free().
FFI_PLUGIN_EXPORT
native_drop_target_t native_drop_target_create(native_window_t window);

FFI_PLUGIN_EXPORT
bool native_drop_target_is_supported(void);

FFI_PLUGIN_EXPORT
native_window_id_t native_drop_target_get_window_id(native_drop_target_t drop_target);

FFI_PLUGIN_EXPORT
void native_drop_target_set_drop_operation(native_drop_target_t drop_target, native_drag_operation_t operation);

FFI_PLUGIN_EXPORT
native_drag_operation_t native_drop_target_get_drop_operation(native_drop_target_t drop_target);

FFI_PLUGIN_EXPORT
bool native_drop_target_is_active(native_drop_target_t drop_target);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_drop_target_free(native_drop_target_t drop_target);

/// Registers @p callback for every DropTargetEvent this DropTarget emits.
/// @return the listener id, or NATIVE_INVALID_LISTENER_ID on failure.
FFI_PLUGIN_EXPORT
native_listener_id_t native_drop_target_add_listener(native_drop_target_t drop_target, native_drop_target_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data);

/// Unregisters a listener. Returns false if unknown.
FFI_PLUGIN_EXPORT
bool native_drop_target_remove_listener(native_drop_target_t drop_target, native_listener_id_t listener_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nativeapi {
class DropTargetEvent;
}  // namespace nativeapi

/// Fills @p out from @p event. Returns false when the event is not one
/// of the concrete types the C ABI knows about.
bool to_c_drop_target_event(const nativeapi::DropTargetEvent& event, native_drop_target_event_t* out);
/// Releases everything to_c_drop_target_event() allocated.
void free_c_drop_target_event(native_drop_target_event_t* value);

#endif
