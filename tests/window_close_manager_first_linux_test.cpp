// A window that WindowManager sees before any Window wraps it - the way a
// Flutter app asks for its window with GetCurrent() - still gets its own
// events and cancellable close requests. WindowManager keys windows by their
// GdkWindow; the close gate checks the ID on the GtkWindow, so the two must
// carry the same one. Private display, no input.
#include <gtk/gtk.h>
#include <iostream>
#include "nativeapi.h"
namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Drain() {
  while (g_main_context_pending(nullptr))
    g_main_context_iteration(nullptr, FALSE);
}
}  // namespace
int main() {
  if (!gtk_init_check(nullptr, nullptr))
    return 77;
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_object_ref_sink(widget);
  gtk_widget_realize(widget);  // A real GdkWindow, not shown yet (as a Flutter runner's at start).

  auto current = nativeapi::WindowManager::GetInstance().GetCurrent();
  Check(current && current->GetNativeObject() == widget, "GetCurrent finds the window");
  if (!current) return 1;
  nativeapi::Window wrapper(widget);
  Check(wrapper.GetId() == current->GetId(), "a wrapper of the widget has the same ID");

  int requested = 0;
  current->AddListener<nativeapi::WindowCloseRequestedEvent>([&](const auto& event) {
    ++requested;
    event.GetRequest()->Cancel();
  });
  Drain();  // The subscription is set up on the next main loop turn.
  gtk_window_close(GTK_WINDOW(widget));
  Drain();
  Check(requested == 1, "the close request reaches the window found through WindowManager");
  Check(!gtk_widget_in_destruction(widget), "and cancelling it keeps the window");

  int titles = 0;
  current->AddListener<nativeapi::WindowPropertyChangedEvent>([&](const auto& event) {
    if (event.GetProperty() == nativeapi::WindowProperty::Title) ++titles;
  });
  Drain();
  gtk_window_set_title(GTK_WINDOW(widget), "renamed");
  Drain();
  Check(titles == 1, "its own property events arrive too");

  gtk_widget_destroy(widget);
  g_object_unref(widget);
  std::cout << (failures ? "FAILED" : "OK") << std::endl;
  return failures ? 1 : 0;
}
