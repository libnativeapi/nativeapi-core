// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Which concrete NotificationEvent arrived.
typedef enum {
  NATIVE_NOTIFICATION_EVENT_TYPE_ACTIVATED = 0,
} native_notification_event_type_t;

/// One NotificationEvent, tagged by its concrete type.
///
/// Valid only for the duration of the callback: anything it points at
/// is released as soon as the callback returns. Copy what you need.
typedef struct {
  native_notification_event_type_t type;
  union {
    struct {
      char* argument;
    } activated;
  } data;
} native_notification_event_t;

typedef void (*native_notification_event_callback_t)(const native_notification_event_t* event, void* user_data);

FFI_PLUGIN_EXPORT
bool native_notification_manager_is_supported(void);

FFI_PLUGIN_EXPORT
bool native_notification_manager_initialize(void);

FFI_PLUGIN_EXPORT
void native_notification_manager_shutdown(void);

FFI_PLUGIN_EXPORT
bool native_notification_manager_show(const char* title, const char* message, const char* tag, const char* button_label);

FFI_PLUGIN_EXPORT
bool native_notification_manager_remove(const char* tag);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_notification_manager_get_last_error(void);

/// Registers @p callback for every NotificationEvent this NotificationManager emits.
/// @return the listener id, or NATIVE_INVALID_LISTENER_ID on failure.
FFI_PLUGIN_EXPORT
native_listener_id_t native_notification_manager_add_listener(native_notification_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data);

/// Unregisters a listener. Returns false if unknown.
FFI_PLUGIN_EXPORT
bool native_notification_manager_remove_listener(native_listener_id_t listener_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nativeapi {
class NotificationEvent;
}  // namespace nativeapi

/// Fills @p out from @p event. Returns false when the event is not one
/// of the concrete types the C ABI knows about.
bool to_c_notification_event(const nativeapi::NotificationEvent& event, native_notification_event_t* out);
/// Releases everything to_c_notification_event() allocated.
void free_c_notification_event(native_notification_event_t* value);

#endif
