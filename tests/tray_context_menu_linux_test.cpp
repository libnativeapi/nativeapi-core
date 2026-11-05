// Run in a private X11/Wayland display; no synthetic input is sent.
#include "nativeapi.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#undef None
#include <cstdlib>
#include <chrono>
#include <thread>
#include <iostream>
using namespace nativeapi;
static void Check(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  if (!ok) std::exit(1);
}
static GVariant* Call(TrayIcon& tray, const char* path, const char* interface,
                      const char* method, GVariant* parameters) {
  auto* client = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  auto* connection = static_cast<GDBusConnection*>(tray.GetNativeObject());
  struct Result { bool done = false; GVariant* reply = nullptr; } result;
  g_dbus_connection_call(client, g_dbus_connection_get_unique_name(connection), path,
      interface, method, parameters, nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr,
      [](GObject* source, GAsyncResult* async, gpointer data) {
        auto* result = static_cast<Result*>(data);
        result->reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), async, nullptr);
        result->done = true;
      }, &result);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  while (!result.done && std::chrono::steady_clock::now() < deadline) {
    g_main_context_iteration(nullptr, FALSE);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  Check(result.done && result.reply, "D-Bus call completed");
  g_object_unref(client);
  return result.reply;
}
static void CheckExport(TrayIcon& tray, ContextMenuTrigger trigger, bool exposed, bool item_is_menu) {
  tray.SetContextMenuTrigger(trigger);
  for (const char* name : {"Menu", "ItemIsMenu"}) {
    auto* reply = Call(tray, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "Get",
                       g_variant_new("(ss)", "org.kde.StatusNotifierItem", name));
    GVariant* value = nullptr;
    g_variant_get(reply, "(v)", &value);
    Check(std::string(name) == "Menu"
              ? std::string(g_variant_get_string(value, nullptr)) ==
                    (exposed ? "/StatusNotifierItem/Menu" : "/NO_DBUSMENU")
              : bool(g_variant_get_boolean(value)) == item_is_menu,
          "trigger preserves host menu policy");
    g_variant_unref(value);
    g_variant_unref(reply);
  }
}
int main(int argc, char** argv) {
  if (!g_getenv("NATIVEAPI_TEST_PRIVATE_DISPLAY")) return 77;
  if (!gtk_init_check(&argc, &argv)) return 77;
  auto tray = std::make_unique<TrayIcon>();
  Check(!tray->OpenContextMenu(), "no menu returns false");
  auto menu = std::make_shared<Menu>();
  auto item = std::make_shared<MenuItem>("Item", MenuItemType::Normal);
  menu->AddItem(item);
  auto* widget = static_cast<GtkWidget*>(menu->GetNativeObject());
  int opened = 0, closed = 0, clicked = 0;
  menu->AddListener<MenuOpenedEvent>([&](const auto&) { ++opened; });
  menu->AddListener<MenuClosedEvent>([&](const auto&) { ++closed; });
  item->AddListener<MenuItemClickedEvent>([&](const auto&) { ++clicked; });
  tray->SetContextMenu(menu);
  CheckExport(*tray, ContextMenuTrigger::None, false, false);
  CheckExport(*tray, ContextMenuTrigger::DoubleClicked, false, false);
  CheckExport(*tray, ContextMenuTrigger::Clicked, true, true);
  CheckExport(*tray, ContextMenuTrigger::RightClicked, true, false);
  // Host-rendered menu events retain their existing delivery and close limitation.
  auto host_event = [&](const char* event) {
    g_variant_unref(Call(*tray, "/StatusNotifierItem/Menu", "com.canonical.dbusmenu", "Event",
                        g_variant_new("(isvu)", 0, event, g_variant_new_int32(0), 0u)));
  };
  host_event("opened");
  Check(opened == 1 && !tray->CloseContextMenu(), "host menu reports open but cannot be dismissed via SNI");
  host_event("closed");
  Check(closed == 1 && tray->CloseContextMenu(), "host menu closed event retained");
  opened = closed = 0;
  tray->SetContextMenuTrigger(ContextMenuTrigger::None);
  if (!GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
    Check(!tray->OpenContextMenu(), "Wayland reports unsupported");
    Check(!gtk_widget_get_mapped(widget) && opened == 0, "Wayland has no phantom popup/event");
    Check(tray->CloseContextMenu(), "nothing to close");
    return 0;
  }
  // Deliberately before gtk_main: a D-Bus invocation has no current GdkEvent.
  Check(tray->OpenContextMenu(), "manual None popup before main loop");
  Check(gtk_widget_get_mapped(widget) && opened == 1, "actual map emits opened once");
  Check(tray->OpenContextMenu() && opened == 1, "repeated open is idempotent");
  gtk_menu_item_activate(GTK_MENU_ITEM(item->GetNativeObject()));
  Check(clicked == 1, "local item activation reaches API event");
  Check(tray->CloseContextMenu(), "close succeeds");
  Check(!gtk_widget_get_mapped(widget) && closed == 1, "actual unmap emits closed once");
  Check(tray->CloseContextMenu() && closed == 1, "repeated close is idempotent");
  tray->SetContextMenuTrigger(ContextMenuTrigger::DoubleClicked);
  int doubles = 0;
  tray->AddListener<TrayIconDoubleClickedEvent>([&](const auto&) { ++doubles; });
  for (int i = 0; i < 2; ++i) {
    g_variant_unref(Call(*tray, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "Activate",
                        g_variant_new("(ii)", 0, 0)));
  }
  Check(doubles == 1 && gtk_widget_get_mapped(widget), "double activation automatically opens menu");
  auto replacement = std::make_shared<Menu>();
  replacement->AddItem(std::make_shared<MenuItem>("Replacement", MenuItemType::Normal));
  tray->SetContextMenu(replacement);
  Check(!gtk_widget_get_mapped(widget) && closed == 2, "replacing menu closes old popup");
  Check(tray->OpenContextMenu(), "replacement opens");
  auto* replacement_widget = static_cast<GtkWidget*>(replacement->GetNativeObject());
  tray.reset();
  Check(!gtk_widget_get_mapped(replacement_widget), "tray destruction closes retained menu");
  tray = std::make_unique<TrayIcon>();
  tray->SetContextMenu(menu);
  auto listener = menu->AddListener<MenuOpenedEvent>([&](const auto&) { tray->SetContextMenu(nullptr); });
  Check(!tray->OpenContextMenu(), "opened listener may remove menu synchronously");
  Check(!gtk_widget_get_mapped(widget), "removed menu stays closed");
  menu->RemoveListener(listener);
  tray->SetContextMenu(menu);
  tray->SetContextMenuTrigger(ContextMenuTrigger::DoubleClicked);
  tray->AddListener<TrayIconDoubleClickedEvent>([&](const auto&) { tray.reset(); });
  for (int i = 0; i < 2; ++i) {
    g_variant_unref(Call(*tray, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "Activate",
                        g_variant_new("(ii)", 0, 0)));
  }
  Check(!tray && !gtk_widget_get_mapped(widget), "double-click listener can destroy tray safely");
  return 0;
}
