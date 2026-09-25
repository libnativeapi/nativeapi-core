// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "message_dialog_c.h"

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
#include "../dialog.h"
#include "dialog_c.h"
#include "../window.h"
#include "window_c.h"
#include "../message_dialog.h"

native_message_dialog_t native_message_dialog_create(const char* title, const char* message) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::MessageDialog>(std::string(title ? title : ""), std::string(message ? message : "")));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_create");
    return 0;
  }
}

bool native_message_dialog_is_extended_supported(void) {
  try {
    return nativeapi::MessageDialog::IsExtendedSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_is_extended_supported");
    return false;
  }
}

bool native_message_dialog_set_buttons(native_message_dialog_t message_dialog, const char* primary, const char* secondary, const char* close) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->SetButtons(std::string(primary ? primary : ""), std::string(secondary ? secondary : ""), std::string(close ? close : ""));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_buttons");
    return false;
  }
}

bool native_message_dialog_set_default_button(native_message_dialog_t message_dialog, native_message_dialog_result_t button) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->SetDefaultButton(to_cpp_message_dialog_result(button));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_default_button");
    return false;
  }
}

bool native_message_dialog_set_parent_window(native_message_dialog_t message_dialog, native_window_t window) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    auto window_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Window>(window);
    return self->SetParentWindow(window_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_parent_window");
    return false;
  }
}

native_message_dialog_result_t native_message_dialog_get_result(native_message_dialog_t message_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return (native_message_dialog_result_t)NATIVE_MESSAGE_DIALOG_RESULT_NONE;
  }
  try {
    return to_c_message_dialog_result(self->GetResult());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_get_result");
    return (native_message_dialog_result_t)NATIVE_MESSAGE_DIALOG_RESULT_NONE;
  }
}

bool native_message_dialog_is_open(native_message_dialog_t message_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->IsOpen();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_is_open");
    return false;
  }
}

bool native_message_dialog_set_input_enabled(native_message_dialog_t message_dialog, bool enabled) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->SetInputEnabled(enabled);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_input_enabled");
    return false;
  }
}

bool native_message_dialog_set_input_text(native_message_dialog_t message_dialog, const char* text) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->SetInputText(std::string(text ? text : ""));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_input_text");
    return false;
  }
}

char* native_message_dialog_get_input_text(native_message_dialog_t message_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return nullptr;
  }
  try {
    return to_c_str(self->GetInputText());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_get_input_text");
    return nullptr;
  }
}

bool native_message_dialog_set_checkbox(native_message_dialog_t message_dialog, const char* label, bool checked) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->SetCheckbox(std::string(label ? label : ""), checked);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_checkbox");
    return false;
  }
}

bool native_message_dialog_is_checkbox_checked(native_message_dialog_t message_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->IsCheckboxChecked();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_is_checkbox_checked");
    return false;
  }
}

bool native_message_dialog_set_progress(native_message_dialog_t message_dialog, double value) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->SetProgress(value);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_progress");
    return false;
  }
}

void native_message_dialog_set_title(native_message_dialog_t message_dialog, const char* title) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return;
  }
  try {
    self->SetTitle(std::string(title ? title : ""));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_title");
    return;
  }
}

char* native_message_dialog_get_title(native_message_dialog_t message_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return nullptr;
  }
  try {
    return to_c_str(self->GetTitle());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_get_title");
    return nullptr;
  }
}

void native_message_dialog_set_message(native_message_dialog_t message_dialog, const char* message) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return;
  }
  try {
    self->SetMessage(std::string(message ? message : ""));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_message");
    return;
  }
}

char* native_message_dialog_get_message(native_message_dialog_t message_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return nullptr;
  }
  try {
    return to_c_str(self->GetMessage());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_get_message");
    return nullptr;
  }
}

native_dialog_modality_t native_message_dialog_get_modality(native_message_dialog_t message_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return (native_dialog_modality_t)0;
  }
  try {
    return to_c_dialog_modality(self->GetModality());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_get_modality");
    return (native_dialog_modality_t)0;
  }
}

void native_message_dialog_set_modality(native_message_dialog_t message_dialog, native_dialog_modality_t modality) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return;
  }
  try {
    self->SetModality(to_cpp_dialog_modality(modality));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_set_modality");
    return;
  }
}

bool native_message_dialog_open(native_message_dialog_t message_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->Open();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_open");
    return false;
  }
}

bool native_message_dialog_close(native_message_dialog_t message_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::MessageDialog>(message_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->Close();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_message_dialog_close");
    return false;
  }
}

void native_message_dialog_free(native_message_dialog_t message_dialog) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(message_dialog);
}

