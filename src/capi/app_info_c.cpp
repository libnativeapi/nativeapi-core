// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "app_info_c.h"

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
#include "../app_info.h"

char* native_app_info_get_name(void) {
  try {
    return to_c_str(nativeapi::AppInfo::GetInstance().GetName());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_app_info_get_name");
    return nullptr;
  }
}

char* native_app_info_get_identifier(void) {
  try {
    return to_c_str(nativeapi::AppInfo::GetInstance().GetIdentifier());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_app_info_get_identifier");
    return nullptr;
  }
}

char* native_app_info_get_version(void) {
  try {
    return to_c_str(nativeapi::AppInfo::GetInstance().GetVersion());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_app_info_get_version");
    return nullptr;
  }
}

char* native_app_info_get_build_number(void) {
  try {
    return to_c_str(nativeapi::AppInfo::GetInstance().GetBuildNumber());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_app_info_get_build_number");
    return nullptr;
  }
}

