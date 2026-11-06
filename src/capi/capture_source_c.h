// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
typedef uint64_t native_display_t;
typedef uint64_t native_window_t;

#include "display_c.h"
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
  NATIVE_CAPTURE_SOURCE_TYPE_NONE = 0,
  NATIVE_CAPTURE_SOURCE_TYPE_DISPLAY = 1,
  NATIVE_CAPTURE_SOURCE_TYPE_WINDOW = 2,
  NATIVE_CAPTURE_SOURCE_TYPE_UNKNOWN = 3,
} native_capture_source_type_t;

/// Opaque CaptureSource handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_CAPTURE_SOURCE rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_capture_source_t;

/// Never refers to a live CaptureSource.
#define NATIVE_INVALID_CAPTURE_SOURCE ((native_capture_source_t)0)

/// Caller owns the returned handle; release it with native_capture_source_free().
FFI_PLUGIN_EXPORT
native_capture_source_t native_capture_source_from_display(native_display_t display);

/// Caller owns the returned handle; release it with native_capture_source_free().
FFI_PLUGIN_EXPORT
native_capture_source_t native_capture_source_from_window(native_window_t window);

FFI_PLUGIN_EXPORT
native_capture_source_type_t native_capture_source_get_type(native_capture_source_t capture_source);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_capture_source_get_name(native_capture_source_t capture_source);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_capture_source_free(native_capture_source_t capture_source);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "../capture_source.h"
#include "string_utils_c.h"
#include "user_data.h"

// Conversion helpers between these C types and their C++ originals.

inline native_capture_source_type_t to_c_capture_source_type(nativeapi::CaptureSourceType value);
inline nativeapi::CaptureSourceType to_cpp_capture_source_type(native_capture_source_type_t value);

inline native_capture_source_type_t to_c_capture_source_type(nativeapi::CaptureSourceType value) {
  switch (value) {
    case nativeapi::CaptureSourceType::None:
      return NATIVE_CAPTURE_SOURCE_TYPE_NONE;
    case nativeapi::CaptureSourceType::Display:
      return NATIVE_CAPTURE_SOURCE_TYPE_DISPLAY;
    case nativeapi::CaptureSourceType::Window:
      return NATIVE_CAPTURE_SOURCE_TYPE_WINDOW;
    case nativeapi::CaptureSourceType::Unknown:
      return NATIVE_CAPTURE_SOURCE_TYPE_UNKNOWN;
    default:
      return NATIVE_CAPTURE_SOURCE_TYPE_NONE;
  }
}

inline nativeapi::CaptureSourceType to_cpp_capture_source_type(native_capture_source_type_t value) {
  switch (value) {
    case NATIVE_CAPTURE_SOURCE_TYPE_NONE:
      return nativeapi::CaptureSourceType::None;
    case NATIVE_CAPTURE_SOURCE_TYPE_DISPLAY:
      return nativeapi::CaptureSourceType::Display;
    case NATIVE_CAPTURE_SOURCE_TYPE_WINDOW:
      return nativeapi::CaptureSourceType::Window;
    case NATIVE_CAPTURE_SOURCE_TYPE_UNKNOWN:
      return nativeapi::CaptureSourceType::Unknown;
    default:
      return static_cast<nativeapi::CaptureSourceType>(value);
  }
}

#endif  // __cplusplus
