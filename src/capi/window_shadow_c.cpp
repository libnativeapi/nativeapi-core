// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "window_shadow_c.h"

#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "string_utils_c.h"
#include "../foundation/handle_table.h"
#include "../foundation/geometry.h"
#include "geometry_c.h"
#include "../foundation/color.h"
#include "color_c.h"
#include "../window_shadow.h"

native_window_shadow_t native_window_shadow_create(void) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::WindowShadow>());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shadow_create");
    return 0;
  }
}

void native_window_shadow_set_color(native_window_shadow_t window_shadow, native_color_t color) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShadow>(window_shadow);
  if (!self) {
    return;
  }
  try {
    auto color_cpp = to_cpp_color(color);
    self->SetColor(color_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shadow_set_color");
    return;
  }
}

native_color_t native_window_shadow_get_color(native_window_shadow_t window_shadow) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShadow>(window_shadow);
  if (!self) {
    native_color_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetColor();
    return to_c_color(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shadow_get_color");
    native_color_t result = {};
    return result;
  }
}

bool native_window_shadow_set_blur_radius(native_window_shadow_t window_shadow, double radius) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShadow>(window_shadow);
  if (!self) {
    return false;
  }
  try {
    return self->SetBlurRadius(radius);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shadow_set_blur_radius");
    return false;
  }
}

double native_window_shadow_get_blur_radius(native_window_shadow_t window_shadow) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShadow>(window_shadow);
  if (!self) {
    return 0;
  }
  try {
    return self->GetBlurRadius();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shadow_get_blur_radius");
    return 0;
  }
}

bool native_window_shadow_set_offset(native_window_shadow_t window_shadow, native_point_t offset) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShadow>(window_shadow);
  if (!self) {
    return false;
  }
  try {
    auto offset_cpp = to_cpp_point(offset);
    return self->SetOffset(offset_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shadow_set_offset");
    return false;
  }
}

native_point_t native_window_shadow_get_offset(native_window_shadow_t window_shadow) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShadow>(window_shadow);
  if (!self) {
    native_point_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetOffset();
    return to_c_point(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shadow_get_offset");
    native_point_t result = {};
    return result;
  }
}

void native_window_shadow_free(native_window_shadow_t window_shadow) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(window_shadow);
}

