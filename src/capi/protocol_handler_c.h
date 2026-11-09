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

/// Which concrete ProtocolHandlerEvent arrived.
typedef enum {
  NATIVE_PROTOCOL_HANDLER_EVENT_TYPE_PROTOCOL_URL_RECEIVED = 0,
} native_protocol_handler_event_type_t;

/// One ProtocolHandlerEvent, tagged by its concrete type.
///
/// Synchronous callbacks borrow this payload until they return. Async callbacks
/// borrow it until event_delivery_complete. Copy anything needed after that.
typedef struct {
  native_protocol_handler_event_type_t type;
  union {
    struct {
      char* url;
    } protocol_url_received;
  } data;
} native_protocol_handler_event_t;

typedef void (*native_protocol_handler_event_callback_t)(const native_protocol_handler_event_t* event, void* user_data);
typedef void (*native_protocol_handler_event_callback_t_async)(const native_protocol_handler_event_t* event, native_event_delivery_t delivery, void* user_data);

FFI_PLUGIN_EXPORT
bool native_protocol_handler_is_supported(void);

FFI_PLUGIN_EXPORT
bool native_protocol_handler_register(const char* scheme);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_protocol_handler_get_initial_url(void);

/// Registers @p callback for every ProtocolHandlerEvent this ProtocolHandler emits.
/// @return the listener id, or NATIVE_INVALID_LISTENER_ID on failure.
FFI_PLUGIN_EXPORT
native_listener_id_t native_protocol_handler_add_listener(native_protocol_handler_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data);

/// Registers an asynchronous callback. Its event, borrowed handles and user_data
/// remain valid until event_delivery_complete is called, including after removal.
/// Every delivered payload must be acknowledged. Check is_active before invoking a queued callback.
FFI_PLUGIN_EXPORT
native_listener_id_t native_protocol_handler_add_listener_async(native_protocol_handler_event_callback_t_async callback, void* user_data, native_release_user_data_t release_user_data);

/// Unregisters a listener. Returns false if unknown.
FFI_PLUGIN_EXPORT
bool native_protocol_handler_remove_listener(native_listener_id_t listener_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nativeapi {
class ProtocolHandlerEvent;
}  // namespace nativeapi

/// Fills @p out from @p event. Returns false when the event is not one
/// of the concrete types the C ABI knows about.
bool to_c_protocol_handler_event(const nativeapi::ProtocolHandlerEvent& event, native_protocol_handler_event_t* out);
/// Releases everything to_c_protocol_handler_event() allocated.
void free_c_protocol_handler_event(native_protocol_handler_event_t* value);

#endif
