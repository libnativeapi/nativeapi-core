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

/// Opaque EventDecision handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_EVENT_DECISION rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_event_decision_t;

/// Never refers to a live EventDecision.
#define NATIVE_INVALID_EVENT_DECISION ((native_event_decision_t)0)

/// Opaque EventRequest handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_EVENT_REQUEST rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_event_request_t;

/// Never refers to a live EventRequest.
#define NATIVE_INVALID_EVENT_REQUEST ((native_event_request_t)0)

FFI_PLUGIN_EXPORT
bool native_event_decision_accept(native_event_decision_t event_decision);

FFI_PLUGIN_EXPORT
bool native_event_decision_cancel(native_event_decision_t event_decision);

FFI_PLUGIN_EXPORT
bool native_event_decision_is_pending(native_event_decision_t event_decision);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_event_decision_free(native_event_decision_t event_decision);

FFI_PLUGIN_EXPORT
bool native_event_request_is_cancelable(native_event_request_t event_request);

FFI_PLUGIN_EXPORT
bool native_event_request_is_cancelled(native_event_request_t event_request);

FFI_PLUGIN_EXPORT
bool native_event_request_is_pending(native_event_request_t event_request);

FFI_PLUGIN_EXPORT
bool native_event_request_cancel(native_event_request_t event_request);

/// Caller owns the returned handle; release it with native_event_decision_free().
FFI_PLUGIN_EXPORT
native_event_decision_t native_event_request_defer(native_event_request_t event_request);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_event_request_free(native_event_request_t event_request);

#ifdef __cplusplus
}
#endif
