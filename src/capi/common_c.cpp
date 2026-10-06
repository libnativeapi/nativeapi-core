// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "common_c.h"
#include "event_delivery.h"
#include "../foundation/handle_table.h"

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
