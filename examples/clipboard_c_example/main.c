#include <stdio.h>
#include "../../src/capi/application_c.h"
#include "../../src/capi/clipboard_c.h"

static void Read(bool success,
                 const native_clipboard_data_t* data,
                 native_event_delivery_t delivery,
                 void* user_data) {
  (void)user_data;
  if (success)
    printf("text=%d html=%d image=%d files=%ld\n", data->text != NULL, data->html != NULL,
           data->image != 0, data->file_paths.count);
  // Copy data or native_handle_retain(image) before acknowledging if keeping it.
  native_event_delivery_complete(delivery, success);
  native_application_quit(success ? 0 : 1);
}
int main(void) {
  if (!native_clipboard_is_supported())
    return 1;
  native_clipboard_read(Read, NULL, NULL);
  return native_application_run();
}
