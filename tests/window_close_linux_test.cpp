// GTK delete-event with existing host handlers, in a private display session.
#include <gtk/gtk.h>
#include <iostream>
#include <thread>
#include "nativeapi.h"
namespace {
int failures = 0, host_close = 0, preblocked_calls = 0;
bool refuse = true;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
gboolean Host(GtkWidget*, GdkEvent*, gpointer) {
  ++host_close;
  return refuse;
}
gboolean Preblocked(GtkWidget*, GdkEvent*, gpointer) {
  ++preblocked_calls;
  return FALSE;
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
  // Realize without showing: this is still a real GdkWindow for either backend.
  gtk_widget_realize(widget);
  g_signal_connect(widget, "delete-event", G_CALLBACK(Host), nullptr);
  auto blocked = g_signal_connect(widget, "delete-event", G_CALLBACK(Preblocked), nullptr);
  g_signal_handler_block(widget, blocked);
  nativeapi::Window first(widget), alias(widget);
  auto ui = std::this_thread::get_id();
  enum Mode { Veto, Defer, Accept } mode = Veto;
  int requested = 0, required = 0;
  std::shared_ptr<nativeapi::EventRequest> request;
  std::shared_ptr<nativeapi::EventDecision> a, b;
  auto listener = first.AddListener<nativeapi::WindowCloseRequestedEvent>([&](const auto& event) {
    Check(std::this_thread::get_id() == ui, "GTK callback is on its main context thread");
    request = event.GetRequest();
    if (!request->IsCancelable()) {
      ++required;
      request->Cancel();
      return;
    }
    ++requested;
    if (mode == Veto)
      request->Cancel();
    if (mode == Defer)
      a = request->Defer();
  });
  auto other = alias.AddListener<nativeapi::WindowCloseRequestedEvent>([&](const auto& event) {
    Check(event.GetRequest() == request, "aliases share the GTK request");
    if (mode == Defer && event.GetRequest()->IsCancelable())
      b = event.GetRequest()->Defer();
  });
  Drain();
  Check(first.Close(), "GTK public close submitted");
  Drain();
  Check(requested == 1 && host_close == 0, "veto precedes previously installed host handlers");
  gtk_window_close(GTK_WINDOW(widget));
  Drain();
  Check(requested == 2 && host_close == 0, "native GTK close uses the same gate");
  mode = Defer;
  first.Close();
  alias.Close();
  Drain();
  Check(requested == 3, "GTK pending close coalesces");
  a->Accept();
  std::thread worker([&] { b->Accept(); });
  worker.join();
  Check(host_close == 0, "worker approval waits for the GTK context");
  Drain();
  Check(host_close == 1 && !gtk_widget_in_destruction(widget), "approval preserves host refusal");
  Check(preblocked_calls == 0, "host's existing block count is preserved");
  std::thread close_worker([&] { Check(first.Close(), "foreign-thread close submitted"); });
  close_worker.join();
  Drain();
  auto late_a = a, late_b = b;
  gtk_widget_destroy(widget);
  late_a->Accept();
  late_b->Accept();
  Drain();
  Check(required == 1 && host_close == 1, "native destroy proceeds and fences old decisions");
  Check(first.Close(), "queued close reports submission before UI validity check");
  Drain();
  Check(host_close == 1, "destroyed GTK target receives no host replay");
  first.RemoveListener(listener);
  alias.RemoveListener(other);
  Drain();
  g_object_unref(widget);
  return failures ? 1 : 0;
}
