// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "clipboard_c.h"

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
#include "../image.h"
#include "image_c.h"
#include "../clipboard.h"

void native_clipboard_data_free(native_clipboard_data_t* value) {
  if (!value) {
    return;
  }
  free_c_str(value->text);
  value->text = nullptr;
  free_c_str(value->html);
  value->html = nullptr;
  nativeapi::HandleTable::GetInstance().Release(value->image); value->image = 0;
  native_string_list_free(&value->file_paths);
}

bool native_clipboard_is_supported(void) {
  try {
    return nativeapi::Clipboard::GetInstance().IsSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_is_supported");
    return false;
  }
}

bool native_clipboard_is_change_monitoring_supported(void) {
  try {
    return nativeapi::Clipboard::GetInstance().IsChangeMonitoringSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_is_change_monitoring_supported");
    return false;
  }
}

void native_clipboard_read(native_clipboard_read_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data) {
  auto callback_holder = nativeapi::capi::UserData::Make(callback_user_data, callback_release_user_data);
  try {
    std::function<void(bool, nativeapi::ClipboardData)> callback_cpp;
    if (callback) {
      callback_cpp = [callback, holder = callback_holder](bool arg0, nativeapi::ClipboardData arg1) {
        if (holder->revoked()) return;
        struct Payload {
          bool arg0{};
          native_clipboard_data_t arg1{};
        };
        auto payload = std::shared_ptr<Payload>(new Payload{}, [](Payload* value) {
          native_clipboard_data_free(&value->arg1);
          delete value;
        });
        payload->arg0 = arg0;
        payload->arg1 = to_c_clipboard_data(arg1);
        auto context = std::make_shared<nativeapi::capi::EventDeliveryContext>(holder);
        auto lease = std::make_shared<nativeapi::capi::EventDelivery>(context, payload, nullptr);
        auto delivery = nativeapi::HandleTable::GetInstance().Insert(lease);
        callback(payload->arg0, &payload->arg1, delivery, holder->get());
      };
    }
    nativeapi::Clipboard::GetInstance().Read(callback_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_read");
    return;
  }
}

void native_clipboard_read_text(native_clipboard_read_text_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data) {
  auto callback_holder = nativeapi::capi::UserData::Make(callback_user_data, callback_release_user_data);
  try {
    std::function<void(bool, std::optional<std::string>)> callback_cpp;
    if (callback) {
      callback_cpp = [callback, holder = callback_holder](bool arg0, std::optional<std::string> arg1) {
        if (holder->revoked()) return;
        struct Payload {
          bool arg0{};
          char* arg1{};
        };
        auto payload = std::shared_ptr<Payload>(new Payload{}, [](Payload* value) {
          free_c_str(value->arg1);
          value->arg1 = nullptr;
          delete value;
        });
        payload->arg0 = arg0;
        payload->arg1 = arg1 ? (arg1->empty() ? new char[1]{0} : to_c_str(*arg1)) : nullptr;
        auto context = std::make_shared<nativeapi::capi::EventDeliveryContext>(holder);
        auto lease = std::make_shared<nativeapi::capi::EventDelivery>(context, payload, nullptr);
        auto delivery = nativeapi::HandleTable::GetInstance().Insert(lease);
        callback(payload->arg0, payload->arg1, delivery, holder->get());
      };
    }
    nativeapi::Clipboard::GetInstance().ReadText(callback_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_read_text");
    return;
  }
}

void native_clipboard_read_html(native_clipboard_read_html_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data) {
  auto callback_holder = nativeapi::capi::UserData::Make(callback_user_data, callback_release_user_data);
  try {
    std::function<void(bool, std::optional<std::string>)> callback_cpp;
    if (callback) {
      callback_cpp = [callback, holder = callback_holder](bool arg0, std::optional<std::string> arg1) {
        if (holder->revoked()) return;
        struct Payload {
          bool arg0{};
          char* arg1{};
        };
        auto payload = std::shared_ptr<Payload>(new Payload{}, [](Payload* value) {
          free_c_str(value->arg1);
          value->arg1 = nullptr;
          delete value;
        });
        payload->arg0 = arg0;
        payload->arg1 = arg1 ? (arg1->empty() ? new char[1]{0} : to_c_str(*arg1)) : nullptr;
        auto context = std::make_shared<nativeapi::capi::EventDeliveryContext>(holder);
        auto lease = std::make_shared<nativeapi::capi::EventDelivery>(context, payload, nullptr);
        auto delivery = nativeapi::HandleTable::GetInstance().Insert(lease);
        callback(payload->arg0, payload->arg1, delivery, holder->get());
      };
    }
    nativeapi::Clipboard::GetInstance().ReadHtml(callback_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_read_html");
    return;
  }
}

void native_clipboard_read_image(native_clipboard_read_image_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data) {
  auto callback_holder = nativeapi::capi::UserData::Make(callback_user_data, callback_release_user_data);
  try {
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
    nativeapi::Clipboard::GetInstance().ReadImage(callback_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_read_image");
    return;
  }
}

void native_clipboard_read_file_paths(native_clipboard_read_file_paths_callback_t callback, void* callback_user_data, native_release_user_data_t callback_release_user_data) {
  auto callback_holder = nativeapi::capi::UserData::Make(callback_user_data, callback_release_user_data);
  try {
    std::function<void(bool, std::vector<std::string>)> callback_cpp;
    if (callback) {
      callback_cpp = [callback, holder = callback_holder](bool arg0, std::vector<std::string> arg1) {
        if (holder->revoked()) return;
        struct Payload {
          bool arg0{};
          native_string_list_t arg1{};
        };
        auto payload = std::shared_ptr<Payload>(new Payload{}, [](Payload* value) {
          native_string_list_free(&value->arg1);
          delete value;
        });
        payload->arg0 = arg0;
        payload->arg1 = to_c_string_list(arg1);
        auto context = std::make_shared<nativeapi::capi::EventDeliveryContext>(holder);
        auto lease = std::make_shared<nativeapi::capi::EventDelivery>(context, payload, nullptr);
        auto delivery = nativeapi::HandleTable::GetInstance().Insert(lease);
        callback(payload->arg0, &payload->arg1, delivery, holder->get());
      };
    }
    nativeapi::Clipboard::GetInstance().ReadFilePaths(callback_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_read_file_paths");
    return;
  }
}

bool native_clipboard_write(native_clipboard_data_t data) {
  try {
    auto data_cpp = to_cpp_clipboard_data(data);
    return nativeapi::Clipboard::GetInstance().Write(data_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_write");
    return false;
  }
}

bool native_clipboard_write_text(const char* text) {
  try {
    return nativeapi::Clipboard::GetInstance().WriteText(std::string(text ? text : ""));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_write_text");
    return false;
  }
}

bool native_clipboard_write_html(const char* html) {
  try {
    return nativeapi::Clipboard::GetInstance().WriteHtml(std::string(html ? html : ""));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_write_html");
    return false;
  }
}

bool native_clipboard_write_image(native_image_t image) {
  try {
    auto image_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Image>(image);
    if (image && !image_cpp) {
        return false;
    }
    return nativeapi::Clipboard::GetInstance().WriteImage(image_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_write_image");
    return false;
  }
}

bool native_clipboard_write_file_paths(native_string_list_t file_paths) {
  try {
    if (file_paths.count < 0 || (file_paths.count > 0 && !file_paths.items)) {
        return false;
    }
    for (long i = 0; i < file_paths.count; ++i) {
      if (!file_paths.items[i]) {
          return false;
      }
    }
    std::vector<std::string> file_paths_cpp;
    for (long i = 0; i < file_paths.count; ++i) {
      file_paths_cpp.emplace_back(file_paths.items[i] ? file_paths.items[i] : "");
    }
    return nativeapi::Clipboard::GetInstance().WriteFilePaths(file_paths_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_write_file_paths");
    return false;
  }
}

bool native_clipboard_clear(void) {
  try {
    return nativeapi::Clipboard::GetInstance().Clear();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_clear");
    return false;
  }
}

bool native_clipboard_is_monitoring(void) {
  try {
    return nativeapi::Clipboard::GetInstance().IsMonitoring();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_clipboard_is_monitoring");
    return false;
  }
}

native_listener_id_t native_clipboard_add_listener(native_clipboard_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) {
    return 0;
  }
  try {
    return static_cast<native_listener_id_t>(nativeapi::Clipboard::GetInstance().AddListener<nativeapi::ClipboardEvent>(
        [callback, holder](const nativeapi::ClipboardEvent& event) {
          if (holder->revoked()) return;
          native_clipboard_event_t c_event = {};
          if (!to_c_clipboard_event(event, &c_event)) {
            return;
          }
          callback(&c_event, holder->get());
          free_c_clipboard_event(&c_event);
        }));
  } catch (...) {
    return 0;
  }
}

native_listener_id_t native_clipboard_add_listener_async(native_clipboard_event_callback_t_async callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) return 0;
  try {
    auto registration = std::make_shared<nativeapi::capi::EventDeliveryRegistration>(holder);
    return static_cast<native_listener_id_t>(nativeapi::detail::EventListenerDispatch::AddListener<nativeapi::ClipboardEvent>(nativeapi::Clipboard::GetInstance(),
        [callback, registration](const nativeapi::ClipboardEvent& event) {
          if (registration->context->holder->revoked()) return;
          std::shared_ptr<nativeapi::EventRequest> request;
          auto vote = request && request->IsCancelable() ? request->Defer() : nullptr;
          native_event_delivery_t delivery = 0;
          try {
            auto payload = std::shared_ptr<native_clipboard_event_t>(new native_clipboard_event_t{}, [](native_clipboard_event_t* value) { free_c_clipboard_event(value); delete value; });
            if (!to_c_clipboard_event(event, payload.get())) { if (request) request->Cancel(); return; }
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

bool native_clipboard_remove_listener(native_listener_id_t listener_id) {
  try {
    return nativeapi::Clipboard::GetInstance().RemoveListener(static_cast<size_t>(listener_id));
  } catch (...) {
    return false;
  }
}

bool to_c_clipboard_event(const nativeapi::ClipboardEvent& event, native_clipboard_event_t* out) {
  if (!out) {
    return false;
  }
  *out = native_clipboard_event_t{};
  if (const auto* typed = dynamic_cast<const nativeapi::ClipboardChangedEvent*>(&event)) {
    out->type = NATIVE_CLIPBOARD_EVENT_TYPE_CHANGED;
    (void)typed;
    return true;
  }
  return false;
}

void free_c_clipboard_event(native_clipboard_event_t* value) {
  if (!value) {
    return;
  }
}

