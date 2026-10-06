#include <gtk/gtk.h>
#include <new>
#include "../../window_close_dispatch.h"

namespace nativeapi::detail {
namespace {
constexpr char kPolicy[] = "NativeAPIWindowClosePolicy";
constexpr char kId[] = "NativeAPIWindowId";
struct Policy : std::enable_shared_from_this<Policy> {
  GtkWidget* widget = nullptr;
  WindowId id;
  std::shared_ptr<WindowCloseState> state;
  std::atomic<bool> destroyed{false};
  gulong gate = 0;
  bool bypass = false;
  GSList* blocked = nullptr;
  bool Valid() const {
    return !destroyed.load() && widget && state->IsAlive() && !gtk_widget_in_destruction(widget) &&
           gtk_widget_get_realized(widget) && gtk_widget_get_window(widget) &&
           !gdk_window_is_destroyed(gtk_widget_get_window(widget)) &&
           reinterpret_cast<uintptr_t>(g_object_get_data(G_OBJECT(widget), kId)) == id;
  }
  void Restore(GArray* frame) {
    if (widget)
      for (guint index = 0; index < frame->len; ++index) {
        auto handler = g_array_index(frame, gulong, index);
        if (handler != gate && g_signal_handler_is_connected(widget, handler))
          g_signal_handler_unblock(widget, handler);
      }
    g_array_unref(frame);
  }
  void RestoreAll() {
    while (blocked) {
      auto* frame = static_cast<GArray*>(blocked->data);
      blocked = g_slist_delete_link(blocked, blocked);
      Restore(frame);
    }
  }
  ~Policy() {
    RestoreAll();
    if (state)
      state->Invalidate();
    if (widget)
      g_object_remove_weak_pointer(G_OBJECT(widget), reinterpret_cast<gpointer*>(&widget));
  }
  bool Post(std::function<void()> work) {
    if (destroyed.load())
      return false;
    // Always leave the original GTK emission before replaying an approval.
    // Nested emissions must finish restoring the host's handlers first.
    auto* callback = new std::function<void()>(std::move(work));
    auto source = g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer data) -> gboolean {
          try {
            (*static_cast<std::function<void()>*>(data))();
          } catch (...) {
          }
          return G_SOURCE_REMOVE;
        },
        callback, [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
    return source != 0;
  }
};
using Box = std::shared_ptr<Policy>;
std::shared_ptr<Policy> Lookup(GtkWidget* widget) {
  auto* box = static_cast<Box*>(g_object_get_data(G_OBJECT(widget), kPolicy));
  return box ? *box : nullptr;
}
gboolean Gate(GtkWidget* widget, GdkEvent* event, gpointer data) {
  auto policy = static_cast<Policy*>(data)->shared_from_this();
  if (policy->bypass)
    return FALSE;
  if (!policy->blocked)
    return FALSE;
  auto* frame = static_cast<GArray*>(policy->blocked->data);
  policy->blocked = g_slist_delete_link(policy->blocked, policy->blocked);
  policy->Restore(frame);
  if (!policy->Valid())
    return TRUE;
  try {
    auto payload = std::shared_ptr<GdkEvent>(gdk_event_copy(event), &gdk_event_free);
    std::weak_ptr<Policy> weak = policy;
    (void)policy->state->Request([weak, payload] {
      auto policy = weak.lock();
      if (!policy || !policy->Valid())
        return;
      auto keep = std::shared_ptr<GtkWidget>(GTK_WIDGET(g_object_ref(policy->widget)),
                                             [](GtkWidget* target) { g_object_unref(target); });
      const bool previous = policy->bypass;
      policy->bypass = true;
      gboolean handled = FALSE;
      g_signal_emit_by_name(keep.get(), "delete-event", payload.get(), &handled);
      policy->bypass = previous;
      if (!handled && policy->Valid())
        gtk_widget_destroy(keep.get());
    });
  } catch (...) {
  }  // A failed request is swallowed, never silently approved.
  return TRUE;
}
gboolean BeforeDelete(GSignalInvocationHint* hint, guint count, const GValue* values, gpointer) {
  if (!count)
    return TRUE;
  auto* widget = GTK_WIDGET(g_value_get_object(&values[0]));
  auto policy = Lookup(widget);
  if (!policy || policy->bypass || !policy->Valid())
    return TRUE;
  try {
    if (!policy->state->HasObservers() && !policy->state->IsPending())
      return TRUE;
  } catch (...) {
  }  // Allocation failure must still route through the gate.
  // GLib explicitly forbids stop_emission from an emission hook. Temporarily
  // block only currently unblocked instance handlers, leaving our normal
  // handler first. It restores them before emitting user callbacks, and its
  // TRUE return stops GTK's handled accumulator. Replay preserves host order,
  // class closure, return value and pre-existing block counts.
  auto* frame = g_array_new(FALSE, FALSE, sizeof(gulong));
  for (;;) {
    auto handler = g_signal_handler_find(
        widget, static_cast<GSignalMatchType>(G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_UNBLOCKED),
        hint->signal_id, 0, nullptr, nullptr, nullptr);
    if (!handler)
      break;
    g_array_append_val(frame, handler);
    g_signal_handler_block(widget, handler);
  }
  bool gate_blocked_here = false;
  for (guint index = 0; index < frame->len; ++index)
    gate_blocked_here |= g_array_index(frame, gulong, index) == policy->gate;
  if (!gate_blocked_here) {
    policy->Restore(frame);
    return TRUE;
  }
  g_signal_handler_unblock(widget, policy->gate);
  policy->blocked = g_slist_prepend(policy->blocked, frame);
  // Roll back even if another hook removes or blocks the gate during emission.
  auto* weak = static_cast<std::weak_ptr<Policy>*>(g_malloc(sizeof(std::weak_ptr<Policy>)));
  new (weak) std::weak_ptr<Policy>(policy);
  g_idle_add_full(
      G_PRIORITY_DEFAULT_IDLE,
      [](gpointer data) -> gboolean {
        if (auto policy = static_cast<std::weak_ptr<Policy>*>(data)->lock())
          policy->RestoreAll();
        return G_SOURCE_REMOVE;
      },
      weak,
      [](gpointer data) {
        static_cast<std::weak_ptr<Policy>*>(data)->~weak_ptr();
        g_free(data);
      });
  return TRUE;
}
void Destroyed(GtkWidget*, gpointer data) {
  auto policy = static_cast<Policy*>(data)->shared_from_this();
  policy->destroyed = true;
  policy->RestoreAll();
  if (!policy->bypass)
    policy->state->NotifyRequired();
  policy->state->Invalidate();
}
std::shared_ptr<Policy> GetPolicy(void* native, WindowId id) {
  if (!native || !GTK_IS_WINDOW(native))
    return nullptr;
  auto* widget = GTK_WIDGET(native);
  if (gtk_widget_in_destruction(widget) ||
      reinterpret_cast<uintptr_t>(g_object_get_data(G_OBJECT(widget), kId)) != id)
    return nullptr;
  if (auto policy = Lookup(widget))
    return policy->Valid() ? policy : nullptr;
  static gulong hook = g_signal_add_emission_hook(g_signal_lookup("delete-event", GTK_TYPE_WIDGET),
                                                  0, BeforeDelete, nullptr, nullptr);
  if (!hook)
    return nullptr;
  auto policy = std::make_shared<Policy>();
  policy->widget = widget;
  policy->id = id;
  g_object_add_weak_pointer(G_OBJECT(widget), reinterpret_cast<gpointer*>(&policy->widget));
  std::weak_ptr<Policy> weak = policy;
  policy->state = WindowCloseState::Create(
      id,
      [weak](auto work) {
        if (auto policy = weak.lock())
          return policy->Post(std::move(work));
        return false;
      },
      [weak] {
        auto policy = weak.lock();
        return policy && policy->Valid();
      });
  g_object_set_data_full(G_OBJECT(widget), kPolicy, new Box(policy),
                         [](gpointer data) { delete static_cast<Box*>(data); });
  policy->gate = g_signal_connect(widget, "delete-event", G_CALLBACK(Gate), policy.get());
  g_signal_connect(widget, "destroy", G_CALLBACK(Destroyed), policy.get());
  return policy;
}
}  // namespace
std::shared_ptr<WindowCloseState> GetNativeWindowCloseState(void* window, WindowId id) {
  auto policy = GetPolicy(window, id);
  return policy ? policy->state : nullptr;
}
bool RequestNativeWindowClose(void* window, WindowId id) {
  auto policy = GetPolicy(window, id);
  if (!policy || !policy->Valid())
    return false;
  gtk_window_close(GTK_WINDOW(policy->widget));
  return true;
}
bool IsNativeWindowCloseSupported() {
  return true;
}
}  // namespace nativeapi::detail

namespace nativeapi {
bool Window::DispatchWindowTask(std::function<void()> task) {
  // The default GTK context owns every native widget. Do not infer its owner
  // from whichever FFI thread first initialized the common dispatcher.
  auto* work = new std::function<void()>(std::move(task));
  return g_idle_add_full(
             G_PRIORITY_DEFAULT,
             [](gpointer data) -> gboolean {
               try {
                 (*static_cast<std::function<void()>*>(data))();
               } catch (...) {
               }
               return G_SOURCE_REMOVE;
             },
             work, [](gpointer data) { delete static_cast<std::function<void()>*>(data); }) != 0;
}
}  // namespace nativeapi
