// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
typedef uint64_t native_menu_t;
typedef uint64_t native_window_t;

#include "menu_c.h"
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
  NATIVE_BRIGHTNESS_SYSTEM = 0,
  NATIVE_BRIGHTNESS_LIGHT = 1,
  NATIVE_BRIGHTNESS_DARK = 2,
} native_brightness_t;

/// Which concrete ApplicationEvent arrived.
typedef enum {
  NATIVE_APPLICATION_EVENT_TYPE_STARTED = 0,
  NATIVE_APPLICATION_EVENT_TYPE_EXITING = 1,
  NATIVE_APPLICATION_EVENT_TYPE_ACTIVATED = 2,
  NATIVE_APPLICATION_EVENT_TYPE_DEACTIVATED = 3,
  NATIVE_APPLICATION_EVENT_TYPE_QUIT_REQUESTED = 4,
} native_application_event_type_t;

/// One ApplicationEvent, tagged by its concrete type.
///
/// Valid only for the duration of the callback: anything it points at
/// is released as soon as the callback returns. Copy what you need.
typedef struct {
  native_application_event_type_t type;
  union {
    struct {
      int exit_code;
    } exiting;
  } data;
} native_application_event_t;

typedef void (*native_application_event_callback_t)(const native_application_event_t* event, void* user_data);

FFI_PLUGIN_EXPORT
int native_application_run(void);

FFI_PLUGIN_EXPORT
int native_application_run_with_window(native_window_t window);

FFI_PLUGIN_EXPORT
void native_application_quit(int exit_code);

FFI_PLUGIN_EXPORT
bool native_application_is_running(void);

FFI_PLUGIN_EXPORT
bool native_application_is_single_instance(void);

FFI_PLUGIN_EXPORT
bool native_application_set_icon(const char* icon_path);

FFI_PLUGIN_EXPORT
bool native_application_set_dock_icon_visible(bool visible);

FFI_PLUGIN_EXPORT
bool native_application_set_progress_bar(double progress);

FFI_PLUGIN_EXPORT
bool native_application_set_badge_label(const char* label);

FFI_PLUGIN_EXPORT
bool native_application_set_brightness(native_brightness_t brightness);

FFI_PLUGIN_EXPORT
bool native_application_set_menu_bar(native_menu_t menu);

/// Caller owns the returned handle; release it with native_window_free().
FFI_PLUGIN_EXPORT
native_window_t native_application_get_primary_window(void);

FFI_PLUGIN_EXPORT
void native_application_set_primary_window(native_window_t window);

FFI_PLUGIN_EXPORT
native_window_list_t native_application_get_all_windows(void);

/// Registers @p callback for every ApplicationEvent this Application emits.
/// @return the listener id, or NATIVE_INVALID_LISTENER_ID on failure.
FFI_PLUGIN_EXPORT
native_listener_id_t native_application_add_listener(native_application_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data);

/// Unregisters a listener. Returns false if unknown.
FFI_PLUGIN_EXPORT
bool native_application_remove_listener(native_listener_id_t listener_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nativeapi {
class ApplicationEvent;
}  // namespace nativeapi

/// Fills @p out from @p event. Returns false when the event is not one
/// of the concrete types the C ABI knows about.
bool to_c_application_event(const nativeapi::ApplicationEvent& event, native_application_event_t* out);
/// Releases everything to_c_application_event() allocated.
void free_c_application_event(native_application_event_t* value);

#endif

#ifdef __cplusplus
#include "../application.h"
#include "string_utils_c.h"
#include "user_data.h"

// Conversion helpers between these C types and their C++ originals.

inline native_brightness_t to_c_brightness(nativeapi::Brightness value);
inline nativeapi::Brightness to_cpp_brightness(native_brightness_t value);

inline native_brightness_t to_c_brightness(nativeapi::Brightness value) {
  switch (value) {
    case nativeapi::Brightness::System:
      return NATIVE_BRIGHTNESS_SYSTEM;
    case nativeapi::Brightness::Light:
      return NATIVE_BRIGHTNESS_LIGHT;
    case nativeapi::Brightness::Dark:
      return NATIVE_BRIGHTNESS_DARK;
    default:
      return NATIVE_BRIGHTNESS_SYSTEM;
  }
}

inline nativeapi::Brightness to_cpp_brightness(native_brightness_t value) {
  switch (value) {
    case NATIVE_BRIGHTNESS_SYSTEM:
      return nativeapi::Brightness::System;
    case NATIVE_BRIGHTNESS_LIGHT:
      return nativeapi::Brightness::Light;
    case NATIVE_BRIGHTNESS_DARK:
      return nativeapi::Brightness::Dark;
    default:
      return nativeapi::Brightness::System;
  }
}

#endif  // __cplusplus
