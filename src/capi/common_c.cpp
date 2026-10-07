// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "common_c.h"
#include "event_delivery.h"
#include "../foundation/dispatcher.h"
#include "../foundation/handle_table.h"
#include "user_data.h"

bool native_event_delivery_is_active(native_event_delivery_t delivery) {
  auto value = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::capi::EventDelivery>(delivery);
  return value && value->IsActive();
}

bool native_event_delivery_complete(native_event_delivery_t delivery, bool accept) {
  auto& table = nativeapi::HandleTable::GetInstance();
  auto value = table.Resolve<nativeapi::capi::EventDelivery>(delivery);
  if (!value || !value->Complete(accept)) return false;
  table.Release(delivery);
  return true;
}

void native_handle_finalize(void* handle) {
  const auto value = static_cast<nativeapi::HandleValue>(
      reinterpret_cast<uintptr_t>(handle));
  // Always posted, even from the main thread: destroying a platform object
  // off it is unsafe, and a runtime shutting down revokes its callbacks in
  // its other finalizers first, which the destruction may otherwise call.
  auto release = [value] { nativeapi::HandleTable::GetInstance().Release(value); };
  if (!nativeapi::RunOnMainThread(release) && !nativeapi::IsMainThreadDispatchSupported()) release();
}

void native_user_data_revoke(void* user_data) {
  nativeapi::capi::RevokedUserData::Revoke(user_data);
}
