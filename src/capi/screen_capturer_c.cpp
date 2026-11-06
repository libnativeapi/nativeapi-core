// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "screen_capturer_c.h"

#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "string_utils_c.h"
#include "user_data.h"
#include "event_delivery.h"
#include "../foundation/handle_table.h"
#include "../foundation/geometry.h"
#include "geometry_c.h"
#include "../image.h"
#include "image_c.h"
#include "../capture_source.h"
#include "capture_source_c.h"
#include "../window.h"
#include "window_c.h"
#include "../screen_capturer.h"

void native_capture_source_selection_options_free(native_capture_source_selection_options_t* value) {
  if (!value) {
    return;
  }
  nativeapi::HandleTable::GetInstance().Release(value->parent_window); value->parent_window = 0;
}

void native_screen_capture_options_free(native_screen_capture_options_t* value) {
  if (!value) {
    return;
  }
  native_rectangle_free(&value->crop);
}

bool native_screen_capturer_is_supported(void) {
  try {
    return nativeapi::ScreenCapturer::IsSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_capturer_is_supported");
    return false;
  }
}

bool native_screen_capturer_is_source_selection_supported(void) {
  try {
    return nativeapi::ScreenCapturer::IsSourceSelectionSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_capturer_is_source_selection_supported");
    return false;
  }
}

bool native_screen_capturer_is_direct_capture_supported(native_capture_source_type_t type) {
  try {
    return nativeapi::ScreenCapturer::IsDirectCaptureSupported(to_cpp_capture_source_type(type));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_capturer_is_direct_capture_supported");
    return false;
  }
}

void native_screen_capturer_select_source(native_capture_source_selection_options_t options, native_screen_capturer_select_source_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data) {
  auto callback_holder = nativeapi::capi::UserData::Make(callback_user_data, callback_release_user_data);
  try {
    auto options_cpp = to_cpp_capture_source_selection_options(options);
    std::function<void(bool, std::shared_ptr<nativeapi::CaptureSource>)> callback_cpp;
    if (callback) {
      callback_cpp = [callback, holder = callback_holder](bool arg0, std::shared_ptr<nativeapi::CaptureSource> arg1) {
        if (holder->revoked()) return;
        struct Payload {
          bool arg0{};
          native_capture_source_t arg1{};
        };
        auto payload = std::shared_ptr<Payload>(new Payload{}, [](Payload* value) {
          nativeapi::HandleTable::GetInstance().Release(value->arg1); value->arg1 = 0;
          delete value;
        });
        payload->arg0 = arg0;
        payload->arg1 = nativeapi::HandleTable::GetInstance().Insert(arg1);
        auto context = std::make_shared<nativeapi::capi::EventDeliveryContext>(holder);
        auto lease = std::make_shared<nativeapi::capi::EventDelivery>(context, payload, nullptr);
        auto delivery = nativeapi::HandleTable::GetInstance().Insert(lease);
        callback(payload->arg0, payload->arg1, delivery, holder->get());
      };
    }
    nativeapi::ScreenCapturer::SelectSource(options_cpp, callback_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_capturer_select_source");
    return;
  }
}

void native_screen_capturer_capture(native_capture_source_t source, native_screen_capture_options_t options, native_screen_capturer_capture_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data) {
  auto callback_holder = nativeapi::capi::UserData::Make(callback_user_data, callback_release_user_data);
  try {
    auto source_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::CaptureSource>(source);
    auto options_cpp = to_cpp_screen_capture_options(options);
    std::function<void(bool, std::shared_ptr<nativeapi::Image>)> callback_cpp;
    if (callback) {
      callback_cpp = [callback, holder = callback_holder](bool arg0, std::shared_ptr<nativeapi::Image> arg1) {
        if (holder->revoked()) return;
        struct Payload {
          bool arg0{};
          native_image_t arg1{};
        };
        auto payload = std::shared_ptr<Payload>(new Payload{}, [](Payload* value) {
          nativeapi::HandleTable::GetInstance().Release(value->arg1); value->arg1 = 0;
          delete value;
        });
        payload->arg0 = arg0;
        payload->arg1 = nativeapi::HandleTable::GetInstance().Insert(arg1);
        auto context = std::make_shared<nativeapi::capi::EventDeliveryContext>(holder);
        auto lease = std::make_shared<nativeapi::capi::EventDelivery>(context, payload, nullptr);
        auto delivery = nativeapi::HandleTable::GetInstance().Insert(lease);
        callback(payload->arg0, payload->arg1, delivery, holder->get());
      };
    }
    nativeapi::ScreenCapturer::Capture(source_cpp, options_cpp, callback_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_capturer_capture");
    return;
  }
}

