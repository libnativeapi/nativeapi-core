// Label, Button, TextField and ImageView on GTK 3.

#include "../../view.h"

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

/// A multi-line field: a GtkTextView in a GtkScrolledWindow. The scrolled
/// window is the view's widget; the text view is handed back through
/// `text_view`.
GtkWidget* MakeTextView(const std::string& text,
                        const Platform& platform,
                        GtkTextView** text_view) {
  *text_view = nullptr;
  GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
  GtkWidget* view = gtk_text_view_new();
  if (!scrolled || !view) {
    if (scrolled) {
      g_object_ref_sink(scrolled);
      g_object_unref(scrolled);
    }
    if (view) {
      g_object_ref_sink(view);
      g_object_unref(view);
    }
    return nullptr;
  }
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(view), platform.editable ? TRUE : FALSE);
  gtk_text_view_set_justification(GTK_TEXT_VIEW(view), ToGtkJustification(platform.text_alignment));
  GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  if (buffer) {
    gtk_text_buffer_set_text(buffer, text.c_str(), -1);
  }
  gtk_container_add(GTK_CONTAINER(scrolled), view);
  gtk_widget_show(view);
  *text_view = GTK_TEXT_VIEW(view);
  return scrolled;
}

// The text properties shared by Label and TextField.

void SetFieldTextColor(Impl& impl, Color color) {
  if (!impl.platform->widget) {
    return;
  }
  impl.platform->text_color = color;
  impl.platform->ApplyCss();
}

void SetFieldFontSize(Impl& impl, double size) {
  if (!impl.platform->widget) {
    return;
  }
  impl.platform->font_size = size < 0 ? 0 : size;
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

// ---------------------------------------------------------------------------
// TextField
// ---------------------------------------------------------------------------

TextField::TextField(const std::string& text) : View(Adopt(MakeEntry(text, nullptr))) {}

void TextField::SetText(const std::string& text) {
  auto& platform = *pimpl_->platform;
  if (!platform.widget) {
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

void TextField::SetTextAlignment(TextAlignment alignment) {
  auto& platform = *pimpl_->platform;
  platform.text_alignment = alignment;
  if (platform.text_view) {
    gtk_text_view_set_justification(platform.text_view, ToGtkJustification(alignment));
  } else if (platform.widget && GTK_IS_ENTRY(platform.widget)) {
    gtk_entry_set_alignment(GTK_ENTRY(platform.widget), ToGtkXAlign(alignment));
  }
}

TextAlignment TextField::GetTextAlignment() const {
  return pimpl_->platform->text_alignment;
}

void TextField::SetPlaceholder(const std::optional<std::string>& placeholder) {
  auto& platform = *pimpl_->platform;
  platform.placeholder = placeholder;
  // GtkTextView has no placeholder: a multi-line field only remembers it, for
  // the entry it turns back into.
  if (platform.widget && GTK_IS_ENTRY(platform.widget)) {
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
  if (is_multiline) {
    replacement = MakeTextView(text, platform, &replacement_text_view);
  } else {
    replacement = MakeEntry(text, &platform);
  }
  if (!replacement) {
    return;
  }
  platform.multiline = is_multiline;
  platform.ReplaceWidget(replacement, replacement_text_view);
  pimpl_->InvalidateIntrinsicSize();
}

bool TextField::IsMultiline() const {
  return pimpl_->platform->multiline;
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
