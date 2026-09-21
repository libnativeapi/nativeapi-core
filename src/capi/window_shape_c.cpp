// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "window_shape_c.h"

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
#include "../window_shape.h"

native_window_shape_t native_window_shape_create(void) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::WindowShape>());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shape_create");
    return 0;
  }
}

bool native_window_shape_add_point(native_window_shape_t window_shape, native_point_t point) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShape>(window_shape);
  if (!self) {
    return false;
  }
  try {
    auto point_cpp = to_cpp_point(point);
    return self->AddPoint(point_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shape_add_point");
    return false;
  }
}

void native_window_shape_clear(native_window_shape_t window_shape) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShape>(window_shape);
  if (!self) {
    return;
  }
  try {
    self->Clear();
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shape_clear");
    return;
  }
}

unsigned long native_window_shape_get_point_count(native_window_shape_t window_shape) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShape>(window_shape);
  if (!self) {
    return 0;
  }
  try {
    return self->GetPointCount();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shape_get_point_count");
    return 0;
  }
}

native_point_t native_window_shape_get_point_at(native_window_shape_t window_shape, unsigned long index) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::WindowShape>(window_shape);
  if (!self) {
    native_point_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetPointAt(index);
    return to_c_point(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_window_shape_get_point_at");
    native_point_t result = {};
    return result;
  }
}

void native_window_shape_free(native_window_shape_t window_shape) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(window_shape);
}

