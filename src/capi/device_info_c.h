// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define FFI_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_device_info_get_name(void);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_device_info_get_model(void);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_device_info_get_manufacturer(void);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_device_info_get_os_name(void);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_device_info_get_os_version(void);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_device_info_get_kernel_version(void);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_device_info_get_architecture(void);

#ifdef __cplusplus
}
#endif
