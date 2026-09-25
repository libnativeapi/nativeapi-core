// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "device_info_c.h"

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
#include "../device_info.h"

char* native_device_info_get_name(void) {
  try {
    return to_c_str(nativeapi::DeviceInfo::GetInstance().GetName());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_device_info_get_name");
    return nullptr;
  }
}

char* native_device_info_get_model(void) {
  try {
    return to_c_str(nativeapi::DeviceInfo::GetInstance().GetModel());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_device_info_get_model");
    return nullptr;
  }
}

char* native_device_info_get_manufacturer(void) {
  try {
    return to_c_str(nativeapi::DeviceInfo::GetInstance().GetManufacturer());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_device_info_get_manufacturer");
    return nullptr;
  }
}

char* native_device_info_get_os_name(void) {
  try {
    return to_c_str(nativeapi::DeviceInfo::GetInstance().GetOsName());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_device_info_get_os_name");
    return nullptr;
  }
}

char* native_device_info_get_os_version(void) {
  try {
    return to_c_str(nativeapi::DeviceInfo::GetInstance().GetOsVersion());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_device_info_get_os_version");
    return nullptr;
  }
}

char* native_device_info_get_kernel_version(void) {
  try {
    return to_c_str(nativeapi::DeviceInfo::GetInstance().GetKernelVersion());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_device_info_get_kernel_version");
    return nullptr;
  }
}

char* native_device_info_get_architecture(void) {
  try {
    return to_c_str(nativeapi::DeviceInfo::GetInstance().GetArchitecture());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_device_info_get_architecture");
    return nullptr;
  }
}

