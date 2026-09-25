#pragma once

// Shared between view_linux.cpp and view_controls_linux.cpp: the platform half
// of View::Impl on GTK 3 and the helpers that turn widget signals into events.

#include <gtk/gtk.h>

#include <memory>
#include <optional>
#include <string>

#include "../../image.h"
#include "../../view_impl.h"

namespace nativeapi {

/**
 * The GTK state of one View. `widget` is what View::Impl::native points at:
 * a GtkFixed for a plain container, the control itself for Label / Button /
 * TextField / ImageView, and the GtkScrolledWindow around a GtkTextView for a
 * multi-line TextField. The Platform holds one strong reference on `widget`
 * (the floating one sunk at creation for an owned control, an extra one for a
 * wrapped root) and drops it in its destructor.
 */
struct View::Impl::Platform {
  Platform(Impl* impl, GtkWidget* widget, bool owned);
  ~Platform();

  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  Impl* impl;
  GtkWidget* widget;
  /// The GtkTextView inside `widget` while a TextField is multi-line; owned by
  /// the scrolled window, never referenced separately.
  GtkTextView* text_view = nullptr;
  bool listening = false;

  // Signal handler ids; 0 while not connected.
  gulong clicked_id = 0;
  gulong changed_id = 0;
  gulong activate_id = 0;
  gulong buffer_changed_id = 0;
  /// The GtkTextBuffer `buffer_changed_id` is connected on.
  GtkTextBuffer* hooked_buffer = nullptr;
  gulong focus_in_id = 0;
  gulong focus_out_id = 0;
  gulong size_allocate_id = 0;
  gulong draw_id = 0;
  /// The last size "size-allocate" reported on a root, so a pass that only
  /// moved the widget does not lay the tree out again.
  int last_width = -1;
  int last_height = -1;

  // Everything a programmatic change should not report as user input.
  int suppress_changed = 0;

  // The stylesheet. One GtkCssProvider per view, selecting the widget by the
  // unique CSS name set at creation ("nativeapi-view-<id>"); rebuilt whenever
  // one of the three values changes. A zero alpha / zero size drops the rule.
  GtkCssProvider* css_provider = nullptr;
  GtkWidget* css_widget = nullptr;  // where the provider is currently added
  Color text_color{0, 0, 0, 0};
  double font_size = 0.0;
  Color background_color{0, 0, 0, 0};

  // TextField state that the widget cannot hold itself, or that must survive
  // the single-line / multi-line swap.
  bool secure = false;
  bool multiline = false;
  bool editable = true;
  TextAlignment text_alignment = TextAlignment::Start;
  std::optional<std::string> placeholder;

  // ImageView: the image and its unscaled pixbuf. GtkImage shows a copy scaled
  // to the frame; `scaled_width` / `scaled_height` remember which one.
  std::shared_ptr<Image> image;
  GdkPixbuf* pixbuf = nullptr;
  int scaled_width = 0;
  int scaled_height = 0;

  /// The widget that takes keyboard focus and receives the focus signals: the
  /// text view of a multi-line field, otherwise `widget`.
  GtkWidget* FocusWidget() const;
  /// The widget the stylesheet is attached to: same rule as FocusWidget().
  GtkWidget* StyleWidget() const;

  /// Swaps the native widget for `replacement`, keeping frame, parent,
  /// z-order, visibility, sensitivity, tooltip, stylesheet and the listening
  /// state. Used when a text field changes between single- and multi-line.
  void ReplaceWidget(GtkWidget* replacement, GtkTextView* replacement_text_view);
  /// (Re)connects the signals that produce ViewEvents on the current widget.
  void HookControl();
  void UnhookControl();
  /// Regenerates and reloads the stylesheet from text_color / font_size /
  /// background_color.
  void ApplyCss();
  /// ImageView: replaces the GtkImage's pixbuf with one scaled proportionally
  /// into `width` x `height` (the unscaled one when either is zero).
  void ScaleImage(int width, int height);
};

/// Disconnects `id` from `instance` if it is still connected, and zeroes it.
inline void DisconnectSignal(gpointer instance, gulong& id) {
  if (instance && id != 0 && g_signal_handler_is_connected(instance, id)) {
    g_signal_handler_disconnect(instance, id);
  }
  id = 0;
}

/// The text of a GtkEntry, or of the buffer behind a GtkTextView.
inline std::string ReadWidgetText(GtkWidget* widget, GtkTextView* text_view) {
  if (text_view) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(text_view);
    if (!buffer) {
      return "";
    }
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    std::string result(text ? text : "");
    g_free(text);
    return result;
  }
  if (widget && GTK_IS_ENTRY(widget)) {
    const gchar* text = gtk_entry_get_text(GTK_ENTRY(widget));
    return text ? text : "";
  }
  return "";
}

inline gfloat ToGtkXAlign(TextAlignment alignment) {
  switch (alignment) {
    case TextAlignment::Center:
      return 0.5f;
    case TextAlignment::End:
      return 1.0f;
    case TextAlignment::Start:
    default:
      return 0.0f;
  }
}

inline TextAlignment FromGtkXAlign(gfloat xalign) {
  if (xalign >= 0.75f) {
    return TextAlignment::End;
  }
  if (xalign >= 0.25f) {
    return TextAlignment::Center;
  }
  return TextAlignment::Start;
}

inline GtkJustification ToGtkJustification(TextAlignment alignment) {
  switch (alignment) {
    case TextAlignment::Center:
      return GTK_JUSTIFY_CENTER;
    case TextAlignment::End:
      return GTK_JUSTIFY_RIGHT;
    case TextAlignment::Start:
    default:
      return GTK_JUSTIFY_LEFT;
  }
}

inline TextAlignment FromGtkJustification(GtkJustification justification) {
  switch (justification) {
    case GTK_JUSTIFY_CENTER:
      return TextAlignment::Center;
    case GTK_JUSTIFY_RIGHT:
      return TextAlignment::End;
    default:
      return TextAlignment::Start;
  }
}

}  // namespace nativeapi
