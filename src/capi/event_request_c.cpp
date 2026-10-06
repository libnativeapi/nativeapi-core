// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "event_request_c.h"

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
#include "../foundation/event_request.h"

bool native_event_decision_accept(native_event_decision_t event_decision) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::EventDecision>(event_decision);
  if (!self) {
    return false;
  }
  try {
    return self->Accept();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_event_decision_accept");
    return false;
  }
}

bool native_event_decision_cancel(native_event_decision_t event_decision) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::EventDecision>(event_decision);
  if (!self) {
    return false;
  }
  try {
    return self->Cancel();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_event_decision_cancel");
    return false;
  }
}

bool native_event_decision_is_pending(native_event_decision_t event_decision) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::EventDecision>(event_decision);
  if (!self) {
    return false;
  }
  try {
    return self->IsPending();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_event_decision_is_pending");
    return false;
  }
}

void native_event_decision_free(native_event_decision_t event_decision) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(event_decision);
}

bool native_event_request_is_cancelable(native_event_request_t event_request) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::EventRequest>(event_request);
  if (!self) {
    return false;
  }
  try {
    return self->IsCancelable();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_event_request_is_cancelable");
    return false;
  }
}

bool native_event_request_is_cancelled(native_event_request_t event_request) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::EventRequest>(event_request);
  if (!self) {
    return false;
  }
  try {
    return self->IsCancelled();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_event_request_is_cancelled");
    return false;
  }
}

bool native_event_request_is_pending(native_event_request_t event_request) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::EventRequest>(event_request);
  if (!self) {
    return false;
  }
  try {
    return self->IsPending();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_event_request_is_pending");
    return false;
  }
}

bool native_event_request_cancel(native_event_request_t event_request) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::EventRequest>(event_request);
  if (!self) {
    return false;
  }
  try {
    return self->Cancel();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_event_request_cancel");
    return false;
  }
}

native_event_decision_t native_event_request_defer(native_event_request_t event_request) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::EventRequest>(event_request);
  if (!self) {
    return 0;
  }
  try {
    return nativeapi::HandleTable::GetInstance().Insert(self->Defer());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_event_request_defer");
    return 0;
  }
}

void native_event_request_free(native_event_request_t event_request) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(event_request);
}

