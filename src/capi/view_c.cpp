// AUTO-GENERATED. DO NOT EDIT.
// Any manual changes WILL BE LOST when this file is regenerated.

#include "view_c.h"

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
#include "../foundation/geometry.h"
#include "geometry_c.h"
#include "../foundation/color.h"
#include "color_c.h"
#include "../image.h"
#include "image_c.h"
#include "../window.h"
#include "window_c.h"
#include "../view.h"

native_view_t native_view_create(void) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::View>());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_create");
    return 0;
  }
}

native_view_t native_view_create_with_native_view(void* native_view) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::View>(native_view));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_create_with_native_view");
    return 0;
  }
}

bool native_view_is_supported(void) {
  try {
    return nativeapi::View::IsSupported();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_is_supported");
    return false;
  }
}

native_view_id_t native_view_get_id(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return 0;
  }
  try {
    return self->GetId();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_id");
    return 0;
  }
}

void native_view_add_subview(native_view_t view, native_view_t subview) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    auto subview_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(subview);
    self->AddSubview(subview_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_add_subview");
    return;
  }
}

void native_view_insert_subview(native_view_t view, unsigned long index, native_view_t subview) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    auto subview_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(subview);
    self->InsertSubview(index, subview_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_insert_subview");
    return;
  }
}

bool native_view_remove_subview(native_view_t view, native_view_t subview) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return false;
  }
  try {
    auto subview_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(subview);
    return self->RemoveSubview(subview_cpp);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_remove_subview");
    return false;
  }
}

bool native_view_remove_subview_at(native_view_t view, unsigned long index) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return false;
  }
  try {
    return self->RemoveSubviewAt(index);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_remove_subview_at");
    return false;
  }
}

void native_view_clear_subviews(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    self->ClearSubviews();
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_clear_subviews");
    return;
  }
}

unsigned long native_view_get_subview_count(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return 0;
  }
  try {
    return self->GetSubviewCount();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_subview_count");
    return 0;
  }
}

native_view_t native_view_get_subview_at(native_view_t view, unsigned long index) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return 0;
  }
  try {
    return nativeapi::HandleTable::GetInstance().Insert(self->GetSubviewAt(index));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_subview_at");
    return 0;
  }
}

native_view_list_t native_view_get_subviews(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    native_view_list_t empty = {};
    return empty;
  }
  try {
    const auto items = self->GetSubviews();
    native_view_list_t list = {};
    if (items.empty()) {
      return list;
    }
    list.views = new (std::nothrow) native_view_t[items.size()];
    if (!list.views) {
      return list;
    }
    for (size_t i = 0; i < items.size(); ++i) {
      list.views[i] = nativeapi::HandleTable::GetInstance().Insert(items[i]);
    }
    list.count = static_cast<long>(items.size());
    return list;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_subviews");
    native_view_list_t empty = {};
    return empty;
  }
}

native_view_t native_view_get_parent(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return 0;
  }
  try {
    return nativeapi::HandleTable::GetInstance().Insert(self->GetParent());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_parent");
    return 0;
  }
}

native_window_t native_view_get_window(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return 0;
  }
  try {
    return nativeapi::HandleTable::GetInstance().Insert(self->GetWindow());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_window");
    return 0;
  }
}

void native_view_set_frame(native_view_t view, native_rectangle_t frame) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    auto frame_cpp = to_cpp_rectangle(frame);
    self->SetFrame(frame_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_frame");
    return;
  }
}

native_rectangle_t native_view_get_frame(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    native_rectangle_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetFrame();
    return to_c_rectangle(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_frame");
    native_rectangle_t result = {};
    return result;
  }
}

void native_view_set_preferred_size(native_view_t view, native_size_t size) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    auto size_cpp = to_cpp_size(size);
    self->SetPreferredSize(size_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_preferred_size");
    return;
  }
}

native_size_t native_view_get_preferred_size(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    native_size_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetPreferredSize();
    return to_c_size(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_preferred_size");
    native_size_t result = {};
    return result;
  }
}

native_size_t native_view_get_intrinsic_size(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    native_size_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetIntrinsicSize();
    return to_c_size(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_intrinsic_size");
    native_size_t result = {};
    return result;
  }
}

void native_view_set_flex(native_view_t view, double flex) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    self->SetFlex(flex);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_flex");
    return;
  }
}

double native_view_get_flex(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return 0;
  }
  try {
    return self->GetFlex();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_flex");
    return 0;
  }
}

void native_view_set_alignment(native_view_t view, native_view_alignment_t alignment) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    self->SetAlignment(to_cpp_view_alignment(alignment));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_alignment");
    return;
  }
}

native_view_alignment_t native_view_get_alignment(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return (native_view_alignment_t)NATIVE_VIEW_ALIGNMENT_STRETCH;
  }
  try {
    return to_c_view_alignment(self->GetAlignment());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_alignment");
    return (native_view_alignment_t)NATIVE_VIEW_ALIGNMENT_STRETCH;
  }
}

void native_view_set_layout(native_view_t view, native_view_layout_t layout) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    self->SetLayout(to_cpp_view_layout(layout));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_layout");
    return;
  }
}

native_view_layout_t native_view_get_layout(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return (native_view_layout_t)NATIVE_VIEW_LAYOUT_ABSOLUTE;
  }
  try {
    return to_c_view_layout(self->GetLayout());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_layout");
    return (native_view_layout_t)NATIVE_VIEW_LAYOUT_ABSOLUTE;
  }
}

void native_view_set_spacing(native_view_t view, double spacing) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    self->SetSpacing(spacing);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_spacing");
    return;
  }
}

double native_view_get_spacing(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return 0;
  }
  try {
    return self->GetSpacing();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_spacing");
    return 0;
  }
}

void native_view_set_padding(native_view_t view, native_edge_insets_t padding) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    auto padding_cpp = to_cpp_edge_insets(padding);
    self->SetPadding(padding_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_padding");
    return;
  }
}

native_edge_insets_t native_view_get_padding(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    native_edge_insets_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetPadding();
    return to_c_edge_insets(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_padding");
    native_edge_insets_t result = {};
    return result;
  }
}

void native_view_set_visible(native_view_t view, bool is_visible) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    self->SetVisible(is_visible);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_visible");
    return;
  }
}

bool native_view_is_visible(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return false;
  }
  try {
    return self->IsVisible();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_is_visible");
    return false;
  }
}

void native_view_set_enabled(native_view_t view, bool is_enabled) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    self->SetEnabled(is_enabled);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_enabled");
    return;
  }
}

bool native_view_is_enabled(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return false;
  }
  try {
    return self->IsEnabled();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_is_enabled");
    return false;
  }
}

void native_view_set_background_color(native_view_t view, native_color_t color) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    auto color_cpp = to_cpp_color(color);
    self->SetBackgroundColor(color_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_background_color");
    return;
  }
}

native_color_t native_view_get_background_color(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    native_color_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetBackgroundColor();
    return to_c_color(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_background_color");
    native_color_t result = {};
    return result;
  }
}

void native_view_set_tooltip(native_view_t view, const char* tooltip) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    std::optional<std::string> tooltip_cpp;
    if (tooltip) {
      tooltip_cpp = std::string(tooltip);
    }
    self->SetTooltip(tooltip_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_set_tooltip");
    return;
  }
}

char* native_view_get_tooltip(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return nullptr;
  }
  try {
    const auto cpp_result = self->GetTooltip();
    return cpp_result ? to_c_str(*cpp_result) : nullptr;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_get_tooltip");
    return nullptr;
  }
}

void native_view_focus(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    self->Focus();
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_focus");
    return;
  }
}

void native_view_blur(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return;
  }
  try {
    self->Blur();
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_blur");
    return;
  }
}

bool native_view_is_focused(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return false;
  }
  try {
    return self->IsFocused();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_view_is_focused");
    return false;
  }
}

void* native_view_get_native_object(native_view_t view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return nullptr;
  }
  return self->GetNativeObject();
}

void native_view_free(native_view_t view) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(view);
}

void native_view_list_free(native_view_list_t* list) {
  if (!list || !list->views) {
    return;
  }
  for (long i = 0; i < list->count; ++i) {
    nativeapi::HandleTable::GetInstance().Release(list->views[i]);
  }
  delete[] list->views;
  list->views = nullptr;
  list->count = 0;
}

void native_view_list_release(native_view_list_t* list) {
  if (!list) {
    return;
  }
  delete[] list->views;
  list->views = nullptr;
  list->count = 0;
}

native_listener_id_t native_view_add_listener(native_view_t view, native_view_event_callback_t callback, void* user_data, native_release_user_data_t release_user_data) {
  auto holder = nativeapi::capi::UserData::Make(user_data, release_user_data);
  if (!callback) {
    return 0;
  }
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return 0;
  }
  try {
    return static_cast<native_listener_id_t>(self->AddListener<nativeapi::ViewEvent>(
        [callback, holder](const nativeapi::ViewEvent& event) {
          native_view_event_t c_event = {};
          if (!to_c_view_event(event, &c_event)) {
            return;
          }
          callback(&c_event, holder->get());
          free_c_view_event(&c_event);
        }));
  } catch (...) {
    return 0;
  }
}

bool native_view_remove_listener(native_view_t view, native_listener_id_t listener_id) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::View>(view);
  if (!self) {
    return false;
  }
  try {
    return self->RemoveListener(static_cast<size_t>(listener_id));
  } catch (...) {
    return false;
  }
}

native_label_t native_label_create(const char* text) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::Label>(std::string(text ? text : "")));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_label_create");
    return 0;
  }
}

void native_label_set_text(native_label_t label, const char* text) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Label>(label);
  if (!self) {
    return;
  }
  try {
    self->SetText(std::string(text ? text : ""));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_label_set_text");
    return;
  }
}

char* native_label_get_text(native_label_t label) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Label>(label);
  if (!self) {
    return nullptr;
  }
  try {
    return to_c_str(self->GetText());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_label_get_text");
    return nullptr;
  }
}

void native_label_set_text_color(native_label_t label, native_color_t color) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Label>(label);
  if (!self) {
    return;
  }
  try {
    auto color_cpp = to_cpp_color(color);
    self->SetTextColor(color_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_label_set_text_color");
    return;
  }
}

native_color_t native_label_get_text_color(native_label_t label) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Label>(label);
  if (!self) {
    native_color_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetTextColor();
    return to_c_color(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_label_get_text_color");
    native_color_t result = {};
    return result;
  }
}

void native_label_set_font_size(native_label_t label, double size) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Label>(label);
  if (!self) {
    return;
  }
  try {
    self->SetFontSize(size);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_label_set_font_size");
    return;
  }
}

double native_label_get_font_size(native_label_t label) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Label>(label);
  if (!self) {
    return 0;
  }
  try {
    return self->GetFontSize();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_label_get_font_size");
    return 0;
  }
}

void native_label_set_text_alignment(native_label_t label, native_text_alignment_t alignment) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Label>(label);
  if (!self) {
    return;
  }
  try {
    self->SetTextAlignment(to_cpp_text_alignment(alignment));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_label_set_text_alignment");
    return;
  }
}

native_text_alignment_t native_label_get_text_alignment(native_label_t label) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Label>(label);
  if (!self) {
    return (native_text_alignment_t)NATIVE_TEXT_ALIGNMENT_START;
  }
  try {
    return to_c_text_alignment(self->GetTextAlignment());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_label_get_text_alignment");
    return (native_text_alignment_t)NATIVE_TEXT_ALIGNMENT_START;
  }
}

void native_label_free(native_label_t label) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(label);
}

native_button_t native_button_create(const char* text) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::Button>(std::string(text ? text : "")));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_button_create");
    return 0;
  }
}

void native_button_set_text(native_button_t button, const char* text) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Button>(button);
  if (!self) {
    return;
  }
  try {
    self->SetText(std::string(text ? text : ""));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_button_set_text");
    return;
  }
}

char* native_button_get_text(native_button_t button) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Button>(button);
  if (!self) {
    return nullptr;
  }
  try {
    return to_c_str(self->GetText());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_button_get_text");
    return nullptr;
  }
}

void native_button_free(native_button_t button) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(button);
}

native_text_field_t native_text_field_create(const char* text) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::TextField>(std::string(text ? text : "")));
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_create");
    return 0;
  }
}

void native_text_field_set_text(native_text_field_t text_field, const char* text) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return;
  }
  try {
    self->SetText(std::string(text ? text : ""));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_set_text");
    return;
  }
}

char* native_text_field_get_text(native_text_field_t text_field) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return nullptr;
  }
  try {
    return to_c_str(self->GetText());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_get_text");
    return nullptr;
  }
}

void native_text_field_set_text_color(native_text_field_t text_field, native_color_t color) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return;
  }
  try {
    auto color_cpp = to_cpp_color(color);
    self->SetTextColor(color_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_set_text_color");
    return;
  }
}

native_color_t native_text_field_get_text_color(native_text_field_t text_field) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    native_color_t result = {};
    return result;
  }
  try {
    const auto cpp_result = self->GetTextColor();
    return to_c_color(cpp_result);
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_get_text_color");
    native_color_t result = {};
    return result;
  }
}

void native_text_field_set_font_size(native_text_field_t text_field, double size) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return;
  }
  try {
    self->SetFontSize(size);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_set_font_size");
    return;
  }
}

double native_text_field_get_font_size(native_text_field_t text_field) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return 0;
  }
  try {
    return self->GetFontSize();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_get_font_size");
    return 0;
  }
}

void native_text_field_set_text_alignment(native_text_field_t text_field, native_text_alignment_t alignment) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return;
  }
  try {
    self->SetTextAlignment(to_cpp_text_alignment(alignment));
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_set_text_alignment");
    return;
  }
}

native_text_alignment_t native_text_field_get_text_alignment(native_text_field_t text_field) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return (native_text_alignment_t)NATIVE_TEXT_ALIGNMENT_START;
  }
  try {
    return to_c_text_alignment(self->GetTextAlignment());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_get_text_alignment");
    return (native_text_alignment_t)NATIVE_TEXT_ALIGNMENT_START;
  }
}

void native_text_field_set_placeholder(native_text_field_t text_field, const char* placeholder) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return;
  }
  try {
    std::optional<std::string> placeholder_cpp;
    if (placeholder) {
      placeholder_cpp = std::string(placeholder);
    }
    self->SetPlaceholder(placeholder_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_set_placeholder");
    return;
  }
}

char* native_text_field_get_placeholder(native_text_field_t text_field) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return nullptr;
  }
  try {
    const auto cpp_result = self->GetPlaceholder();
    return cpp_result ? to_c_str(*cpp_result) : nullptr;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_get_placeholder");
    return nullptr;
  }
}

void native_text_field_set_editable(native_text_field_t text_field, bool is_editable) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return;
  }
  try {
    self->SetEditable(is_editable);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_set_editable");
    return;
  }
}

bool native_text_field_is_editable(native_text_field_t text_field) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return false;
  }
  try {
    return self->IsEditable();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_is_editable");
    return false;
  }
}

void native_text_field_set_secure(native_text_field_t text_field, bool is_secure) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return;
  }
  try {
    self->SetSecure(is_secure);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_set_secure");
    return;
  }
}

bool native_text_field_is_secure(native_text_field_t text_field) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return false;
  }
  try {
    return self->IsSecure();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_is_secure");
    return false;
  }
}

void native_text_field_set_multiline(native_text_field_t text_field, bool is_multiline) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return;
  }
  try {
    self->SetMultiline(is_multiline);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_set_multiline");
    return;
  }
}

bool native_text_field_is_multiline(native_text_field_t text_field) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::TextField>(text_field);
  if (!self) {
    return false;
  }
  try {
    return self->IsMultiline();
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_text_field_is_multiline");
    return false;
  }
}

void native_text_field_free(native_text_field_t text_field) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(text_field);
}

native_image_view_t native_image_view_create(void) {
  try {
    return nativeapi::HandleTable::GetInstance().Insert(
        std::make_shared<nativeapi::ImageView>());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_image_view_create");
    return 0;
  }
}

void native_image_view_set_image(native_image_view_t image_view, native_image_t image) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ImageView>(image_view);
  if (!self) {
    return;
  }
  try {
    auto image_cpp = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::Image>(image);
    self->SetImage(image_cpp);
    return;
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_image_view_set_image");
    return;
  }
}

native_image_t native_image_view_get_image(native_image_view_t image_view) {
  auto self = nativeapi::HandleTable::GetInstance().Resolve<nativeapi::ImageView>(image_view);
  if (!self) {
    return 0;
  }
  try {
    return nativeapi::HandleTable::GetInstance().Insert(self->GetImage());
  } catch (...) {
    fprintf(stderr, "[nativeapi] %s: unexpected exception\n", "native_image_view_get_image");
    return 0;
  }
}

void native_image_view_free(native_image_view_t image_view) {
  // The table invalidates the handle itself, so releasing an unknown or
  // already-released one is a no-op rather than a double free.
  nativeapi::HandleTable::GetInstance().Release(image_view);
}

bool to_c_view_event(const nativeapi::ViewEvent& event, native_view_event_t* out) {
  if (!out) {
    return false;
  }
  *out = native_view_event_t{};
  out->view_id = event.GetViewId();
  if (const auto* typed = dynamic_cast<const nativeapi::ViewFocusedEvent*>(&event)) {
    out->type = NATIVE_VIEW_EVENT_TYPE_FOCUSED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::ViewBlurredEvent*>(&event)) {
    out->type = NATIVE_VIEW_EVENT_TYPE_BLURRED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::ButtonClickedEvent*>(&event)) {
    out->type = NATIVE_VIEW_EVENT_TYPE_BUTTON_CLICKED;
    (void)typed;
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::TextFieldChangedEvent*>(&event)) {
    out->type = NATIVE_VIEW_EVENT_TYPE_TEXT_FIELD_CHANGED;
    out->data.text_field_changed.text = to_c_str(typed->GetText());
    return true;
  }
  if (const auto* typed = dynamic_cast<const nativeapi::TextFieldSubmittedEvent*>(&event)) {
    out->type = NATIVE_VIEW_EVENT_TYPE_TEXT_FIELD_SUBMITTED;
    (void)typed;
    return true;
  }
  return false;
}

void free_c_view_event(native_view_event_t* value) {
  if (!value) {
    return;
  }
  if (value->type == NATIVE_VIEW_EVENT_TYPE_TEXT_FIELD_CHANGED) {
    free_c_str(value->data.text_field_changed.text);
    value->data.text_field_changed.text = nullptr;
  }
}

