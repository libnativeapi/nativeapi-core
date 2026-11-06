// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "screen_recorder_c.h"

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
#include "../capture_source.h"
#include "capture_source_c.h"
#include "../screen_capturer.h"
#include "screen_capturer_c.h"
#include "../screen_recorder.h"

void native_screen_recording_options_free(native_screen_recording_options_t* value) {
  if (!value) {
    return;
  }
  native_screen_capture_options_free(&value->capture);
}

native_screen_recorder_t native_screen_recorder_create(void) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::ScreenRecorder>());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_recorder_create");
    return 0;
  }
}

bool native_screen_recorder_is_supported(void) {
  try {
    return nativeapi::ScreenRecorder::IsSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_recorder_is_supported");
    return false;
  }
}

bool native_screen_recorder_start(native_screen_recorder_t screen_recorder, native_capture_source_t source, const char* file_path, native_screen_recording_options_t options) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return false;
  }
  try {
    auto source_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::CaptureSource>(source);
    auto options_cpp = to_cpp_screen_recording_options(options);
    return self->Start(source_cpp, std::string(file_path ? file_path : ""), options_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_recorder_start");
    return false;
  }
}

bool native_screen_recorder_stop(native_screen_recorder_t screen_recorder) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return false;
  }
  try {
    return self->Stop();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_recorder_stop");
    return false;
  }
}

bool native_screen_recorder_cancel(native_screen_recorder_t screen_recorder) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return false;
  }
  try {
    return self->Cancel();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_recorder_cancel");
    return false;
  }
}

native_screen_recording_state_t native_screen_recorder_get_state(native_screen_recorder_t screen_recorder) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return (native_screen_recording_state_t)NATIVE_SCREEN_RECORDING_STATE_IDLE;
  }
  try {
    return to_c_screen_recording_state(self->GetState());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_recorder_get_state");
    return (native_screen_recording_state_t)NATIVE_SCREEN_RECORDING_STATE_IDLE;
  }
}

bool native_screen_recorder_is_active(native_screen_recorder_t screen_recorder) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return false;
  }
  try {
    return self->IsActive();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_recorder_is_active");
    return false;
  }
}

double native_screen_recorder_get_duration(native_screen_recorder_t screen_recorder) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return 0;
  }
  try {
    return self->GetDuration();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_recorder_get_duration");
    return 0;
  }
}

char* native_screen_recorder_get_file_path(native_screen_recorder_t screen_recorder) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return nullptr;
  }
  try {
    return to_c_str(self->GetFilePath());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_screen_recorder_get_file_path");
    return nullptr;
  }
}

void native_screen_recorder_free(native_screen_recorder_t screen_recorder) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(screen_recorder);
}

native_listener_id_t native_screen_recorder_add_listener(native_screen_recorder_t screen_recorder, native_screen_recording_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) {
    return 0;
  }
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return 0;
  }
  try {
    return static_cast<native_listener_id_t>(self->AddListener<nativeapi::ScreenRecordingEvent>(
        [callback, holder](const nativeapi::ScreenRecordingEvent& event) {
          if (holder->revoked()) return;
          native_screen_recording_event_t c_event = {};
          if (!to_c_screen_recording_event(event, &c_event)) {
            return;
          }
          callback(&c_event, holder->get());
          free_c_screen_recording_event(&c_event);
        }));
  } catch (...) {
    return 0;
  }
}

native_listener_id_t native_screen_recorder_add_listener_async(native_screen_recorder_t screen_recorder, native_screen_recording_event_callback_t_async callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) return 0;
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return 0;
  }
  try {
    auto registration = std::make_shared<nativeapi::capi::EventDeliveryRegistration>(holder);
    return static_cast<native_listener_id_t>(nativeapi::detail::EventListenerDispatch::AddListener<nativeapi::ScreenRecordingEvent>(*self,
        [callback, registration](const nativeapi::ScreenRecordingEvent& event) {
          if (registration->context->holder->revoked()) return;
          std::shared_ptr<nativeapi::EventRequest> request;
          auto vote = request && request->IsCancelable() ? request->Defer() : nullptr;
          native_event_delivery_t delivery = 0;
          try {
            auto payload = std::shared_ptr<native_screen_recording_event_t>(new native_screen_recording_event_t{}, [](native_screen_recording_event_t* value) { free_c_screen_recording_event(value); delete value; });
            if (!to_c_screen_recording_event(event, payload.get())) { if (request) request->Cancel(); return; }
            auto* event_pointer = payload.get();
            auto lease = std::make_shared<nativeapi::capi::EventDelivery>(registration->context, std::move(payload), std::move(vote));
            delivery = nativeapi::HandleTable::GetInstance().Insert(lease);
            callback(event_pointer, delivery, registration->context->holder->get());
          } catch (...) {
            if (request) request->Cancel();
            if (delivery) native_event_delivery_complete(delivery, false);
          }
        }, registration->context->active));
  } catch (...) {
    return 0;
  }
}

bool native_screen_recorder_remove_listener(native_screen_recorder_t screen_recorder, native_listener_id_t listener_id) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ScreenRecorder>(screen_recorder);
  if (!self) {
    return false;
  }
  try {
    return self->RemoveListener(static_cast<size_t>(listener_id));
  } catch (...) {
    return false;
  }
}

bool to_c_screen_recording_event(const nativeapi::ScreenRecordingEvent& event, native_screen_recording_event_t* out) {
  if (!out) {
    return false;
  }
  *out = native_screen_recording_event_t{};
  if (const auto* typed = dynamic_cast<const nativeapi::ScreenRecordingStartedEvent*>(&event)) {
    out->type = NATIVE_SCREEN_RECORDING_EVENT_TYPE_STARTED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::ScreenRecordingCompletedEvent*>(&event)) {
    out->type = NATIVE_SCREEN_RECORDING_EVENT_TYPE_COMPLETED;
    out->data.completed.file_path = to_c_str(typed->GetFilePath());
    out->data.completed.duration = typed->GetDuration();
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::ScreenRecordingCancelledEvent*>(&event)) {
    out->type = NATIVE_SCREEN_RECORDING_EVENT_TYPE_CANCELLED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::ScreenRecordingFailedEvent*>(&event)) {
    out->type = NATIVE_SCREEN_RECORDING_EVENT_TYPE_FAILED;
    out->data.failed.message = to_c_str(typed->GetMessage());
    return true;
  }
  return false;
}

void free_c_screen_recording_event(native_screen_recording_event_t* value) {
  if (!value) {
    return;
  }
  if (value->type == NATIVE_SCREEN_RECORDING_EVENT_TYPE_COMPLETED) {
    free_c_str(value->data.completed.file_path);
    value->data.completed.file_path = nullptr;
  }
  if (value->type == NATIVE_SCREEN_RECORDING_EVENT_TYPE_FAILED) {
    free_c_str(value->data.failed.message);
    value->data.failed.message = nullptr;
  }
}

