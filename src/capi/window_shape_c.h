// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
#include "geometry_c.h"

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque WindowShape handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_WINDOW_SHAPE rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_window_shape_t;

/// Never refers to a live WindowShape.
#define NATIVE_INVALID_WINDOW_SHAPE ((native_window_shape_t)0)

/// Creates a WindowShape instance; release it with native_window_shape_free().
FFI_PLUGIN_EXPORT
native_window_shape_t native_window_shape_create(void);

FFI_PLUGIN_EXPORT
bool native_window_shape_add_point(native_window_shape_t window_shape, native_point_t point);

FFI_PLUGIN_EXPORT
void native_window_shape_clear(native_window_shape_t window_shape);

FFI_PLUGIN_EXPORT
unsigned long native_window_shape_get_point_count(native_window_shape_t window_shape);

FFI_PLUGIN_EXPORT
native_point_t native_window_shape_get_point_at(native_window_shape_t window_shape, unsigned long index);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_window_shape_free(native_window_shape_t window_shape);

#ifdef __cplusplus
}
#endif
