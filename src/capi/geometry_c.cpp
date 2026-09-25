// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "geometry_c.h"

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
#include "../foundation/geometry.h"

native_edge_insets_t native_edge_insets_all(double value) {
  try {
    const auto cpp_result = nativeapi::EdgeInsets::All(value);
    return to_c_edge_insets(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_edge_insets_all");
    native_edge_insets_t result = {};
    return result;
  }
}

native_edge_insets_t native_edge_insets_symmetric(double vertical, double horizontal) {
  try {
    const auto cpp_result = nativeapi::EdgeInsets::Symmetric(vertical, horizontal);
    return to_c_edge_insets(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_edge_insets_symmetric");
    native_edge_insets_t result = {};
    return result;
  }
}

