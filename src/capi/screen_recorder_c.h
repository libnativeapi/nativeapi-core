// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
typedef uint64_t native_capture_source_t;

#include "capture_source_c.h"
#include "screen_capturer_c.h"

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  NATIVE_SCREEN_RECORDING_STATE_IDLE = 0,
  NATIVE_SCREEN_RECORDING_STATE_STARTING = 1,
  NATIVE_SCREEN_RECORDING_STATE_RECORDING = 2,
  NATIVE_SCREEN_RECORDING_STATE_STOPPING = 3,
  NATIVE_SCREEN_RECORDING_STATE_COMPLETED = 4,
  NATIVE_SCREEN_RECORDING_STATE_CANCELLED = 5,
  NATIVE_SCREEN_RECORDING_STATE_FAILED = 6,
} native_screen_recording_state_t;

typedef struct {
  native_screen_capture_options_t capture;
  double frame_rate;
  unsigned long video_bitrate;
} native_screen_recording_options_t;

/// Opaque ScreenRecorder handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_SCREEN_RECORDER rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_screen_recorder_t;

/// Never refers to a live ScreenRecorder.
#define NATIVE_INVALID_SCREEN_RECORDER ((native_screen_recorder_t)0)

/// Which concrete ScreenRecordingEvent arrived.
typedef enum {
  NATIVE_SCREEN_RECORDING_EVENT_TYPE_STARTED = 0,
  NATIVE_SCREEN_RECORDING_EVENT_TYPE_COMPLETED = 1,
  NATIVE_SCREEN_RECORDING_EVENT_TYPE_CANCELLED = 2,
  NATIVE_SCREEN_RECORDING_EVENT_TYPE_FAILED = 3,
} native_screen_recording_event_type_t;

/// One ScreenRecordingEvent, tagged by its concrete type.
///
/// Synchronous callbacks borrow this payload until they return. Async callbacks
/// borrow it until event_delivery_complete. Copy anything needed after that.
typedef struct {
  native_screen_recording_event_type_t type;
  union {
    struct {
      char* file_path;
      double duration;
    } completed;
    struct {
      char* message;
    } failed;
  } data;
} native_screen_recording_event_t;

typedef void (*native_screen_recording_event_callback_t)(const native_screen_recording_event_t* event, void* user_data);
typedef void (*native_screen_recording_event_callback_t_async)(const native_screen_recording_event_t* event, native_event_delivery_t delivery, void* user_data);

/// Frees everything the struct owns.
FFI_PLUGIN_EXPORT
void native_screen_recording_options_free(native_screen_recording_options_t* value);

/// Creates a ScreenRecorder instance; release it with native_screen_recorder_free().
FFI_PLUGIN_EXPORT
native_screen_recorder_t native_screen_recorder_create(void);

FFI_PLUGIN_EXPORT
bool native_screen_recorder_is_supported(void);

FFI_PLUGIN_EXPORT
bool native_screen_recorder_start(native_screen_recorder_t screen_recorder, native_capture_source_t source, const char* file_path, native_screen_recording_options_t options);

FFI_PLUGIN_EXPORT
bool native_screen_recorder_stop(native_screen_recorder_t screen_recorder);

FFI_PLUGIN_EXPORT
bool native_screen_recorder_cancel(native_screen_recorder_t screen_recorder);

FFI_PLUGIN_EXPORT
native_screen_recording_state_t native_screen_recorder_get_state(native_screen_recorder_t screen_recorder);

FFI_PLUGIN_EXPORT
bool native_screen_recorder_is_active(native_screen_recorder_t screen_recorder);

FFI_PLUGIN_EXPORT
double native_screen_recorder_get_duration(native_screen_recorder_t screen_recorder);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_screen_recorder_get_file_path(native_screen_recorder_t screen_recorder);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_screen_recorder_free(native_screen_recorder_t screen_recorder);

/// Registers @p callback for every ScreenRecordingEvent this ScreenRecorder emits.
/// @return the listener id, or NATIVE_INVALID_LISTENER_ID on failure.
FFI_PLUGIN_EXPORT
native_listener_id_t native_screen_recorder_add_listener(native_screen_recorder_t screen_recorder, native_screen_recording_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data);

/// Registers an asynchronous callback. Its event, borrowed handles and user_data
/// remain valid until event_delivery_complete is called, including after removal.
/// Every delivered payload must be acknowledged. Check is_active before invoking a queued callback.
FFI_PLUGIN_EXPORT
native_listener_id_t native_screen_recorder_add_listener_async(native_screen_recorder_t screen_recorder, native_screen_recording_event_callback_t_async callback, void* user_data, native_release_user_data_t release_user_data);

/// Unregisters a listener. Returns false if unknown.
FFI_PLUGIN_EXPORT
bool native_screen_recorder_remove_listener(native_screen_recorder_t screen_recorder, native_listener_id_t listener_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nativeapi {
class ScreenRecordingEvent;
}  // namespace nativeapi

/// Fills @p out from @p event. Returns false when the event is not one
/// of the concrete types the C ABI knows about.
bool to_c_screen_recording_event(const nativeapi::ScreenRecordingEvent& event, native_screen_recording_event_t* out);
/// Releases everything to_c_screen_recording_event() allocated.
void free_c_screen_recording_event(native_screen_recording_event_t* value);

#endif

#ifdef __cplusplus
#include "../screen_recorder.h"
#include "string_utils_c.h"
#include "user_data.h"

// Conversion helpers between these C types and their C++ originals.

inline native_screen_recording_state_t to_c_screen_recording_state(nativeapi::ScreenRecordingState value);
inline nativeapi::ScreenRecordingState to_cpp_screen_recording_state(native_screen_recording_state_t value);
inline native_screen_recording_options_t to_c_screen_recording_options(const nativeapi::ScreenRecordingOptions& value);
inline nativeapi::ScreenRecordingOptions to_cpp_screen_recording_options(const native_screen_recording_options_t& value);

inline native_screen_recording_state_t to_c_screen_recording_state(nativeapi::ScreenRecordingState value) {
  switch (value) {
    case nativeapi::ScreenRecordingState::Idle:
      return NATIVE_SCREEN_RECORDING_STATE_IDLE;
    case nativeapi::ScreenRecordingState::Starting:
      return NATIVE_SCREEN_RECORDING_STATE_STARTING;
    case nativeapi::ScreenRecordingState::Recording:
      return NATIVE_SCREEN_RECORDING_STATE_RECORDING;
    case nativeapi::ScreenRecordingState::Stopping:
      return NATIVE_SCREEN_RECORDING_STATE_STOPPING;
    case nativeapi::ScreenRecordingState::Completed:
      return NATIVE_SCREEN_RECORDING_STATE_COMPLETED;
    case nativeapi::ScreenRecordingState::Cancelled:
      return NATIVE_SCREEN_RECORDING_STATE_CANCELLED;
    case nativeapi::ScreenRecordingState::Failed:
      return NATIVE_SCREEN_RECORDING_STATE_FAILED;
    default:
      return NATIVE_SCREEN_RECORDING_STATE_IDLE;
  }
}

inline nativeapi::ScreenRecordingState to_cpp_screen_recording_state(native_screen_recording_state_t value) {
  switch (value) {
    case NATIVE_SCREEN_RECORDING_STATE_IDLE:
      return nativeapi::ScreenRecordingState::Idle;
    case NATIVE_SCREEN_RECORDING_STATE_STARTING:
      return nativeapi::ScreenRecordingState::Starting;
    case NATIVE_SCREEN_RECORDING_STATE_RECORDING:
      return nativeapi::ScreenRecordingState::Recording;
    case NATIVE_SCREEN_RECORDING_STATE_STOPPING:
      return nativeapi::ScreenRecordingState::Stopping;
    case NATIVE_SCREEN_RECORDING_STATE_COMPLETED:
      return nativeapi::ScreenRecordingState::Completed;
    case NATIVE_SCREEN_RECORDING_STATE_CANCELLED:
      return nativeapi::ScreenRecordingState::Cancelled;
    case NATIVE_SCREEN_RECORDING_STATE_FAILED:
      return nativeapi::ScreenRecordingState::Failed;
    default:
      return static_cast<nativeapi::ScreenRecordingState>(value);
  }
}

inline native_screen_recording_options_t to_c_screen_recording_options(const nativeapi::ScreenRecordingOptions& value) {
  native_screen_recording_options_t result = {};
  result.capture = to_c_screen_capture_options(value.capture);
  result.frame_rate = value.frame_rate;
  result.video_bitrate = value.video_bitrate;
  return result;
}

inline nativeapi::ScreenRecordingOptions to_cpp_screen_recording_options(const native_screen_recording_options_t& value) {
  nativeapi::ScreenRecordingOptions result = {};
  result.capture = to_cpp_screen_capture_options(value.capture);
  result.frame_rate = value.frame_rate;
  result.video_bitrate = value.video_bitrate;
  return result;
}

#endif  // __cplusplus
