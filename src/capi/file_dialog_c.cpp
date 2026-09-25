// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "file_dialog_c.h"

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
#include "../file_dialog.h"

native_file_dialog_t native_file_dialog_create(native_file_dialog_mode_t mode) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::FileDialog>(to_cpp_file_dialog_mode(mode)));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_create");
    return 0;
  }
}

bool native_file_dialog_is_supported(void) {
  try {
    return nativeapi::FileDialog::IsSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_is_supported");
    return false;
  }
}

bool native_file_dialog_set_parent_window(native_file_dialog_t file_dialog, native_window_t window) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    return false;
  }
  try {
    auto window_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Window>(window);
    return self->SetParentWindow(window_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_set_parent_window");
    return false;
  }
}

bool native_file_dialog_set_file_types(native_file_dialog_t file_dialog, native_string_list_t extensions) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    return false;
  }
  try {
    std::vector<std::string> extensions_cpp;
    for (long i = 0; i < extensions.count; ++i) {
      extensions_cpp.emplace_back(extensions.items[i] ? extensions.items[i] : "");
    }
    return self->SetFileTypes(extensions_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_set_file_types");
    return false;
  }
}

bool native_file_dialog_set_suggested_file_name(native_file_dialog_t file_dialog, const char* name) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->SetSuggestedFileName(std::string(name ? name : ""));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_set_suggested_file_name");
    return false;
  }
}

native_dialog_modality_t native_file_dialog_get_modality(native_file_dialog_t file_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    return (native_dialog_modality_t)0;
  }
  try {
    return to_c_dialog_modality(self->GetModality());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_get_modality");
    return (native_dialog_modality_t)0;
  }
}

void native_file_dialog_set_modality(native_file_dialog_t file_dialog, native_dialog_modality_t modality) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    return;
  }
  try {
    self->SetModality(to_cpp_dialog_modality(modality));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_set_modality");
    return;
  }
}

bool native_file_dialog_open(native_file_dialog_t file_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->Open();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_open");
    return false;
  }
}

bool native_file_dialog_close(native_file_dialog_t file_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    return false;
  }
  try {
    return self->Close();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_close");
    return false;
  }
}

native_file_dialog_result_t native_file_dialog_get_result(native_file_dialog_t file_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    return (native_file_dialog_result_t)NATIVE_FILE_DIALOG_RESULT_NONE;
  }
  try {
    return to_c_file_dialog_result(self->GetResult());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_get_result");
    return (native_file_dialog_result_t)NATIVE_FILE_DIALOG_RESULT_NONE;
  }
}

native_string_list_t native_file_dialog_get_paths(native_file_dialog_t file_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    native_string_list_t empty = {};
    return empty;
  }
  try {
    return to_c_string_list(self->GetPaths());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_get_paths");
    native_string_list_t empty = {};
    return empty;
  }
}

char* native_file_dialog_get_last_error(native_file_dialog_t file_dialog) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::FileDialog>(file_dialog);
  if (!self) {
    return nullptr;
  }
  try {
    return to_c_str(self->GetLastError());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_file_dialog_get_last_error");
    return nullptr;
  }
}

void native_file_dialog_free(native_file_dialog_t file_dialog) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(file_dialog);
}

