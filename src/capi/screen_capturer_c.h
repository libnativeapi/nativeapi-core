// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
typedef uint64_t native_capture_source_t;
typedef uint64_t native_image_t;
typedef uint64_t native_window_t;

#include "capture_source_c.h"
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

/// Arguments are borrowed until event_delivery_complete(delivery, ...), which is required exactly once.
typedef void (*native_screen_capturer_select_source_callback_t)(bool arg0, native_capture_source_t arg1, native_event_delivery_t delivery, void* user_data);

/// Arguments are borrowed until event_delivery_complete(delivery, ...), which is required exactly once.
typedef void (*native_screen_capturer_capture_callback_t)(bool arg0, native_image_t arg1, native_event_delivery_t delivery, void* user_data);

typedef struct {
  bool allow_displays;
  bool allow_windows;
  native_window_t parent_window;
} native_capture_source_selection_options_t;

typedef struct {
  bool include_cursor;
  bool crop_enabled;
  native_rectangle_t crop;
} native_screen_capture_options_t;

/// Frees everything the struct owns.
FFI_PLUGIN_EXPORT
void native_capture_source_selection_options_free(native_capture_source_selection_options_t* value);

/// Frees everything the struct owns.
FFI_PLUGIN_EXPORT
void native_screen_capture_options_free(native_screen_capture_options_t* value);

FFI_PLUGIN_EXPORT
bool native_screen_capturer_is_supported(void);

FFI_PLUGIN_EXPORT
bool native_screen_capturer_is_source_selection_supported(void);

FFI_PLUGIN_EXPORT
bool native_screen_capturer_is_direct_capture_supported(native_capture_source_type_t type);

FFI_PLUGIN_EXPORT
void native_screen_capturer_select_source(native_capture_source_selection_options_t options, native_screen_capturer_select_source_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data);

FFI_PLUGIN_EXPORT
void native_screen_capturer_capture(native_capture_source_t source, native_screen_capture_options_t options, native_screen_capturer_capture_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "../screen_capturer.h"
#include "string_utils_c.h"
#include "user_data.h"

// Conversion helpers between these C types and their C++ originals.

#include <cstdlib>
#include <stdexcept>
#include "../foundation/handle_table.h"
inline native_capture_source_selection_options_t to_c_capture_source_selection_options(const nativeapi::CaptureSourceSelectionOptions& value);
inline nativeapi::CaptureSourceSelectionOptions to_cpp_capture_source_selection_options(const native_capture_source_selection_options_t& value);
inline native_screen_capture_options_t to_c_screen_capture_options(const nativeapi::ScreenCaptureOptions& value);
inline nativeapi::ScreenCaptureOptions to_cpp_screen_capture_options(const native_screen_capture_options_t& value);

inline native_capture_source_selection_options_t to_c_capture_source_selection_options(const nativeapi::CaptureSourceSelectionOptions& value) {
  native_capture_source_selection_options_t result = {};
  result.allow_displays = value.allow_displays;
  result.allow_windows = value.allow_windows;
  result.parent_window = nativeapi::HandleTable::GetInstance().Insert(value.parent_window);
  return result;
}

inline nativeapi::CaptureSourceSelectionOptions to_cpp_capture_source_selection_options(const native_capture_source_selection_options_t& value) {
  nativeapi::CaptureSourceSelectionOptions result = {};
  result.allow_displays = value.allow_displays;
  result.allow_windows = value.allow_windows;
  result.parent_window = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Window>(value.parent_window);
  if (value.parent_window && !result.parent_window) throw std::invalid_argument("Invalid struct handle");
  return result;
}

inline native_screen_capture_options_t to_c_screen_capture_options(const nativeapi::ScreenCaptureOptions& value) {
  native_screen_capture_options_t result = {};
  result.include_cursor = value.include_cursor;
  result.crop_enabled = value.crop_enabled;
  result.crop = to_c_rectangle(value.crop);
  return result;
}

inline nativeapi::ScreenCaptureOptions to_cpp_screen_capture_options(const native_screen_capture_options_t& value) {
  nativeapi::ScreenCaptureOptions result = {};
  result.include_cursor = value.include_cursor;
  result.crop_enabled = value.crop_enabled;
  result.crop = to_cpp_rectangle(value.crop);
  return result;
}

#endif  // __cplusplus
