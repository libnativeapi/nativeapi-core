// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common_c.h"
typedef uint64_t native_image_t;
typedef uint64_t native_window_t;

#include "color_c.h"
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

typedef unsigned int native_view_id_t;

typedef enum {
  NATIVE_VIEW_LAYOUT_ABSOLUTE = 0,
  NATIVE_VIEW_LAYOUT_ROW = 1,
  NATIVE_VIEW_LAYOUT_COLUMN = 2,
} native_view_layout_t;

typedef enum {
  NATIVE_VIEW_ALIGNMENT_STRETCH = 0,
  NATIVE_VIEW_ALIGNMENT_START = 1,
  NATIVE_VIEW_ALIGNMENT_CENTER = 2,
  NATIVE_VIEW_ALIGNMENT_END = 3,
} native_view_alignment_t;

typedef enum {
  NATIVE_TEXT_ALIGNMENT_START = 0,
  NATIVE_TEXT_ALIGNMENT_CENTER = 1,
  NATIVE_TEXT_ALIGNMENT_END = 2,
} native_text_alignment_t;

/// Opaque View handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_VIEW rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
typedef uint64_t native_view_t;

/// Never refers to a live View.
#define NATIVE_INVALID_VIEW ((native_view_t)0)

/// Owning list of View handles.
typedef struct {
  native_view_t* views;
  long count;
} native_view_list_t;

/// Opaque Label handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_LABEL rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
///
/// A Label is a View: this handle is accepted wherever a native_view_t is,
/// including its listener registration.
typedef uint64_t native_label_t;

/// Never refers to a live Label.
#define NATIVE_INVALID_LABEL ((native_label_t)0)

/// Opaque Button handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_BUTTON rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
///
/// A Button is a View: this handle is accepted wherever a native_view_t is,
/// including its listener registration.
typedef uint64_t native_button_t;

/// Never refers to a live Button.
#define NATIVE_INVALID_BUTTON ((native_button_t)0)

/// Opaque TextField handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_TEXT_FIELD rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
///
/// A TextField is a View: this handle is accepted wherever a native_view_t is,
/// including its listener registration.
typedef uint64_t native_text_field_t;

/// Never refers to a live TextField.
#define NATIVE_INVALID_TEXT_FIELD ((native_text_field_t)0)

/// Opaque ImageView handle.
///
/// A generational index into the library's handle table, NOT a pointer:
/// never dereference it, and compare it against NATIVE_INVALID_IMAGE_VIEW rather than NULL.
/// Releasing a handle invalidates it; later calls fail safely instead of
/// touching freed memory.
///
/// A ImageView is a View: this handle is accepted wherever a native_view_t is,
/// including its listener registration.
typedef uint64_t native_image_view_t;

/// Never refers to a live ImageView.
#define NATIVE_INVALID_IMAGE_VIEW ((native_image_view_t)0)

/// Which concrete ViewEvent arrived.
typedef enum {
  NATIVE_VIEW_EVENT_TYPE_FOCUSED = 0,
  NATIVE_VIEW_EVENT_TYPE_BLURRED = 1,
  NATIVE_VIEW_EVENT_TYPE_BUTTON_CLICKED = 2,
  NATIVE_VIEW_EVENT_TYPE_TEXT_FIELD_CHANGED = 3,
  NATIVE_VIEW_EVENT_TYPE_TEXT_FIELD_SUBMITTED = 4,
} native_view_event_type_t;

/// One ViewEvent, tagged by its concrete type.
///
/// Valid only for the duration of the callback: anything it points at
/// is released as soon as the callback returns. Copy what you need.
typedef struct {
  native_view_event_type_t type;
  native_view_id_t view_id;
  union {
    struct {
      char* text;
    } text_field_changed;
  } data;
} native_view_event_t;

typedef void (*native_view_event_callback_t)(const native_view_event_t* event, void* user_data);

/// Creates a View instance; release it with native_view_free().
FFI_PLUGIN_EXPORT
native_view_t native_view_create(void);

/// Creates a View instance; release it with native_view_free().
FFI_PLUGIN_EXPORT
native_view_t native_view_create_with_native_view(void* native_view);

FFI_PLUGIN_EXPORT
bool native_view_is_supported(void);

FFI_PLUGIN_EXPORT
native_view_id_t native_view_get_id(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_add_subview(native_view_t view, native_view_t subview);

FFI_PLUGIN_EXPORT
void native_view_insert_subview(native_view_t view, unsigned long index, native_view_t subview);

FFI_PLUGIN_EXPORT
bool native_view_remove_subview(native_view_t view, native_view_t subview);

FFI_PLUGIN_EXPORT
bool native_view_remove_subview_at(native_view_t view, unsigned long index);

FFI_PLUGIN_EXPORT
void native_view_clear_subviews(native_view_t view);

FFI_PLUGIN_EXPORT
unsigned long native_view_get_subview_count(native_view_t view);

/// Caller owns the returned handle; release it with native_view_free().
FFI_PLUGIN_EXPORT
native_view_t native_view_get_subview_at(native_view_t view, unsigned long index);

FFI_PLUGIN_EXPORT
native_view_list_t native_view_get_subviews(native_view_t view);

/// Caller owns the returned handle; release it with native_view_free().
FFI_PLUGIN_EXPORT
native_view_t native_view_get_parent(native_view_t view);

/// Caller owns the returned handle; release it with native_window_free().
FFI_PLUGIN_EXPORT
native_window_t native_view_get_window(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_frame(native_view_t view, native_rectangle_t frame);

FFI_PLUGIN_EXPORT
native_rectangle_t native_view_get_frame(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_preferred_size(native_view_t view, native_size_t size);

FFI_PLUGIN_EXPORT
native_size_t native_view_get_preferred_size(native_view_t view);

FFI_PLUGIN_EXPORT
native_size_t native_view_get_intrinsic_size(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_flex(native_view_t view, double flex);

FFI_PLUGIN_EXPORT
double native_view_get_flex(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_alignment(native_view_t view, native_view_alignment_t alignment);

FFI_PLUGIN_EXPORT
native_view_alignment_t native_view_get_alignment(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_layout(native_view_t view, native_view_layout_t layout);

FFI_PLUGIN_EXPORT
native_view_layout_t native_view_get_layout(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_spacing(native_view_t view, double spacing);

FFI_PLUGIN_EXPORT
double native_view_get_spacing(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_padding(native_view_t view, native_edge_insets_t padding);

FFI_PLUGIN_EXPORT
native_edge_insets_t native_view_get_padding(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_visible(native_view_t view, bool is_visible);

FFI_PLUGIN_EXPORT
bool native_view_is_visible(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_enabled(native_view_t view, bool is_enabled);

FFI_PLUGIN_EXPORT
bool native_view_is_enabled(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_background_color(native_view_t view, native_color_t color);

FFI_PLUGIN_EXPORT
native_color_t native_view_get_background_color(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_set_tooltip(native_view_t view, const char* tooltip);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_view_get_tooltip(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_focus(native_view_t view);

FFI_PLUGIN_EXPORT
void native_view_blur(native_view_t view);

FFI_PLUGIN_EXPORT
bool native_view_is_focused(native_view_t view);

/// Platform-specific native object (NSScreen*, HMONITOR, ...).
FFI_PLUGIN_EXPORT
void* native_view_get_native_object(native_view_t view);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_view_free(native_view_t view);

/// Frees the array and releases every handle it contains.
FFI_PLUGIN_EXPORT
void native_view_list_free(native_view_list_t* list);

/// Frees only the array; the caller takes over the handles.
FFI_PLUGIN_EXPORT
void native_view_list_release(native_view_list_t* list);

/// Registers @p callback for every ViewEvent this View emits.
/// @return the listener id, or NATIVE_INVALID_LISTENER_ID on failure.
FFI_PLUGIN_EXPORT
native_listener_id_t native_view_add_listener(native_view_t view, native_view_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data);

/// Unregisters a listener. Returns false if unknown.
FFI_PLUGIN_EXPORT
bool native_view_remove_listener(native_view_t view, native_listener_id_t listener_id);

/// Creates a Label instance; release it with native_label_free().
FFI_PLUGIN_EXPORT
native_label_t native_label_create(const char* text);

FFI_PLUGIN_EXPORT
void native_label_set_text(native_label_t label, const char* text);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_label_get_text(native_label_t label);

FFI_PLUGIN_EXPORT
void native_label_set_text_color(native_label_t label, native_color_t color);

FFI_PLUGIN_EXPORT
native_color_t native_label_get_text_color(native_label_t label);

FFI_PLUGIN_EXPORT
void native_label_set_font_size(native_label_t label, double size);

FFI_PLUGIN_EXPORT
double native_label_get_font_size(native_label_t label);

FFI_PLUGIN_EXPORT
void native_label_set_text_alignment(native_label_t label, native_text_alignment_t alignment);

FFI_PLUGIN_EXPORT
native_text_alignment_t native_label_get_text_alignment(native_label_t label);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_label_free(native_label_t label);

/// Creates a Button instance; release it with native_button_free().
FFI_PLUGIN_EXPORT
native_button_t native_button_create(const char* text);

FFI_PLUGIN_EXPORT
void native_button_set_text(native_button_t button, const char* text);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_button_get_text(native_button_t button);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_button_free(native_button_t button);

/// Creates a TextField instance; release it with native_text_field_free().
FFI_PLUGIN_EXPORT
native_text_field_t native_text_field_create(const char* text);

FFI_PLUGIN_EXPORT
void native_text_field_set_text(native_text_field_t text_field, const char* text);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_text_field_get_text(native_text_field_t text_field);

FFI_PLUGIN_EXPORT
void native_text_field_set_text_color(native_text_field_t text_field, native_color_t color);

FFI_PLUGIN_EXPORT
native_color_t native_text_field_get_text_color(native_text_field_t text_field);

FFI_PLUGIN_EXPORT
void native_text_field_set_font_size(native_text_field_t text_field, double size);

FFI_PLUGIN_EXPORT
double native_text_field_get_font_size(native_text_field_t text_field);

FFI_PLUGIN_EXPORT
void native_text_field_set_text_alignment(native_text_field_t text_field, native_text_alignment_t alignment);

FFI_PLUGIN_EXPORT
native_text_alignment_t native_text_field_get_text_alignment(native_text_field_t text_field);

FFI_PLUGIN_EXPORT
void native_text_field_set_placeholder(native_text_field_t text_field, const char* placeholder);

/// Caller owns the returned string; free it with free_c_str().
FFI_PLUGIN_EXPORT
char* native_text_field_get_placeholder(native_text_field_t text_field);

FFI_PLUGIN_EXPORT
void native_text_field_set_editable(native_text_field_t text_field, bool is_editable);

FFI_PLUGIN_EXPORT
bool native_text_field_is_editable(native_text_field_t text_field);

FFI_PLUGIN_EXPORT
void native_text_field_set_secure(native_text_field_t text_field, bool is_secure);

FFI_PLUGIN_EXPORT
bool native_text_field_is_secure(native_text_field_t text_field);

FFI_PLUGIN_EXPORT
void native_text_field_set_multiline(native_text_field_t text_field, bool is_multiline);

FFI_PLUGIN_EXPORT
bool native_text_field_is_multiline(native_text_field_t text_field);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_text_field_free(native_text_field_t text_field);

/// Creates a ImageView instance; release it with native_image_view_free().
FFI_PLUGIN_EXPORT
native_image_view_t native_image_view_create(void);

FFI_PLUGIN_EXPORT
void native_image_view_set_image(native_image_view_t image_view, native_image_t image);

/// Caller owns the returned handle; release it with native_image_free().
FFI_PLUGIN_EXPORT
native_image_t native_image_view_get_image(native_image_view_t image_view);

/// Releases the caller's reference. Safe to call with an invalid or
/// already-released handle.
FFI_PLUGIN_EXPORT
void native_image_view_free(native_image_view_t image_view);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace nativeapi {
class ViewEvent;
}  // namespace nativeapi

/// Fills @p out from @p event. Returns false when the event is not one
/// of the concrete types the C ABI knows about.
bool to_c_view_event(const nativeapi::ViewEvent& event, native_view_event_t* out);
/// Releases everything to_c_view_event() allocated.
void free_c_view_event(native_view_event_t* value);

#endif

#ifdef __cplusplus
#include "../view.h"
#include "string_utils_c.h"
#include "user_data.h"

// Conversion helpers between these C types and their C++ originals.

inline native_view_layout_t to_c_view_layout(nativeapi::ViewLayout value);
inline nativeapi::ViewLayout to_cpp_view_layout(native_view_layout_t value);
inline native_view_alignment_t to_c_view_alignment(nativeapi::ViewAlignment value);
inline nativeapi::ViewAlignment to_cpp_view_alignment(native_view_alignment_t value);
inline native_text_alignment_t to_c_text_alignment(nativeapi::TextAlignment value);
inline nativeapi::TextAlignment to_cpp_text_alignment(native_text_alignment_t value);

inline native_view_layout_t to_c_view_layout(nativeapi::ViewLayout value) {
  switch (value) {
    case nativeapi::ViewLayout::Absolute:
      return NATIVE_VIEW_LAYOUT_ABSOLUTE;
    case nativeapi::ViewLayout::Row:
      return NATIVE_VIEW_LAYOUT_ROW;
    case nativeapi::ViewLayout::Column:
      return NATIVE_VIEW_LAYOUT_COLUMN;
    default:
      return NATIVE_VIEW_LAYOUT_ABSOLUTE;
  }
}

inline nativeapi::ViewLayout to_cpp_view_layout(native_view_layout_t value) {
  switch (value) {
    case NATIVE_VIEW_LAYOUT_ABSOLUTE:
      return nativeapi::ViewLayout::Absolute;
    case NATIVE_VIEW_LAYOUT_ROW:
      return nativeapi::ViewLayout::Row;
    case NATIVE_VIEW_LAYOUT_COLUMN:
      return nativeapi::ViewLayout::Column;
    default:
      return nativeapi::ViewLayout::Absolute;
  }
}

inline native_view_alignment_t to_c_view_alignment(nativeapi::ViewAlignment value) {
  switch (value) {
    case nativeapi::ViewAlignment::Stretch:
      return NATIVE_VIEW_ALIGNMENT_STRETCH;
    case nativeapi::ViewAlignment::Start:
      return NATIVE_VIEW_ALIGNMENT_START;
    case nativeapi::ViewAlignment::Center:
      return NATIVE_VIEW_ALIGNMENT_CENTER;
    case nativeapi::ViewAlignment::End:
      return NATIVE_VIEW_ALIGNMENT_END;
    default:
      return NATIVE_VIEW_ALIGNMENT_STRETCH;
  }
}

inline nativeapi::ViewAlignment to_cpp_view_alignment(native_view_alignment_t value) {
  switch (value) {
    case NATIVE_VIEW_ALIGNMENT_STRETCH:
      return nativeapi::ViewAlignment::Stretch;
    case NATIVE_VIEW_ALIGNMENT_START:
      return nativeapi::ViewAlignment::Start;
    case NATIVE_VIEW_ALIGNMENT_CENTER:
      return nativeapi::ViewAlignment::Center;
    case NATIVE_VIEW_ALIGNMENT_END:
      return nativeapi::ViewAlignment::End;
    default:
      return nativeapi::ViewAlignment::Stretch;
  }
}

inline native_text_alignment_t to_c_text_alignment(nativeapi::TextAlignment value) {
  switch (value) {
    case nativeapi::TextAlignment::Start:
      return NATIVE_TEXT_ALIGNMENT_START;
    case nativeapi::TextAlignment::Center:
      return NATIVE_TEXT_ALIGNMENT_CENTER;
    case nativeapi::TextAlignment::End:
      return NATIVE_TEXT_ALIGNMENT_END;
    default:
      return NATIVE_TEXT_ALIGNMENT_START;
  }
}

inline nativeapi::TextAlignment to_cpp_text_alignment(native_text_alignment_t value) {
  switch (value) {
    case NATIVE_TEXT_ALIGNMENT_START:
      return nativeapi::TextAlignment::Start;
    case NATIVE_TEXT_ALIGNMENT_CENTER:
      return nativeapi::TextAlignment::Center;
    case NATIVE_TEXT_ALIGNMENT_END:
      return nativeapi::TextAlignment::End;
    default:
      return nativeapi::TextAlignment::Start;
  }
}

#endif  // __cplusplus
