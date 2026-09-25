#include "../../view.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "../../foundation/id_allocator.h"
#include "../../window.h"
#include "view_internal_linux.h"

namespace nativeapi {

namespace {

using Impl = ViewInternal::Impl;

/// Whether GTK has allocated `widget` yet: a fresh widget carries the
/// placeholder allocation {-1, -1, 1, 1}.
bool IsAllocated(GtkWidget* widget) {
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  return !(allocation.x == -1 && allocation.y == -1 && allocation.width == 1 &&
           allocation.height == 1);
}

/// The position of `child` inside its GtkFixed parent; (0, 0) elsewhere.
void GetFixedPosition(GtkWidget* child, int* x, int* y) {
  *x = 0;
  *y = 0;
  GtkWidget* parent = child ? gtk_widget_get_parent(child) : nullptr;
  if (parent && GTK_IS_FIXED(parent)) {
    gtk_container_child_get(GTK_CONTAINER(parent), child, "x", x, "y", y, nullptr);
  }
}

/// Re-appends `child` to `fixed` so it draws above every other child. GtkFixed
/// paints its children in insertion order and offers no reorder call, so this
/// (remove + put at the same position) is how the z-order is arranged.
void MoveToEnd(GtkWidget* fixed, GtkWidget* child) {
  if (!fixed || !child || gtk_widget_get_parent(child) != fixed) {
    return;
  }
  int x = 0;
  int y = 0;
  GetFixedPosition(child, &x, &y);
  g_object_ref(child);
  gtk_container_remove(GTK_CONTAINER(fixed), child);
  gtk_fixed_put(GTK_FIXED(fixed), child, x, y);
  g_object_unref(child);
}

int ToPixels(double value) {
  return static_cast<int>(std::lround(value));
}

/// "%.2f" independent of the locale: CSS wants a dot.
std::string FormatDouble(double value) {
  gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_formatd(buffer, sizeof(buffer), "%.2f", value);
  return buffer;
}

std::string ToCssRgba(Color color) {
  return "rgba(" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " +
         std::to_string(color.b) + ", " + FormatDouble(color.a / 255.0) + ")";
}

// ---------------------------------------------------------------------------
// Signal handlers
// ---------------------------------------------------------------------------

void OnButtonClicked(GtkButton*, gpointer data) {
  auto* impl = static_cast<Impl*>(data);
  impl->Emit(ButtonClickedEvent(impl->id));
}

void OnEntryChanged(GtkEditable*, gpointer data) {
  auto* impl = static_cast<Impl*>(data);
  auto& platform = *impl->platform;
  if (platform.suppress_changed > 0) {
    return;
  }
  impl->Emit(TextFieldChangedEvent(impl->id, ReadWidgetText(platform.widget, nullptr)));
}

void OnEntryActivate(GtkEntry*, gpointer data) {
  auto* impl = static_cast<Impl*>(data);
  impl->Emit(TextFieldSubmittedEvent(impl->id));
}

void OnBufferChanged(GtkTextBuffer*, gpointer data) {
  auto* impl = static_cast<Impl*>(data);
  auto& platform = *impl->platform;
  if (platform.suppress_changed > 0) {
    return;
  }
  impl->Emit(TextFieldChangedEvent(impl->id, ReadWidgetText(nullptr, platform.text_view)));
}

gboolean OnFocusIn(GtkWidget*, GdkEventFocus*, gpointer data) {
  auto* impl = static_cast<Impl*>(data);
  impl->Emit(ViewFocusedEvent(impl->id));
  return FALSE;  // let GTK's own handler run too
}

gboolean OnFocusOut(GtkWidget*, GdkEventFocus*, gpointer data) {
  auto* impl = static_cast<Impl*>(data);
  impl->Emit(ViewBlurredEvent(impl->id));
  return FALSE;
}

gboolean RelayoutRootWhenIdle(gpointer data) {
  auto* impl = static_cast<Impl*>(data);
  impl->platform->relayout_source_id = 0;
  impl->OnNativeResized();
  return G_SOURCE_REMOVE;
}

/// A root's "size-allocate": lays the tree out again when the size changed.
///
/// Not from inside the handler: GTK is in the middle of an allocation pass
/// there, and the size requests the relayout sets on nested containers (a Row
/// growing with the window) are only queued, not applied, so their children
/// end up outside their parent's old allocation — undrawn and unclickable. An
/// idle callback runs the relayout in a fresh layout cycle. The size
/// comparison stops the allocation that relayout causes from looping.
void OnRootSizeAllocate(GtkWidget*, GtkAllocation* allocation, gpointer data) {
  auto* impl = static_cast<Impl*>(data);
  auto& platform = *impl->platform;
  if (!allocation ||
      (allocation->width == platform.last_width && allocation->height == platform.last_height)) {
    return;
  }
  platform.last_width = allocation->width;
  platform.last_height = allocation->height;
  if (platform.relayout_source_id == 0) {
    platform.relayout_source_id =
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, RelayoutRootWhenIdle, impl, nullptr);
  }
}

/// GtkFixed paints no background of its own; render the one the stylesheet
/// gives it (nothing while the colour is transparent) before the children.
gboolean OnDrawContainerBackground(GtkWidget* widget, cairo_t* cr, gpointer) {
  gtk_render_background(gtk_widget_get_style_context(widget), cr, 0, 0,
                        gtk_widget_get_allocated_width(widget),
                        gtk_widget_get_allocated_height(widget));
  return FALSE;  // the default handler draws the children
}

}  // namespace

// ---------------------------------------------------------------------------
// Platform state
// ---------------------------------------------------------------------------

View::Impl::Platform::Platform(Impl* impl, GtkWidget* widget, bool owned)
    : impl(impl), widget(widget) {
  if (!widget) {
    return;
  }
  if (!owned) {
    // A wrapped widget belongs to someone else: keep it alive while this view
    // refers to it. An owned one arrives with the floating reference already
    // sunk by Adopt() / CreateNativeContainer(), which this Platform takes over.
    g_object_ref(widget);
  }
  // The stylesheet selects the widget by this name. A wrapped widget keeps a
  // name its owner gave it (gtk_widget_get_name() falls back to the type name).
  if (owned || g_strcmp0(gtk_widget_get_name(widget), G_OBJECT_TYPE_NAME(widget)) == 0) {
    gchar* name = g_strdup_printf("nativeapi-view-%llu", static_cast<unsigned long long>(impl->id));
    gtk_widget_set_name(widget, name);
    g_free(name);
  }
  if (GTK_IS_FIXED(widget)) {
    draw_id = g_signal_connect(widget, "draw", G_CALLBACK(OnDrawContainerBackground), this);
  }
}

View::Impl::Platform::~Platform() {
  if (relayout_source_id) {
    g_source_remove(relayout_source_id);
    relayout_source_id = 0;
  }
  UnhookControl();
  if (widget) {
    DisconnectSignal(widget, size_allocate_id);
    DisconnectSignal(widget, draw_id);
  }
  if (css_provider) {
    if (css_widget) {
      gtk_style_context_remove_provider(gtk_widget_get_style_context(css_widget),
                                        GTK_STYLE_PROVIDER(css_provider));
      css_widget = nullptr;
    }
    g_object_unref(css_provider);
    css_provider = nullptr;
  }
  if (pixbuf) {
    g_object_unref(pixbuf);
    pixbuf = nullptr;
  }
  image.reset();
  text_view = nullptr;
  if (widget) {
    g_object_unref(widget);
    widget = nullptr;
  }
}

GtkWidget* View::Impl::Platform::FocusWidget() const {
  return text_view ? GTK_WIDGET(text_view) : widget;
}

GtkWidget* View::Impl::Platform::StyleWidget() const {
  return text_view ? GTK_WIDGET(text_view) : widget;
}

void View::Impl::Platform::HookControl() {
  if (!listening || !widget) {
    return;
  }
  UnhookControl();
  if (GTK_IS_BUTTON(widget)) {
    clicked_id = g_signal_connect(widget, "clicked", G_CALLBACK(OnButtonClicked), impl);
  } else if (GTK_IS_ENTRY(widget)) {
    changed_id = g_signal_connect(widget, "changed", G_CALLBACK(OnEntryChanged), impl);
    activate_id = g_signal_connect(widget, "activate", G_CALLBACK(OnEntryActivate), impl);
  } else if (text_view) {
    hooked_buffer = gtk_text_view_get_buffer(text_view);
    if (hooked_buffer) {
      buffer_changed_id =
          g_signal_connect(hooked_buffer, "changed", G_CALLBACK(OnBufferChanged), impl);
    }
  }
  GtkWidget* focusable = FocusWidget();
  if (focusable && gtk_widget_get_can_focus(focusable)) {
    focus_in_id = g_signal_connect(focusable, "focus-in-event", G_CALLBACK(OnFocusIn), impl);
    focus_out_id = g_signal_connect(focusable, "focus-out-event", G_CALLBACK(OnFocusOut), impl);
  }
}

void View::Impl::Platform::UnhookControl() {
  if (widget) {
    DisconnectSignal(widget, clicked_id);
    DisconnectSignal(widget, changed_id);
    DisconnectSignal(widget, activate_id);
  } else {
    clicked_id = changed_id = activate_id = 0;
  }
  if (hooked_buffer) {
    DisconnectSignal(hooked_buffer, buffer_changed_id);
    hooked_buffer = nullptr;
  } else {
    buffer_changed_id = 0;
  }
  GtkWidget* focusable = FocusWidget();
  if (focusable) {
    DisconnectSignal(focusable, focus_in_id);
    DisconnectSignal(focusable, focus_out_id);
  } else {
    focus_in_id = focus_out_id = 0;
  }
}

void View::Impl::Platform::ApplyCss() {
  GtkWidget* target = StyleWidget();
  if (!target) {
    return;
  }
  if (!css_provider) {
    css_provider = gtk_css_provider_new();
  }
  if (css_widget != target) {
    if (css_widget) {
      gtk_style_context_remove_provider(gtk_widget_get_style_context(css_widget),
                                        GTK_STYLE_PROVIDER(css_provider));
    }
    // A provider added to a widget's own context styles that widget only, so
    // the button label or the text view's "text" node must be selected
    // explicitly where needed. Application priority beats the theme, but a
    // theme rule on a more specific node (or a later theme) may still win.
    gtk_style_context_add_provider(gtk_widget_get_style_context(target),
                                   GTK_STYLE_PROVIDER(css_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    css_widget = target;
  }

  std::string declarations;
  if (text_color.a != 0) {
    declarations += "color: " + ToCssRgba(text_color) + "; ";
  }
  if (font_size > 0.0) {
    declarations += "font-size: " + FormatDouble(font_size) + "pt; ";
  }
  if (background_color.a != 0) {
    // Themes paint buttons and entries with a gradient image; drop it or the
    // colour never shows.
    declarations +=
        "background-color: " + ToCssRgba(background_color) + "; background-image: none; ";
  }
  std::string css;
  if (!declarations.empty()) {
    const gchar* name = gtk_widget_get_name(target);
    const std::string selector = std::string("#") + (name ? name : "");
    // "<name> text" reaches the text node a GtkTextView draws its contents in;
    // it matches nothing on the other controls.
    css = selector + ", " + selector + " text { " + declarations + "}";
  }
  gtk_css_provider_load_from_data(css_provider, css.c_str(), -1, nullptr);
  gtk_widget_queue_resize(target);
}

void View::Impl::Platform::ScaleImage(int width, int height) {
  if (!widget || !GTK_IS_IMAGE(widget)) {
    return;
  }
  if (!pixbuf) {
    gtk_image_clear(GTK_IMAGE(widget));
    scaled_width = 0;
    scaled_height = 0;
    return;
  }
  const int source_width = gdk_pixbuf_get_width(pixbuf);
  const int source_height = gdk_pixbuf_get_height(pixbuf);
  int target_width = source_width;
  int target_height = source_height;
  if (width > 0 && height > 0 && source_width > 0 && source_height > 0) {
    const double scale = std::min(width / static_cast<double>(source_width),
                                  height / static_cast<double>(source_height));
    target_width = std::max(1, static_cast<int>(std::lround(source_width * scale)));
    target_height = std::max(1, static_cast<int>(std::lround(source_height * scale)));
  }
  if (target_width == scaled_width && target_height == scaled_height &&
      gtk_image_get_pixbuf(GTK_IMAGE(widget)) != nullptr) {
    return;
  }
  if (target_width == source_width && target_height == source_height) {
    gtk_image_set_from_pixbuf(GTK_IMAGE(widget), pixbuf);
  } else {
    GdkPixbuf* scaled =
        gdk_pixbuf_scale_simple(pixbuf, target_width, target_height, GDK_INTERP_BILINEAR);
    if (!scaled) {
      return;
    }
    gtk_image_set_from_pixbuf(GTK_IMAGE(widget), scaled);
    g_object_unref(scaled);
  }
  scaled_width = target_width;
  scaled_height = target_height;
}

void View::Impl::Platform::ReplaceWidget(GtkWidget* replacement,
                                         GtkTextView* replacement_text_view) {
  if (!replacement || replacement == widget) {
    return;
  }
  GtkWidget* old = widget;
  const bool had_focus = old && FocusWidget() && gtk_widget_is_focus(FocusWidget());
  UnhookControl();
  if (css_provider && css_widget) {
    gtk_style_context_remove_provider(gtk_widget_get_style_context(css_widget),
                                      GTK_STYLE_PROVIDER(css_provider));
    css_widget = nullptr;
  }
  g_object_ref_sink(replacement);

  if (old) {
    // Carry over what the shared half and the caller set on the old widget.
    int request_width = -1;
    int request_height = -1;
    gtk_widget_get_size_request(old, &request_width, &request_height);
    gtk_widget_set_size_request(replacement, request_width, request_height);
    gtk_widget_set_sensitive(replacement, gtk_widget_get_sensitive(old));
    gtk_widget_set_visible(replacement, gtk_widget_get_visible(old));
    gchar* tooltip = gtk_widget_get_tooltip_text(old);
    gtk_widget_set_tooltip_text(replacement, tooltip);
    g_free(tooltip);
    const gchar* name = gtk_widget_get_name(old);
    gtk_widget_set_name(replacement, name);
    if (replacement_text_view) {
      gtk_widget_set_name(GTK_WIDGET(replacement_text_view), name);
    }

    GtkWidget* parent = gtk_widget_get_parent(old);
    if (parent && GTK_IS_FIXED(parent)) {
      int x = 0;
      int y = 0;
      GetFixedPosition(old, &x, &y);
      gtk_fixed_put(GTK_FIXED(parent), replacement, x, y);
      // Keep the z-order: whatever drew above the old widget goes back above
      // the replacement, which gtk_fixed_put has just appended.
      GList* children = gtk_container_get_children(GTK_CONTAINER(parent));
      bool after_old = false;
      for (GList* it = children; it; it = it->next) {
        GtkWidget* sibling = GTK_WIDGET(it->data);
        if (sibling == old) {
          after_old = true;
        } else if (after_old && sibling != replacement) {
          MoveToEnd(parent, sibling);
        }
      }
      g_list_free(children);
      gtk_container_remove(GTK_CONTAINER(parent), old);
    }
  }

  widget = replacement;
  text_view = replacement_text_view;
  impl->native = replacement;
  if (old) {
    g_object_unref(old);
  }
  if (css_provider) {
    ApplyCss();
  }
  if (had_focus) {
    GtkWidget* focusable = FocusWidget();
    if (focusable && gtk_widget_get_can_focus(focusable)) {
      gtk_widget_grab_focus(focusable);
    }
  }
  HookControl();
}

// ---------------------------------------------------------------------------
// Impl: construction and the platform seam
// ---------------------------------------------------------------------------

View::Impl::Impl(View* owner, void* native, bool owned)
    : owner(owner), native(native), owned(owned), id(IdAllocator::Allocate<View>()) {
  if (!this->native && owned) {
    this->native = CreateNativeContainer();
  }
  GtkWidget* widget = nullptr;
  if (this->native && GTK_IS_WIDGET(this->native)) {
    widget = static_cast<GtkWidget*>(this->native);
  } else {
    this->native = nullptr;
  }
  platform = std::make_unique<Platform>(this, widget, owned);
}

View::Impl::~Impl() {
  if (platform && platform->widget && owned) {
    // Subviews that outlive this view must keep a usable widget: detach them
    // before the container goes, since a GtkContainer destroys whatever it
    // still holds when it is finalized.
    for (const auto& subview : subviews) {
      if (subview && subview->pimpl_) {
        RemoveNativeSubview(*subview->pimpl_);
      }
    }
    GtkWidget* parent = gtk_widget_get_parent(platform->widget);
    if (parent) {
      gtk_container_remove(GTK_CONTAINER(parent), platform->widget);
    }
  }
  platform.reset();
  native = nullptr;
}

void* View::Impl::CreateNativeContainer() {
  GtkWidget* fixed = gtk_fixed_new();
  if (!fixed) {
    return nullptr;
  }
  g_object_ref_sink(fixed);
  gtk_widget_show(fixed);
  return fixed;
}

void View::Impl::SetNativeFrame(Rectangle frame) {
  GtkWidget* widget = platform->widget;
  if (!widget || is_root) {
    return;
  }
  const int width = std::max(0, ToPixels(frame.width));
  const int height = std::max(0, ToPixels(frame.height));
  GtkWidget* parent = gtk_widget_get_parent(widget);
  if (parent && GTK_IS_FIXED(parent)) {
    gtk_fixed_move(GTK_FIXED(parent), widget, ToPixels(frame.x), ToPixels(frame.y));
  }
  // GtkFixed allocates each child its minimum size, and the size request is
  // the only public way to raise that minimum. A control cannot go below its
  // own minimum, so a frame smaller than that is widened by GTK.
  gtk_widget_set_size_request(widget, width, height);
  if (GTK_IS_IMAGE(widget)) {
    platform->ScaleImage(width, height);
  }
}

Rectangle View::Impl::GetNativeFrame() const {
  GtkWidget* widget = platform->widget;
  if (!widget) {
    return Rectangle{0, 0, 0, 0};
  }
  if (is_root) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width;
    int height = allocation.height;
    if (!IsAllocated(widget)) {
      // Not laid out yet: the window's requested size is the best guess.
      width = 0;
      height = 0;
      GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
      if (toplevel && GTK_IS_WINDOW(toplevel)) {
        gtk_window_get_size(GTK_WINDOW(toplevel), &width, &height);
      }
    }
    return Rectangle{0, 0, static_cast<double>(width), static_cast<double>(height)};
  }
  int x = 0;
  int y = 0;
  GetFixedPosition(widget, &x, &y);
  int width = -1;
  int height = -1;
  gtk_widget_get_size_request(widget, &width, &height);
  if (width < 0 || height < 0) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    const bool allocated = IsAllocated(widget);
    if (width < 0) {
      width = allocated ? allocation.width : 0;
    }
    if (height < 0) {
      height = allocated ? allocation.height : 0;
    }
  }
  return Rectangle{static_cast<double>(x), static_cast<double>(y), static_cast<double>(width),
                   static_cast<double>(height)};
}

Size View::Impl::GetNativeIntrinsicSize() const {
  GtkWidget* widget = platform->widget;
  if (!widget || GTK_IS_FIXED(widget)) {
    return Size{0, 0};
  }
  if (GTK_IS_IMAGE(widget)) {
    // The unscaled image, not the copy scaled into the current frame.
    if (!platform->pixbuf) {
      return Size{0, 0};
    }
    return Size{static_cast<double>(gdk_pixbuf_get_width(platform->pixbuf)),
                static_cast<double>(gdk_pixbuf_get_height(platform->pixbuf))};
  }
  // The size request set by SetNativeFrame() is folded into every preferred
  // size GTK reports; lift it for the measurement so a frame given earlier
  // does not pass for the control's own wish.
  int request_width = -1;
  int request_height = -1;
  gtk_widget_get_size_request(widget, &request_width, &request_height);
  const bool has_request = request_width >= 0 || request_height >= 0;
  if (has_request) {
    gtk_widget_set_size_request(widget, -1, -1);
  }
  GtkRequisition minimum;
  GtkRequisition natural;
  gtk_widget_get_preferred_size(widget, &minimum, &natural);
  if (has_request) {
    gtk_widget_set_size_request(widget, request_width, request_height);
  }
  return Size{static_cast<double>(std::max(0, natural.width)),
              static_cast<double>(std::max(0, natural.height))};
}

void View::Impl::AddNativeSubview(Impl& child, size_t index) {
  GtkWidget* widget = platform->widget;
  GtkWidget* child_widget = child.platform ? child.platform->widget : nullptr;
  if (!widget || !child_widget || !GTK_IS_FIXED(widget)) {
    return;
  }
  GtkWidget* old_parent = gtk_widget_get_parent(child_widget);
  if (old_parent && old_parent != widget) {
    // The child's Platform holds its own reference, so the removal cannot
    // finalize it.
    gtk_container_remove(GTK_CONTAINER(old_parent), child_widget);
  }
  if (gtk_widget_get_parent(child_widget) != widget) {
    // Position and size follow in the relayout the shared half runs next.
    gtk_fixed_put(GTK_FIXED(widget), child_widget, 0, 0);
  }
  // gtk_fixed_put appends: the subviews meant to stay above the new one are
  // re-appended in order so the paint order matches the subview list.
  for (size_t i = index + 1; i < subviews.size(); ++i) {
    const auto& above = subviews[i];
    if (above && above->pimpl_ && above->pimpl_->platform) {
      MoveToEnd(widget, above->pimpl_->platform->widget);
    }
  }
}

void View::Impl::RemoveNativeSubview(Impl& child) {
  GtkWidget* widget = platform->widget;
  GtkWidget* child_widget = child.platform ? child.platform->widget : nullptr;
  if (!widget || !child_widget || gtk_widget_get_parent(child_widget) != widget) {
    return;
  }
  // The child's Platform keeps its reference: the widget survives for reuse.
  gtk_container_remove(GTK_CONTAINER(widget), child_widget);
}

void View::Impl::SetNativeVisible(bool is_visible) {
  if (platform->widget) {
    gtk_widget_set_visible(platform->widget, is_visible ? TRUE : FALSE);
  }
}

void View::Impl::SetNativeEnabled(bool is_enabled) {
  if (platform->widget) {
    gtk_widget_set_sensitive(platform->widget, is_enabled ? TRUE : FALSE);
  }
}

bool View::Impl::IsNativeEnabled() const {
  if (!platform->widget) {
    return true;
  }
  return gtk_widget_get_sensitive(platform->widget) == TRUE;
}

void View::Impl::SetNativeBackgroundColor(Color color) {
  if (!platform->widget) {
    return;
  }
  platform->background_color = color;
  platform->ApplyCss();
  gtk_widget_queue_draw(platform->widget);
}

void View::Impl::SetNativeTooltip(const std::optional<std::string>& tooltip) {
  if (platform->widget) {
    gtk_widget_set_tooltip_text(platform->widget, tooltip ? tooltip->c_str() : nullptr);
  }
}

void View::Impl::NativeFocus() {
  GtkWidget* focusable = platform->FocusWidget();
  if (focusable && gtk_widget_get_can_focus(focusable)) {
    gtk_widget_grab_focus(focusable);
  }
}

void View::Impl::NativeBlur() {
  GtkWidget* focusable = platform->FocusWidget();
  if (!focusable || !IsNativeFocused()) {
    return;
  }
  GtkWidget* toplevel = gtk_widget_get_toplevel(focusable);
  if (toplevel && GTK_IS_WINDOW(toplevel)) {
    gtk_window_set_focus(GTK_WINDOW(toplevel), nullptr);
  }
}

bool View::Impl::IsNativeFocused() const {
  GtkWidget* focusable = platform->FocusWidget();
  // is_focus: the toplevel's focus widget, whether or not the window is
  // active; has_focus would also demand an active window.
  return focusable && gtk_widget_is_focus(focusable) == TRUE;
}

void View::Impl::StartNativeListening() {
  platform->listening = true;
  platform->HookControl();
}

void View::Impl::StopNativeListening() {
  platform->listening = false;
  platform->UnhookControl();
}

void View::Impl::ObserveNativeResize(bool observe) {
  GtkWidget* widget = platform->widget;
  if (!widget) {
    return;
  }
  if (observe && platform->size_allocate_id == 0) {
    platform->last_width = -1;
    platform->last_height = -1;
    // After GtkFixed has placed the children, so a relayout reads fresh
    // allocations.
    platform->size_allocate_id =
        g_signal_connect_after(widget, "size-allocate", G_CALLBACK(OnRootSizeAllocate), this);
  } else if (!observe) {
    DisconnectSignal(widget, platform->size_allocate_id);
    if (platform->relayout_source_id) {
      g_source_remove(platform->relayout_source_id);
      platform->relayout_source_id = 0;
    }
  }
}

bool View::IsSupported() {
  return true;
}

// ---------------------------------------------------------------------------
// Window::GetContentView
// ---------------------------------------------------------------------------

namespace {

/// Set on the GtkOverlay this library installs to point at its GtkFixed.
const char* kContentFixedKey = "nativeapi-content-fixed";

}  // namespace

// The root view is a GtkFixed that fills the window. A GtkWindow is a GtkBin
// with a single child, and what that child is depends on who made the window:
//
// - A window this library created has no child: a GtkFixed is added as the
//   content and the root view wraps it.
// - The child is already a GtkFixed: it is wrapped as is (the case above on a
//   second call, or a host that chose a GtkFixed itself).
// - The child is a host framework's widget (a Flutter FlView, say): it is moved
//   into a GtkOverlay as the main child and a pass-through GtkFixed is laid over
//   it, so native controls sit above the host content while the empty parts of
//   the overlay still deliver input to it. The overlay remembers its GtkFixed,
//   so every later call finds the same one. Moving the host widget re-parents
//   it once (it is unrealized and realized again); hosts that cannot survive
//   that should hand the window a GtkFixed themselves.
std::shared_ptr<View> Window::GetContentView() const {
  void* native = GetNativeObject();
  if (!native || !GTK_IS_WINDOW(native) || !View::IsSupported()) {
    return nullptr;
  }
  GtkWidget* window = GTK_WIDGET(native);
  GtkWidget* child = gtk_bin_get_child(GTK_BIN(window));
  GtkWidget* fixed = nullptr;
  if (!child) {
    fixed = gtk_fixed_new();
    if (!fixed) {
      return nullptr;
    }
    gtk_container_add(GTK_CONTAINER(window), fixed);
    gtk_widget_show(fixed);
  } else if (GTK_IS_FIXED(child)) {
    fixed = child;
  } else if (gpointer marked = g_object_get_data(G_OBJECT(child), kContentFixedKey)) {
    fixed = GTK_WIDGET(marked);
  } else {
    GtkWidget* overlay = gtk_overlay_new();
    fixed = gtk_fixed_new();
    if (!overlay || !fixed) {
      return nullptr;
    }
    g_object_ref(child);
    gtk_container_remove(GTK_CONTAINER(window), child);
    gtk_container_add(GTK_CONTAINER(overlay), child);
    g_object_unref(child);
    gtk_widget_set_halign(fixed, GTK_ALIGN_FILL);
    gtk_widget_set_valign(fixed, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), fixed);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), fixed, TRUE);
    g_object_set_data(G_OBJECT(overlay), kContentFixedKey, fixed);
    gtk_widget_show(fixed);
    gtk_widget_show(overlay);
    gtk_container_add(GTK_CONTAINER(window), overlay);
  }
  return ContentViewFor(fixed);
}

}  // namespace nativeapi
