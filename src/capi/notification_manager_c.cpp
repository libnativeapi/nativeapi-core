// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "notification_manager_c.h"

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
#include "../notification_manager.h"

bool native_notification_manager_is_supported(void) {
  try {
    return nativeapi::NotificationManager::GetInstance().IsSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_notification_manager_is_supported");
    return false;
  }
}

bool native_notification_manager_initialize(void) {
  try {
    return nativeapi::NotificationManager::GetInstance().Initialize();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_notification_manager_initialize");
    return false;
  }
}

void native_notification_manager_shutdown(void) {
  try {
    nativeapi::NotificationManager::GetInstance().Shutdown();
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_notification_manager_shutdown");
    return;
  }
}

bool native_notification_manager_show(const char* title, const char* message, const char* tag, const char* button_label) {
  try {
    return nativeapi::NotificationManager::GetInstance().Show(std::string(title ? title : ""), std::string(message ? message : ""), std::string(tag ? tag : ""), std::string(button_label ? button_label : ""));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_notification_manager_show");
    return false;
  }
}

bool native_notification_manager_remove(const char* tag) {
  try {
    return nativeapi::NotificationManager::GetInstance().Remove(std::string(tag ? tag : ""));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_notification_manager_remove");
    return false;
  }
}

char* native_notification_manager_get_last_error(void) {
  try {
    return to_c_str(nativeapi::NotificationManager::GetInstance().GetLastError());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_notification_manager_get_last_error");
    return nullptr;
  }
}

native_listener_id_t native_notification_manager_add_listener(native_notification_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) {
    return 0;
  }
  try {
    return static_cast<native_listener_id_t>(nativeapi::NotificationManager::GetInstance().AddListener<nativeapi::NotificationEvent>(
        [callback, holder](const nativeapi::NotificationEvent& event) {
          native_notification_event_t c_event = {};
          if (!to_c_notification_event(event, &c_event)) {
            return;
          }
          callback(&c_event, holder->get());
          free_c_notification_event(&c_event);
        }));
  } catch (...) {
    return 0;
  }
}

bool native_notification_manager_remove_listener(native_listener_id_t listener_id) {
  try {
    return nativeapi::NotificationManager::GetInstance().RemoveListener(static_cast<size_t>(listener_id));
  } catch (...) {
    return false;
  }
}

bool to_c_notification_event(const nativeapi::NotificationEvent& event, native_notification_event_t* out) {
  if (!out) {
    return false;
  }
  *out = native_notification_event_t{};
  if (const auto* typed = dynamic_cast<const nativeapi::NotificationActivatedEvent*>(&event)) {
    out->type = NATIVE_NOTIFICATION_EVENT_TYPE_ACTIVATED;
    out->data.activated.argument = to_c_str(typed->GetArgument());
    return true;
  }
  return false;
}

void free_c_notification_event(native_notification_event_t* value) {
  if (!value) {
    return;
  }
  if (value->type == NATIVE_NOTIFICATION_EVENT_TYPE_ACTIVATED) {
    free_c_str(value->data.activated.argument);
    value->data.activated.argument = nullptr;
  }
}

