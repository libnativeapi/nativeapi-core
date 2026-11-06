// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "capture_source_c.h"

#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "string_utils_c.h"
#include "user_data.h"
#include "../foundation/handle_table.h"
#include "../display.h"
#include "display_c.h"
#include "../window.h"
#include "window_c.h"
#include "../capture_source.h"

native_capture_source_t native_capture_source_from_display(native_display_t display) {
  try {
    auto display_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Display>(display);
    return nativeapi::HandleTable::GetInstance().Insert(nativeapi::CaptureSource::FromDisplay(display_cpp));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_capture_source_from_display");
    return 0;
  }
}

native_capture_source_t native_capture_source_from_window(native_window_t window) {
  try {
    auto window_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Window>(window);
    return nativeapi::HandleTable::GetInstance().Insert(nativeapi::CaptureSource::FromWindow(window_cpp));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_capture_source_from_window");
    return 0;
  }
}

native_capture_source_type_t native_capture_source_get_type(native_capture_source_t capture_source) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::CaptureSource>(capture_source);
  if (!self) {
    return (native_capture_source_type_t)NATIVE_CAPTURE_SOURCE_TYPE_NONE;
  }
  try {
    return to_c_capture_source_type(self->GetType());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_capture_source_get_type");
    return (native_capture_source_type_t)NATIVE_CAPTURE_SOURCE_TYPE_NONE;
  }
}

char* native_capture_source_get_name(native_capture_source_t capture_source) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::CaptureSource>(capture_source);
  if (!self) {
    return nullptr;
  }
  try {
    return to_c_str(self->GetName());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_capture_source_get_name");
    return nullptr;
  }
}

void native_capture_source_free(native_capture_source_t capture_source) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(capture_source);
}

