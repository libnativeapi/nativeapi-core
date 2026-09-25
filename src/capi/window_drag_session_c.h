// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
typedef uint64_t native_window_t;

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

/// Opaque WindowDragSession handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_WINDOW_DRAG_SESSION rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_window_drag_session_t;

/// Never refers to a live WindowDragSession.
#define NATIVE_INVALID_WINDOW_DRAG_SESSION ((native_window_drag_session_t)0)

/// Which concrete WindowDragEvent arrived.
typedef enum {
  NATIVE_WINDOW_DRAG_EVENT_TYPE_MOVED = 0,
  NATIVE_WINDOW_DRAG_EVENT_TYPE_ENDED = 1,
  NATIVE_WINDOW_DRAG_EVENT_TYPE_CANCELLED = 2,
} native_window_drag_event_type_t;

/// One WindowDragEvent, tagged by its concrete type.
///
/// Valid only for the duration of the callback: anything it points at
/// is released as soon as the callback returns. Copy what you need.
typedef struct {
  native_window_drag_event_type_t type;
  native_window_id_t window_id;
  native_point_t cursor_position;
} native_window_drag_event_t;

typedef void (*native_window_drag_event_callback_t)(const native_window_drag_event_t* event, void* user_data);

/// Creates a WindowDragSession instance; release it with native_window_drag_session_free().
FFI_PLUGIN_EXPORT
native_window_drag_session_t native_window_drag_session_create(void);

FFI_PLUGIN_EXPORT
bool native_window_drag_session_start(native_window_drag_session_t window_drag_session, native_window_t window, native_point_t anchor);

FFI_PLUGIN_EXPORT
void native_window_drag_session_cancel(native_window_drag_session_t window_drag_session);

FFI_PLUGIN_EXPORT
bool native_window_drag_session_is_active(native_window_drag_session_t window_drag_session);

FFI_PLUGIN_EXPORT
native_window_id_t native_window_drag_session_get_window_id(native_window_drag_session_t window_drag_session);

FFI_PLUGIN_EXPORT
native_point_t native_window_drag_session_get_anchor(native_window_drag_session_t window_drag_session);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_window_drag_session_free(native_window_drag_session_t window_drag_session);

/// Registers @p callback for every WindowDragEvent this WindowDragSession emits.
/// @return the listener id, or NATIVE_INVALID_LISTENER_ID on failure.
FFI_PLUGIN_EXPORT
native_listener_id_t native_window_drag_session_add_listener(native_window_drag_session_t window_drag_session, native_window_drag_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data);

/// Unregisters a listener. Returns false if unknown.
FFI_PLUGIN_EXPORT
bool native_window_drag_session_remove_listener(native_window_drag_session_t window_drag_session, native_listener_id_t listener_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nativeapi {
class WindowDragEvent;
}  // namespace nativeapi

/// Fills @p out from @p event. Returns false when the event is not one
/// of the concrete types the C ABI knows about.
bool to_c_window_drag_event(const nativeapi::WindowDragEvent& event, native_window_drag_event_t* out);
/// Releases everything to_c_window_drag_event() allocated.
void free_c_window_drag_event(native_window_drag_event_t* value);

#endif
