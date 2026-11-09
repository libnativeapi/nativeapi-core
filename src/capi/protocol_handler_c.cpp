// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "protocol_handler_c.h"

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
#include "../protocol_handler.h"

bool native_protocol_handler_is_supported(void) {
  try {
    return nativeapi::ProtocolHandler::GetInstance().IsSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_protocol_handler_is_supported");
    return false;
  }
}

bool native_protocol_handler_register(const char* scheme) {
  try {
    return nativeapi::ProtocolHandler::GetInstance().Register(std::string(scheme ? scheme : ""));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_protocol_handler_register");
    return false;
  }
}

char* native_protocol_handler_get_initial_url(void) {
  try {
    const auto cpp_result = nativeapi::ProtocolHandler::GetInstance().GetInitialUrl();
    return cpp_result ? to_c_str(*cpp_result) : nullptr;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_protocol_handler_get_initial_url");
    return nullptr;
  }
}

native_listener_id_t native_protocol_handler_add_listener(native_protocol_handler_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) {
    return 0;
  }
  try {
    return static_cast<native_listener_id_t>(nativeapi::ProtocolHandler::GetInstance().AddListener<nativeapi::ProtocolHandlerEvent>(
        [callback, holder](const nativeapi::ProtocolHandlerEvent& event) {
          if (holder->revoked()) return;
          native_protocol_handler_event_t c_event = {};
          if (!to_c_protocol_handler_event(event, &c_event)) {
            return;
          }
          callback(&c_event, holder->get());
          free_c_protocol_handler_event(&c_event);
        }));
  } catch (...) {
    return 0;
  }
}

native_listener_id_t native_protocol_handler_add_listener_async(native_protocol_handler_event_callback_t_async callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) return 0;
  try {
    auto registration = std::make_shared<nativeapi::capi::EventDeliveryRegistration>(holder);
    return static_cast<native_listener_id_t>(nativeapi::detail::EventListenerDispatch::AddListener<nativeapi::ProtocolHandlerEvent>(nativeapi::ProtocolHandler::GetInstance(),
        [callback, registration](const nativeapi::ProtocolHandlerEvent& event) {
          if (registration->context->holder->revoked()) return;
          std::shared_ptr<nativeapi::EventRequest> request;
          auto vote = request && request->IsCancelable() ? request->Defer() : nullptr;
          native_event_delivery_t delivery = 0;
          try {
            auto payload = std::shared_ptr<native_protocol_handler_event_t>(new native_protocol_handler_event_t{}, [](native_protocol_handler_event_t* value) { free_c_protocol_handler_event(value); delete value; });
            if (!to_c_protocol_handler_event(event, payload.get())) { if (request) request->Cancel(); return; }
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

bool native_protocol_handler_remove_listener(native_listener_id_t listener_id) {
  try {
    return nativeapi::ProtocolHandler::GetInstance().RemoveListener(static_cast<size_t>(listener_id));
  } catch (...) {
    return false;
  }
}

bool to_c_protocol_handler_event(const nativeapi::ProtocolHandlerEvent& event, native_protocol_handler_event_t* out) {
  if (!out) {
    return false;
  }
  *out = native_protocol_handler_event_t{};
  if (const auto* typed = dynamic_cast<const nativeapi::ProtocolUrlReceivedEvent*>(&event)) {
    out->type = NATIVE_PROTOCOL_HANDLER_EVENT_TYPE_PROTOCOL_URL_RECEIVED;
    out->data.protocol_url_received.url = to_c_str(typed->GetUrl());
    return true;
  }
  return false;
}

void free_c_protocol_handler_event(native_protocol_handler_event_t* value) {
  if (!value) {
    return;
  }
  if (value->type == NATIVE_PROTOCOL_HANDLER_EVENT_TYPE_PROTOCOL_URL_RECEIVED) {
    free_c_str(value->data.protocol_url_received.url);
    value->data.protocol_url_received.url = nullptr;
  }
}

