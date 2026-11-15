// Label, Button, TextField and ImageView on GTK 3.

#include "../../view.h"

#include <algorithm>
#include <string>

#include "view_internal_linux.h"

namespace nativeapi {

namespace {

using Impl = ViewInternal::Impl;
using Platform = ViewInternal::Impl::Platform;

/// A control the view will own: the floating reference is sunk here and
/// handed to View(NativeControl), whose Platform releases it. Shown at once so
/// the control appears as soon as it is parented.
ViewInternal::NativeControl Adopt(GtkWidget* widget) {
  if (widget) {
    g_object_ref_sink(widget);
    gtk_widget_show(widget);
  }
  return ViewInternal::NativeControl{widget};
}

GtkWidget* MakeLabel(const std::string& text) {
  GtkWidget* label = gtk_label_new(text.c_str());
  if (!label) {
    return nullptr;
  }
  gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_yalign(GTK_LABEL(label), 0.0f);
  return label;
}

/// A single-line field carrying the state a Platform remembers.
GtkWidget* MakeEntry(const std::string& text, const Platform* platform) {
  GtkWidget* entry = gtk_entry_new();
  if (!entry) {
    return nullptr;
  }
  gtk_entry_set_text(GTK_ENTRY(entry), text.c_str());
  gtk_entry_set_has_frame(GTK_ENTRY(entry), (!platform || platform->bordered) ? TRUE : FALSE);
  if (platform) {
    gtk_entry_set_visibility(GTK_ENTRY(entry), platform->secure ? FALSE : TRUE);
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), platform->secure ? GTK_INPUT_PURPOSE_PASSWORD
                                                                   : GTK_INPUT_PURPOSE_FREE_FORM);
    gtk_editable_set_editable(GTK_EDITABLE(entry), platform->editable ? TRUE : FALSE);
    gtk_entry_set_alignment(GTK_ENTRY(entry), ToGtkXAlign(platform->text_alignment));
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(entry), platform->placeholder ? platform->placeholder->c_str() : nullptr);
  }
  return entry;
}

/// A multi-line field: a GtkTextView in a GtkScrolledWindow wrapped by a
/// GtkOverlay so a pass-through label can render its placeholder text.
GtkWidget* MakeTextView(const std::string& text,
                        const Platform& platform,
                        GtkTextView** text_view,
                        GtkLabel** placeholder_label) {
  *text_view = nullptr;
  *placeholder_label = nullptr;
  GtkWidget* overlay = gtk_overlay_new();
  GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
  GtkWidget* view = gtk_text_view_new();
  GtkWidget* label = gtk_label_new(platform.placeholder ? platform.placeholder->c_str() : "");
  if (!overlay || !scrolled || !view || !label) {
    for (GtkWidget* candidate : {overlay, scrolled, view, label}) {
      if (candidate) {
        g_object_ref_sink(candidate);
        g_object_unref(candidate);
      }
    }
    return nullptr;
  }
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                      platform.bordered ? GTK_SHADOW_IN : GTK_SHADOW_NONE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(view), platform.editable ? TRUE : FALSE);
  gtk_text_view_set_justification(GTK_TEXT_VIEW(view), ToGtkJustification(platform.text_alignment));
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  if (buffer) {
    gtk_text_buffer_set_text(buffer, text.c_str(), -1);
  }
  gtk_container_add(GTK_CONTAINER(scrolled), view);
  gtk_widget_show(view);
  gtk_widget_show(scrolled);

  gtk_label_set_xalign(GTK_LABEL(label), ToGtkXAlign(platform.text_alignment));
  gtk_widget_set_halign(label, GTK_ALIGN_FILL);
  gtk_widget_set_valign(label, GTK_ALIGN_START);
  gtk_widget_set_margin_start(label, platform.bordered ? 6 : 0);
  gtk_widget_set_margin_end(label, platform.bordered ? 6 : 0);
  gtk_widget_set_margin_top(label, platform.bordered ? 4 : 0);
  gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim-label");

  gtk_container_add(GTK_CONTAINER(overlay), scrolled);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), label);
  gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), label, TRUE);

  *text_view = GTK_TEXT_VIEW(view);
  *placeholder_label = GTK_LABEL(label);
  return overlay;
}

// The text properties shared by Label and TextField.

void SetFieldTextColor(Impl& impl, Color color) {
  if (!impl.platform->widget || impl.platform->text_color.ToRGBA() == color.ToRGBA()) {
    return;
  }
  impl.platform->text_color = color;
  impl.platform->ApplyCss();
}

void SetFieldFontSize(Impl& impl, double size) {
  const double clamped = size < 0 ? 0 : size;
  if (!impl.platform->widget || impl.platform->font_size == clamped) {
    return;
  }
  impl.platform->font_size = clamped;
  impl.platform->ApplyCss();
  impl.InvalidateIntrinsicSize();
}

void SetControlFontFamily(Impl& impl, const std::optional<std::string>& family) {
  if (!impl.platform->widget || impl.platform->font_family == family) {
    return;
  }
  impl.platform->font_family = family;
  impl.platform->ApplyCss();
  impl.InvalidateIntrinsicSize();
}

void SetControlFontWeight(Impl& impl, int weight) {
  const int clamped = weight <= 0 ? 0 : std::clamp(weight, 100, 900);
  if (!impl.platform->widget || impl.platform->font_weight == clamped) {
    return;
  }
  impl.platform->font_weight = clamped;
  impl.platform->ApplyCss();
  impl.InvalidateIntrinsicSize();
}

}  // namespace

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------

Label::Label(const std::string& text) : View(Adopt(MakeLabel(text))) {}

void Label::SetText(const std::string& text) {
  GtkWidget* widget = pimpl_->platform->widget;
  if (!widget || !GTK_IS_LABEL(widget)) {
    return;
  }
  gtk_label_set_text(GTK_LABEL(widget), text.c_str());
  pimpl_->InvalidateIntrinsicSize();
}

std::string Label::GetText() const {
  GtkWidget* widget = pimpl_->platform->widget;
  if (!widget || !GTK_IS_LABEL(widget)) {
    return "";
  }
  const gchar* text = gtk_label_get_text(GTK_LABEL(widget));
  return text ? text : "";
}

void Label::SetTextColor(Color color) {
  SetFieldTextColor(*pimpl_, color);
}

Color Label::GetTextColor() const {
  return pimpl_->platform->text_color;
}

void Label::SetFontSize(double size) {
  SetFieldFontSize(*pimpl_, size);
}

double Label::GetFontSize() const {
  return pimpl_->platform->font_size;
}

void Label::SetFontFamily(const std::optional<std::string>& family) {
  SetControlFontFamily(*pimpl_, family);
}

std::optional<std::string> Label::GetFontFamily() const {
  return pimpl_->platform->font_family;
}

void Label::SetFontWeight(int weight) {
  SetControlFontWeight(*pimpl_, weight);
}

int Label::GetFontWeight() const {
  return pimpl_->platform->font_weight;
}

void Label::SetTextAlignment(TextAlignment alignment) {
  GtkWidget* widget = pimpl_->platform->widget;
  if (!widget || !GTK_IS_LABEL(widget)) {
    return;
  }
  pimpl_->platform->text_alignment = alignment;
  // xalign places a short text inside the allocation; justify aligns the
  // lines of a wrapped one against each other.
  gtk_label_set_xalign(GTK_LABEL(widget), ToGtkXAlign(alignment));
  gtk_label_set_justify(GTK_LABEL(widget), ToGtkJustification(alignment));
}

TextAlignment Label::GetTextAlignment() const {
  return pimpl_->platform->text_alignment;
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

Button::Button(const std::string& text) : View(Adopt(gtk_button_new_with_label(text.c_str()))) {}

void Button::SetText(const std::string& text) {
  GtkWidget* widget = pimpl_->platform->widget;
  if (!widget || !GTK_IS_BUTTON(widget)) {
    return;
  }
  gtk_button_set_label(GTK_BUTTON(widget), text.c_str());
  pimpl_->InvalidateIntrinsicSize();
}

std::string Button::GetText() const {
  GtkWidget* widget = pimpl_->platform->widget;
  if (!widget || !GTK_IS_BUTTON(widget)) {
    return "";
  }
  const gchar* text = gtk_button_get_label(GTK_BUTTON(widget));
  return text ? text : "";
}

void Button::SetFontSize(double size) {
  SetFieldFontSize(*pimpl_, size);
}

double Button::GetFontSize() const {
  return pimpl_->platform->font_size;
}

void Button::SetFontFamily(const std::optional<std::string>& family) {
  SetControlFontFamily(*pimpl_, family);
}

std::optional<std::string> Button::GetFontFamily() const {
  return pimpl_->platform->font_family;
}

void Button::SetFontWeight(int weight) {
  SetControlFontWeight(*pimpl_, weight);
}

int Button::GetFontWeight() const {
  return pimpl_->platform->font_weight;
}

// ---------------------------------------------------------------------------
// TextField
// ---------------------------------------------------------------------------

TextField::TextField(const std::string& text) : View(Adopt(MakeEntry(text, nullptr))) {}

void TextField::SetText(const std::string& text) {
  auto& platform = *pimpl_->platform;
  if (!platform.widget || ReadWidgetText(platform.widget, platform.text_view) == text) {
    return;
  }
  // A programmatic change is not user input: hold the "changed" signals back.
  ++platform.suppress_changed;
  if (platform.text_view) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(platform.text_view);
    if (buffer) {
      gtk_text_buffer_set_text(buffer, text.c_str(), -1);
    }
  } else if (GTK_IS_ENTRY(platform.widget)) {
    gtk_entry_set_text(GTK_ENTRY(platform.widget), text.c_str());
  }
  --platform.suppress_changed;
}

std::string TextField::GetText() const {
  auto& platform = *pimpl_->platform;
  return ReadWidgetText(platform.widget, platform.text_view);
}

void TextField::SetTextColor(Color color) {
  SetFieldTextColor(*pimpl_, color);
}

Color TextField::GetTextColor() const {
  return pimpl_->platform->text_color;
}

void TextField::SetFontSize(double size) {
  SetFieldFontSize(*pimpl_, size);
}

double TextField::GetFontSize() const {
  return pimpl_->platform->font_size;
}

void TextField::SetFontFamily(const std::optional<std::string>& family) {
  SetControlFontFamily(*pimpl_, family);
}

std::optional<std::string> TextField::GetFontFamily() const {
  return pimpl_->platform->font_family;
}

void TextField::SetFontWeight(int weight) {
  SetControlFontWeight(*pimpl_, weight);
}

int TextField::GetFontWeight() const {
  return pimpl_->platform->font_weight;
}

void TextField::SetTextAlignment(TextAlignment alignment) {
  auto& platform = *pimpl_->platform;
  platform.text_alignment = alignment;
  if (platform.text_view) {
    gtk_text_view_set_justification(platform.text_view, ToGtkJustification(alignment));
    if (platform.placeholder_label) {
      gtk_label_set_xalign(platform.placeholder_label, ToGtkXAlign(alignment));
    }
  } else if (platform.widget && GTK_IS_ENTRY(platform.widget)) {
    gtk_entry_set_alignment(GTK_ENTRY(platform.widget), ToGtkXAlign(alignment));
  }
}

TextAlignment TextField::GetTextAlignment() const {
  return pimpl_->platform->text_alignment;
}

void TextField::SetPlaceholder(const std::optional<std::string>& placeholder) {
  auto& platform = *pimpl_->platform;
  if (platform.placeholder == placeholder) {
    return;
  }
  platform.placeholder = placeholder;
  if (platform.placeholder_label) {
    platform.SyncPlaceholderVisibility();
  } else if (platform.widget && GTK_IS_ENTRY(platform.widget)) {
    gtk_entry_set_placeholder_text(GTK_ENTRY(platform.widget),
                                   placeholder ? placeholder->c_str() : nullptr);
  }
}

std::optional<std::string> TextField::GetPlaceholder() const {
  return pimpl_->platform->placeholder;
}

void TextField::SetEditable(bool is_editable) {
  auto& platform = *pimpl_->platform;
  platform.editable = is_editable;
  if (platform.text_view) {
    gtk_text_view_set_editable(platform.text_view, is_editable ? TRUE : FALSE);
  } else if (platform.widget && GTK_IS_ENTRY(platform.widget)) {
    gtk_editable_set_editable(GTK_EDITABLE(platform.widget), is_editable ? TRUE : FALSE);
  }
}

bool TextField::IsEditable() const {
  return pimpl_->platform->editable;
}

void TextField::SetSecure(bool is_secure) {
  auto& platform = *pimpl_->platform;
  platform.secure = is_secure;
  // GtkTextView cannot mask its text: a multi-line field only remembers the
  // flag, for the entry it turns back into.
  if (platform.widget && GTK_IS_ENTRY(platform.widget)) {
    gtk_entry_set_visibility(GTK_ENTRY(platform.widget), is_secure ? FALSE : TRUE);
    gtk_entry_set_input_purpose(GTK_ENTRY(platform.widget), is_secure
                                                                ? GTK_INPUT_PURPOSE_PASSWORD
                                                                : GTK_INPUT_PURPOSE_FREE_FORM);
  }
}

bool TextField::IsSecure() const {
  return pimpl_->platform->secure;
}

void TextField::SetMultiline(bool is_multiline) {
  auto& platform = *pimpl_->platform;
  if (!platform.widget || platform.multiline == is_multiline) {
    return;
  }
  // GtkEntry and GtkTextView are different widgets: rebuild the control and
  // carry the text and the remembered state over. ReplaceWidget() keeps the
  // frame, parent, z-order, stylesheet and the listening state.
  const std::string text = ReadWidgetText(platform.widget, platform.text_view);
  GtkWidget* replacement = nullptr;
  GtkTextView* replacement_text_view = nullptr;
  GtkLabel* replacement_placeholder = nullptr;
  if (is_multiline) {
    replacement = MakeTextView(text, platform, &replacement_text_view, &replacement_placeholder);
  } else {
    replacement = MakeEntry(text, &platform);
  }
  if (!replacement) {
    return;
  }
  platform.multiline = is_multiline;
  platform.ReplaceWidget(replacement, replacement_text_view, replacement_placeholder);
  pimpl_->InvalidateIntrinsicSize();
}

bool TextField::IsMultiline() const {
  return pimpl_->platform->multiline;
}

void TextField::SetBordered(bool is_bordered) {
  auto& platform = *pimpl_->platform;
  if (platform.bordered == is_bordered) {
    return;
  }
  platform.bordered = is_bordered;
  if (platform.text_view && platform.widget && GTK_IS_BIN(platform.widget)) {
    GtkWidget* scrolled = gtk_bin_get_child(GTK_BIN(platform.widget));
    if (scrolled && GTK_IS_SCROLLED_WINDOW(scrolled)) {
      gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                          is_bordered ? GTK_SHADOW_IN : GTK_SHADOW_NONE);
    }
    if (platform.placeholder_label) {
      GtkWidget* lbl = GTK_WIDGET(platform.placeholder_label);
      gtk_widget_set_margin_start(lbl, is_bordered ? 6 : 0);
      gtk_widget_set_margin_end(lbl, is_bordered ? 6 : 0);
      gtk_widget_set_margin_top(lbl, is_bordered ? 4 : 0);
    }
  } else if (platform.widget && GTK_IS_ENTRY(platform.widget)) {
    gtk_entry_set_has_frame(GTK_ENTRY(platform.widget), is_bordered ? TRUE : FALSE);
  }
  platform.ApplyCss();
  pimpl_->InvalidateIntrinsicSize();
}

bool TextField::IsBordered() const {
  return pimpl_->platform->bordered;
}

// ---------------------------------------------------------------------------
// ImageView
// ---------------------------------------------------------------------------

ImageView::ImageView() : View(Adopt(gtk_image_new())) {}

void ImageView::SetImage(std::shared_ptr<Image> image) {
  auto& platform = *pimpl_->platform;
  if (!platform.widget) {
    return;
  }
  if (platform.pixbuf) {
    g_object_unref(platform.pixbuf);
    platform.pixbuf = nullptr;
  }
  platform.image = image;
  // Image::GetNativeObject() is the GdkPixbuf*; keep our own reference so a
  // later change to the Image cannot pull it from under the GtkImage.
  if (image) {
    void* native = image->GetNativeObject();
    if (native && GDK_IS_PIXBUF(native)) {
      platform.pixbuf = GDK_PIXBUF(g_object_ref(native));
    }
  }
  platform.scaled_width = 0;
  platform.scaled_height = 0;
  int width = -1;
  int height = -1;
  gtk_widget_get_size_request(platform.widget, &width, &height);
  platform.ScaleImage(width, height);
  pimpl_->InvalidateIntrinsicSize();
}

std::shared_ptr<Image> ImageView::GetImage() const {
  return pimpl_->platform->image;
}

}  // namespace nativeapi
