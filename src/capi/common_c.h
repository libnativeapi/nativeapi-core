// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Identifies one registered event listener.
typedef uint64_t native_listener_id_t;

/// Returned by add_listener when registration failed.
#define NATIVE_INVALID_LISTENER_ID ((native_listener_id_t)0)

/// Takes back the `user_data` passed with a callback.
///
/// Every function taking a callback also takes one of these (may be NULL).
/// The core calls it exactly once per call — including when the call fails
/// or the callback is NULL — after the last time it can call that callback:
/// when a listener is removed, a callback replaced, a registration ended, or
/// its owner destroyed. It runs on the main thread, never inside the call
/// that let the callback go.
typedef void (*native_release_user_data_t)(void* user_data);

#ifdef __cplusplus
}
#endif
