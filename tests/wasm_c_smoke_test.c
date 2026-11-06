#include <string.h>
#include "nativeapi.h"

static void read_text_callback(bool success,
                               const char* text,
                               native_event_delivery_t delivery,
                               void* user_data) {
  int* called = user_data;
  *called = !success && text == NULL && native_event_delivery_complete(delivery, false);
}

int wasm_c_smoke_test(void) {
  char* architecture = native_device_info_get_architecture();
  int ok = architecture && strcmp(architecture, sizeof(void*) == 8 ? "wasm64" : "wasm32") == 0;
  free_c_str(architecture);

  native_preferences_t preferences = native_preferences_create_with_scope("wasm-test");
  ok = ok && preferences != 0 && !native_preferences_set(preferences, "key", "value");
  char* value = native_preferences_get(preferences, "key", "fallback");
  ok = ok && value && strcmp(value, "fallback") == 0;
  free_c_str(value);
  native_preferences_free(preferences);
  int called = 0;
  native_clipboard_read_text(read_text_callback, &called, NULL);
  return ok && called && !native_clipboard_is_supported() && !native_tray_manager_is_supported();
}
