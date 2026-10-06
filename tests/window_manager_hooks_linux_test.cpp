// GTK integration regression: windows may outlive the manager at process exit.
#include "nativeapi.h"
#include <gtk/gtk.h>
#include <cstdlib>
#include <iostream>

namespace {
GtkWidget* surviving_widget = nullptr;
int calls = 0;

void AfterManagerShutdown() {
  const int before = calls;
  // These signals must no longer reach the already-destroyed singleton. Exercise
  // both the global show/hide hooks and the per-widget map/unmap fallback.
  gtk_widget_show(surviving_widget);
  gtk_widget_hide(surviving_widget);
  GdkEvent* event = gdk_event_new(GDK_MAP);
  gboolean handled = FALSE;
  g_signal_emit_by_name(surviving_widget, "map-event", event, &handled);
  event->type = GDK_UNMAP;
  g_signal_emit_by_name(surviving_widget, "unmap-event", event, &handled);
  gdk_event_free(event);
  gtk_widget_destroy(surviving_widget);
  g_object_unref(surviving_widget);
  if (calls != before) {
    std::cerr << "FAIL GTK callbacks survived manager shutdown\n";
    std::_Exit(1);
  }
  std::cout << "PASS GTK callbacks removed before manager destruction\n";
}
}  // namespace

int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) return 77;
  surviving_widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_object_ref_sink(surviving_widget);
  gtk_widget_realize(surviving_widget);
  // Registered first, so this runs after the singleton's destructor.
  std::atexit(AfterManagerShutdown);
  auto& manager = nativeapi::WindowManager::GetInstance();
  manager.SetWillShowHook([](nativeapi::WindowId) { ++calls; });
  manager.SetWillHideHook([](nativeapi::WindowId) { ++calls; });
  const auto listener = manager.AddListener<nativeapi::WindowFocusedEvent>([](const auto&) {});
  manager.RemoveListener(listener);
  // Removing the last event listener must keep explicit will-show/hide hooks.
  gtk_widget_show(surviving_widget);
  gtk_widget_hide(surviving_widget);
  if (calls < 2) {
    std::cerr << "FAIL will-show/hide hooks lost when event listening stopped\n";
    return 1;
  }
  std::cout << "PASS will-show/hide hooks independent of event listeners\n";
  // Include windows destroyed before the manager, then reinstall event hooks.
  auto* temporary = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  const auto other_listener = manager.AddListener<nativeapi::WindowFocusedEvent>([](const auto&) {});
  gtk_widget_destroy(temporary);
  manager.RemoveListener(other_listener);
  return 0;
}
