// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
typedef uint64_t native_window_t;

#include "dialog_c.h"
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
  NATIVE_MESSAGE_DIALOG_RESULT_NONE = 0,
  NATIVE_MESSAGE_DIALOG_RESULT_PRIMARY = 1,
  NATIVE_MESSAGE_DIALOG_RESULT_SECONDARY = 2,
  NATIVE_MESSAGE_DIALOG_RESULT_CLOSE = 3,
} native_message_dialog_result_t;

/// Opaque MessageDialog handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_MESSAGE_DIALOG rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_message_dialog_t;

/// Never refers to a live MessageDialog.
#define NATIVE_INVALID_MESSAGE_DIALOG ((native_message_dialog_t)0)

/// Creates a MessageDialog instance; release it with native_message_dialog_free().
FFI_PLUGIN_EXPORT
native_message_dialog_t native_message_dialog_create(const char* title, const char* message);

FFI_PLUGIN_EXPORT
bool native_message_dialog_is_extended_supported(void);

FFI_PLUGIN_EXPORT
bool native_message_dialog_set_buttons(native_message_dialog_t message_dialog, const char* primary, const char* secondary, const char* close);

FFI_PLUGIN_EXPORT
bool native_message_dialog_set_default_button(native_message_dialog_t message_dialog, native_message_dialog_result_t button);

FFI_PLUGIN_EXPORT
bool native_message_dialog_set_parent_window(native_message_dialog_t message_dialog, native_window_t window);

FFI_PLUGIN_EXPORT
native_message_dialog_result_t native_message_dialog_get_result(native_message_dialog_t message_dialog);

FFI_PLUGIN_EXPORT
bool native_message_dialog_is_open(native_message_dialog_t message_dialog);

FFI_PLUGIN_EXPORT
bool native_message_dialog_set_input_enabled(native_message_dialog_t message_dialog, bool enabled);

FFI_PLUGIN_EXPORT
bool native_message_dialog_set_input_text(native_message_dialog_t message_dialog, const char* text);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_message_dialog_get_input_text(native_message_dialog_t message_dialog);

FFI_PLUGIN_EXPORT
bool native_message_dialog_set_checkbox(native_message_dialog_t message_dialog, const char* label, bool checked);

FFI_PLUGIN_EXPORT
bool native_message_dialog_is_checkbox_checked(native_message_dialog_t message_dialog);

FFI_PLUGIN_EXPORT
bool native_message_dialog_set_progress(native_message_dialog_t message_dialog, double value);

FFI_PLUGIN_EXPORT
void native_message_dialog_set_title(native_message_dialog_t message_dialog, const char* title);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_message_dialog_get_title(native_message_dialog_t message_dialog);

FFI_PLUGIN_EXPORT
void native_message_dialog_set_message(native_message_dialog_t message_dialog, const char* message);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_message_dialog_get_message(native_message_dialog_t message_dialog);

FFI_PLUGIN_EXPORT
native_dialog_modality_t native_message_dialog_get_modality(native_message_dialog_t message_dialog);

FFI_PLUGIN_EXPORT
void native_message_dialog_set_modality(native_message_dialog_t message_dialog, native_dialog_modality_t modality);

FFI_PLUGIN_EXPORT
bool native_message_dialog_open(native_message_dialog_t message_dialog);

FFI_PLUGIN_EXPORT
bool native_message_dialog_close(native_message_dialog_t message_dialog);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_message_dialog_free(native_message_dialog_t message_dialog);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "../message_dialog.h"
#include "string_utils_c.h"
#include "user_data.h"

// Conversion helpers between these C types and their C++ originals.

inline native_message_dialog_result_t to_c_message_dialog_result(nativeapi::MessageDialogResult value);
inline nativeapi::MessageDialogResult to_cpp_message_dialog_result(native_message_dialog_result_t value);

inline native_message_dialog_result_t to_c_message_dialog_result(nativeapi::MessageDialogResult value) {
  switch (value) {
    case nativeapi::MessageDialogResult::None:
      return NATIVE_MESSAGE_DIALOG_RESULT_NONE;
    case nativeapi::MessageDialogResult::Primary:
      return NATIVE_MESSAGE_DIALOG_RESULT_PRIMARY;
    case nativeapi::MessageDialogResult::Secondary:
      return NATIVE_MESSAGE_DIALOG_RESULT_SECONDARY;
    case nativeapi::MessageDialogResult::Close:
      return NATIVE_MESSAGE_DIALOG_RESULT_CLOSE;
    default:
      return NATIVE_MESSAGE_DIALOG_RESULT_NONE;
  }
}

inline nativeapi::MessageDialogResult to_cpp_message_dialog_result(native_message_dialog_result_t value) {
  switch (value) {
    case NATIVE_MESSAGE_DIALOG_RESULT_NONE:
      return nativeapi::MessageDialogResult::None;
    case NATIVE_MESSAGE_DIALOG_RESULT_PRIMARY:
      return nativeapi::MessageDialogResult::Primary;
    case NATIVE_MESSAGE_DIALOG_RESULT_SECONDARY:
      return nativeapi::MessageDialogResult::Secondary;
    case NATIVE_MESSAGE_DIALOG_RESULT_CLOSE:
      return nativeapi::MessageDialogResult::Close;
    default:
      return nativeapi::MessageDialogResult::None;
  }
}

#endif  // __cplusplus
