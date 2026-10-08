// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
#include "string_utils_c.h"
typedef uint64_t native_image_t;

#include "image_c.h"

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct native_clipboard_data_t native_clipboard_data_t;
/// Arguments are borrowed until event_delivery_complete(delivery, ...), which is required exactly once.
typedef void (*native_clipboard_read_callback_t)(bool arg0, const native_clipboard_data_t* arg1, native_event_delivery_t delivery, void* user_data);

/// Arguments are borrowed until event_delivery_complete(delivery, ...), which is required exactly once.
typedef void (*native_clipboard_read_text_callback_t)(bool arg0, const char* arg1, native_event_delivery_t delivery, void* user_data);

/// Arguments are borrowed until event_delivery_complete(delivery, ...), which is required exactly once.
typedef void (*native_clipboard_read_html_callback_t)(bool arg0, const char* arg1, native_event_delivery_t delivery, void* user_data);

/// Arguments are borrowed until event_delivery_complete(delivery, ...), which is required exactly once.
typedef void (*native_clipboard_read_image_callback_t)(bool arg0, native_image_t arg1, native_event_delivery_t delivery, void* user_data);

/// Arguments are borrowed until event_delivery_complete(delivery, ...), which is required exactly once.
typedef void (*native_clipboard_read_file_paths_callback_t)(bool arg0, const native_string_list_t* arg1, native_event_delivery_t delivery, void* user_data);

struct native_clipboard_data_t {
  char* text;
  char* html;
  native_image_t image;
  native_string_list_t file_paths;
};

/// Which concrete ClipboardEvent arrived.
typedef enum {
  NATIVE_CLIPBOARD_EVENT_TYPE_CHANGED = 0,
} native_clipboard_event_type_t;

/// One ClipboardEvent, tagged by its concrete type.
///
/// Synchronous callbacks borrow this payload until they return. Async callbacks
/// borrow it until event_delivery_complete. Copy anything needed after that.
typedef struct {
  native_clipboard_event_type_t type;
} native_clipboard_event_t;

typedef void (*native_clipboard_event_callback_t)(const native_clipboard_event_t* event, void* user_data);
typedef void (*native_clipboard_event_callback_t_async)(const native_clipboard_event_t* event, native_event_delivery_t delivery, void* user_data);

/// Frees everything the struct owns.
FFI_PLUGIN_EXPORT
void native_clipboard_data_free(native_clipboard_data_t* value);

FFI_PLUGIN_EXPORT
bool native_clipboard_is_supported(void);

FFI_PLUGIN_EXPORT
bool native_clipboard_is_change_monitoring_supported(void);

FFI_PLUGIN_EXPORT
void native_clipboard_read(native_clipboard_read_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data);

FFI_PLUGIN_EXPORT
void native_clipboard_read_text(native_clipboard_read_text_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data);

FFI_PLUGIN_EXPORT
void native_clipboard_read_html(native_clipboard_read_html_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data);

FFI_PLUGIN_EXPORT
void native_clipboard_read_image(native_clipboard_read_image_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data);

FFI_PLUGIN_EXPORT
void native_clipboard_read_file_paths(native_clipboard_read_file_paths_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data);

FFI_PLUGIN_EXPORT
bool native_clipboard_write(native_clipboard_data_t data);

FFI_PLUGIN_EXPORT
bool native_clipboard_write_text(const char* text);

FFI_PLUGIN_EXPORT
bool native_clipboard_write_html(const char* html);

FFI_PLUGIN_EXPORT
bool native_clipboard_write_image(native_image_t image);

FFI_PLUGIN_EXPORT
bool native_clipboard_write_file_paths(native_string_list_t file_paths);

FFI_PLUGIN_EXPORT
bool native_clipboard_clear(void);

FFI_PLUGIN_EXPORT
bool native_clipboard_is_monitoring(void);

/// Registers @p callback for every ClipboardEvent this Clipboard emits.
/// @return the listener id, or NATIVE_INVALID_LISTENER_ID on failure.
FFI_PLUGIN_EXPORT
native_listener_id_t native_clipboard_add_listener(native_clipboard_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data);

/// Registers an asynchronous callback. Its event, borrowed handles and user_data
/// remain valid until event_delivery_complete is called, including after removal.
/// Every delivered payload must be acknowledged. Check is_active before invoking a queued callback.
FFI_PLUGIN_EXPORT
native_listener_id_t native_clipboard_add_listener_async(native_clipboard_event_callback_t_async callback, void* user_data, native_release_user_data_t release_user_data);

/// Unregisters a listener. Returns false if unknown.
FFI_PLUGIN_EXPORT
bool native_clipboard_remove_listener(native_listener_id_t listener_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nativeapi {
class ClipboardEvent;
}  // namespace nativeapi

/// Fills @p out from @p event. Returns false when the event is not one
/// of the concrete types the C ABI knows about.
bool to_c_clipboard_event(const nativeapi::ClipboardEvent& event, native_clipboard_event_t* out);
/// Releases everything to_c_clipboard_event() allocated.
void free_c_clipboard_event(native_clipboard_event_t* value);

#endif

#ifdef __cplusplus
#include "../clipboard.h"
#include "string_utils_c.h"
#include "user_data.h"

// Conversion helpers between these C types and their C++ originals.

#include <cstdlib>
#include <stdexcept>
#include "../foundation/handle_table.h"
inline native_clipboard_data_t to_c_clipboard_data(const nativeapi::ClipboardData& value);
inline nativeapi::ClipboardData to_cpp_clipboard_data(const native_clipboard_data_t& value);

inline native_clipboard_data_t to_c_clipboard_data(const nativeapi::ClipboardData& value) {
  native_clipboard_data_t result = {};
  result.text = value.text ? (value.text->empty() ? new char[1]{0} : to_c_str(*value.text)) : nullptr;
  result.html = value.html ? (value.html->empty() ? new char[1]{0} : to_c_str(*value.html)) : nullptr;
  result.image = nativeapi::HandleTable::GetInstance().Insert(value.image);
  result.file_paths = to_c_string_list(value.file_paths);
  return result;
}

inline nativeapi::ClipboardData to_cpp_clipboard_data(const native_clipboard_data_t& value) {
  nativeapi::ClipboardData result = {};
  if (value.text) result.text = std::string(value.text);
  if (value.html) result.html = std::string(value.html);
  result.image = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Image>(value.image);
  if (value.image && !result.image) throw std::invalid_argument("Invalid struct handle");
  if (value.file_paths.count < 0 || (value.file_paths.count && !value.file_paths.items)) throw std::invalid_argument("Invalid string list");
  for (long i = 0; i < value.file_paths.count; ++i) {
    if (!value.file_paths.items[i]) throw std::invalid_argument("Null string item");
    result.file_paths.emplace_back(value.file_paths.items[i]);
  }
  return result;
}

#endif  // __cplusplus
