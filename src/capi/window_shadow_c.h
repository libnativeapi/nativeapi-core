// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
#include "color_c.h"
#include "geometry_c.h"

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque WindowShadow handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_WINDOW_SHADOW rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_window_shadow_t;

/// Never refers to a live WindowShadow.
#define NATIVE_INVALID_WINDOW_SHADOW ((native_window_shadow_t)0)

/// Creates a WindowShadow instance; release it with native_window_shadow_free().
FFI_PLUGIN_EXPORT
native_window_shadow_t native_window_shadow_create(void);

FFI_PLUGIN_EXPORT
void native_window_shadow_set_color(native_window_shadow_t window_shadow, native_color_t color);

FFI_PLUGIN_EXPORT
native_color_t native_window_shadow_get_color(native_window_shadow_t window_shadow);

FFI_PLUGIN_EXPORT
bool native_window_shadow_set_blur_radius(native_window_shadow_t window_shadow, double radius);

FFI_PLUGIN_EXPORT
double native_window_shadow_get_blur_radius(native_window_shadow_t window_shadow);

FFI_PLUGIN_EXPORT
bool native_window_shadow_set_offset(native_window_shadow_t window_shadow, native_point_t offset);

FFI_PLUGIN_EXPORT
native_point_t native_window_shadow_get_offset(native_window_shadow_t window_shadow);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_window_shadow_free(native_window_shadow_t window_shadow);

#ifdef __cplusplus
}
#endif
