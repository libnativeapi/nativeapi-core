// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
#include "string_utils_c.h"
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
  NATIVE_FILE_DIALOG_MODE_OPEN_FILE = 0,
  NATIVE_FILE_DIALOG_MODE_OPEN_FILES = 1,
  NATIVE_FILE_DIALOG_MODE_SAVE_FILE = 2,
  NATIVE_FILE_DIALOG_MODE_SELECT_FOLDER = 3,
} native_file_dialog_mode_t;

typedef enum {
  NATIVE_FILE_DIALOG_RESULT_NONE = 0,
  NATIVE_FILE_DIALOG_RESULT_ACCEPTED = 1,
  NATIVE_FILE_DIALOG_RESULT_CANCELLED = 2,
  NATIVE_FILE_DIALOG_RESULT_FAILED = 3,
} native_file_dialog_result_t;

/// Opaque FileDialog handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_FILE_DIALOG rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_file_dialog_t;

/// Never refers to a live FileDialog.
#define NATIVE_INVALID_FILE_DIALOG ((native_file_dialog_t)0)

/// Creates a FileDialog instance; release it with native_file_dialog_free().
FFI_PLUGIN_EXPORT
native_file_dialog_t native_file_dialog_create(native_file_dialog_mode_t mode);

FFI_PLUGIN_EXPORT
bool native_file_dialog_is_supported(void);

FFI_PLUGIN_EXPORT
bool native_file_dialog_set_parent_window(native_file_dialog_t file_dialog, native_window_t window);

FFI_PLUGIN_EXPORT
bool native_file_dialog_set_file_types(native_file_dialog_t file_dialog, native_string_list_t extensions);

FFI_PLUGIN_EXPORT
bool native_file_dialog_set_suggested_file_name(native_file_dialog_t file_dialog, const char* name);

FFI_PLUGIN_EXPORT
native_dialog_modality_t native_file_dialog_get_modality(native_file_dialog_t file_dialog);

FFI_PLUGIN_EXPORT
void native_file_dialog_set_modality(native_file_dialog_t file_dialog, native_dialog_modality_t modality);

FFI_PLUGIN_EXPORT
bool native_file_dialog_open(native_file_dialog_t file_dialog);

FFI_PLUGIN_EXPORT
bool native_file_dialog_close(native_file_dialog_t file_dialog);

FFI_PLUGIN_EXPORT
native_file_dialog_result_t native_file_dialog_get_result(native_file_dialog_t file_dialog);

FFI_PLUGIN_EXPORT
native_string_list_t native_file_dialog_get_paths(native_file_dialog_t file_dialog);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_file_dialog_get_last_error(native_file_dialog_t file_dialog);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_file_dialog_free(native_file_dialog_t file_dialog);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "../file_dialog.h"
#include "string_utils_c.h"
#include "user_data.h"

// Conversion helpers between these C types and their C++ originals.

inline native_file_dialog_mode_t to_c_file_dialog_mode(nativeapi::FileDialogMode value);
inline nativeapi::FileDialogMode to_cpp_file_dialog_mode(native_file_dialog_mode_t value);
inline native_file_dialog_result_t to_c_file_dialog_result(nativeapi::FileDialogResult value);
inline nativeapi::FileDialogResult to_cpp_file_dialog_result(native_file_dialog_result_t value);

inline native_file_dialog_mode_t to_c_file_dialog_mode(nativeapi::FileDialogMode value) {
  switch (value) {
    case nativeapi::FileDialogMode::OpenFile:
      return NATIVE_FILE_DIALOG_MODE_OPEN_FILE;
    case nativeapi::FileDialogMode::OpenFiles:
      return NATIVE_FILE_DIALOG_MODE_OPEN_FILES;
    case nativeapi::FileDialogMode::SaveFile:
      return NATIVE_FILE_DIALOG_MODE_SAVE_FILE;
    case nativeapi::FileDialogMode::SelectFolder:
      return NATIVE_FILE_DIALOG_MODE_SELECT_FOLDER;
    default:
      return NATIVE_FILE_DIALOG_MODE_OPEN_FILE;
  }
}

inline nativeapi::FileDialogMode to_cpp_file_dialog_mode(native_file_dialog_mode_t value) {
  switch (value) {
    case NATIVE_FILE_DIALOG_MODE_OPEN_FILE:
      return nativeapi::FileDialogMode::OpenFile;
    case NATIVE_FILE_DIALOG_MODE_OPEN_FILES:
      return nativeapi::FileDialogMode::OpenFiles;
    case NATIVE_FILE_DIALOG_MODE_SAVE_FILE:
      return nativeapi::FileDialogMode::SaveFile;
    case NATIVE_FILE_DIALOG_MODE_SELECT_FOLDER:
      return nativeapi::FileDialogMode::SelectFolder;
    default:
      return nativeapi::FileDialogMode::OpenFile;
  }
}

inline native_file_dialog_result_t to_c_file_dialog_result(nativeapi::FileDialogResult value) {
  switch (value) {
    case nativeapi::FileDialogResult::None:
      return NATIVE_FILE_DIALOG_RESULT_NONE;
    case nativeapi::FileDialogResult::Accepted:
      return NATIVE_FILE_DIALOG_RESULT_ACCEPTED;
    case nativeapi::FileDialogResult::Cancelled:
      return NATIVE_FILE_DIALOG_RESULT_CANCELLED;
    case nativeapi::FileDialogResult::Failed:
      return NATIVE_FILE_DIALOG_RESULT_FAILED;
    default:
      return NATIVE_FILE_DIALOG_RESULT_NONE;
  }
}

inline nativeapi::FileDialogResult to_cpp_file_dialog_result(native_file_dialog_result_t value) {
  switch (value) {
    case NATIVE_FILE_DIALOG_RESULT_NONE:
      return nativeapi::FileDialogResult::None;
    case NATIVE_FILE_DIALOG_RESULT_ACCEPTED:
      return nativeapi::FileDialogResult::Accepted;
    case NATIVE_FILE_DIALOG_RESULT_CANCELLED:
      return nativeapi::FileDialogResult::Cancelled;
    case NATIVE_FILE_DIALOG_RESULT_FAILED:
      return nativeapi::FileDialogResult::Failed;
    default:
      return nativeapi::FileDialogResult::None;
  }
}

#endif  // __cplusplus
