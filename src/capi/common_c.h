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

/// Owns an asynchronous event payload until acknowledged exactly once.
typedef uint64_t native_event_delivery_t;
/// Whether the originating listener is still registered. False for stale handles.
FFI_PLUGIN_EXPORT bool native_event_delivery_is_active(native_event_delivery_t delivery);
/// Releases the payload and its borrowed handles; accept resolves the implicit request vote.
/// Pass false on failure. Returns false for duplicate, stale or type-confused handles.
FFI_PLUGIN_EXPORT bool native_event_delivery_complete(native_event_delivery_t delivery, bool accept);

/// Releases a handle of any type for a garbage collector's native finalizer,
/// such as Dart's NativeFinalizer, which runs it on an arbitrary thread and
/// also when the runtime shuts down (a Flutter hot restart, for one). `handle`
/// is the handle's value cast to a pointer; the release itself runs on the
/// main thread. Stale or invalid handles are ignored.
FFI_PLUGIN_EXPORT void native_handle_finalize(void* handle);

/// Tells the core a binding's runtime is gone for this user_data, from a
/// native finalizer like the one above: from now on it calls neither the
/// callback that travels with it nor its release. Safe from any thread.
/// A binding must not pass a revoked value again.
FFI_PLUGIN_EXPORT void native_user_data_revoke(void* user_data);

#ifdef __cplusplus
}
#endif
