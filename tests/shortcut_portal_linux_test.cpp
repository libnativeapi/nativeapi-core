// An isolated protocol service, not the user's desktop. Generates no input.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <gio/gio.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "../src/platform/linux/shortcut_portal_linux.h"
#ifdef NATIVEAPI_PORTAL_MANAGER_TEST
#include "../src/shortcut_manager.h"
#endif

namespace {
constexpr const char* bus_name = "org.freedesktop.portal.Desktop";
constexpr const char* root = "/org/freedesktop/portal/desktop";
constexpr const char* portal = "org.freedesktop.portal.GlobalShortcuts";
const char* xml = R"(<node>
<interface name='org.freedesktop.portal.GlobalShortcuts'>
<property name='version' type='u' access='read'/>
<method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
<method name='BindShortcuts'><arg type='o' direction='in'/><arg type='a(sa{sv})' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
</interface>
<interface name='org.freedesktop.portal.Session'><method name='Close'/></interface>
<interface name='org.freedesktop.portal.Request'><method name='Close'/></interface>
</node>)";

template<class Predicate> void Wait(Predicate predicate) {
  auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (!predicate() && std::chrono::steady_clock::now() < until) {
    while (g_main_context_iteration(nullptr, false)) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(predicate());
}
struct Server {
  struct Session { std::string owner, key, request; guint object = 0; bool bound = false; };
  GMainContext* context = g_main_context_new();
  GMainLoop* loop = g_main_loop_new(context, false);
  GDBusConnection* connection = nullptr;
  GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(xml, nullptr);
  std::thread thread;
  std::atomic<bool> ready{false};
  std::atomic<int> bind_count{0}, sessions_count{0}, closed_count{0};
  std::atomic<int> explicit_session_closes{0};
  std::atomic<bool> deny{false};
  guint root_object = 0, owner_watch = 0;
  std::unordered_map<std::string, Session> sessions;
  std::unordered_map<std::string, guint> requests;
  static GVariant* Property(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                             const gchar*, GError**, gpointer) { return g_variant_new_uint32(2); }
  static void Method(GDBusConnection*, const gchar* sender, const gchar* path,
                     const gchar*, const gchar* method, GVariant* args,
                     GDBusMethodInvocation* invocation, gpointer data) {
    static_cast<Server*>(data)->Call(sender, path, method, args, invocation);
  }
  const GDBusInterfaceVTable table{Method, Property, nullptr, {nullptr}};
  Server() {
    thread = std::thread([this] {
      g_main_context_push_thread_default(context);
      gchar* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
      connection = g_dbus_connection_new_for_address_sync(address,
          static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                           G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
          nullptr, nullptr, nullptr);
      g_free(address);
      assert(connection);
      root_object = g_dbus_connection_register_object(connection, root, info->interfaces[0], &table, this, nullptr, nullptr);
      Name("RequestName");
      owner_watch = g_dbus_connection_signal_subscribe(connection, "org.freedesktop.DBus", "org.freedesktop.DBus",
          "NameOwnerChanged", "/org/freedesktop/DBus", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
          [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
              GVariant* args, gpointer data) {
            const gchar *name, *old_owner, *new_owner;
            g_variant_get(args, "(&s&s&s)", &name, &old_owner, &new_owner);
            if (*name == ':' && *old_owner && !*new_owner) {
              auto* self = static_cast<Server*>(data);
              for (auto it = self->sessions.begin(); it != self->sessions.end();) {
                if (it->second.owner != name) { ++it; continue; }
                g_dbus_connection_unregister_object(self->connection, it->second.object);
                it = self->sessions.erase(it); --self->sessions_count; ++self->closed_count;
              }
            }
          }, this, nullptr);
      ready.store(true);
      g_main_loop_run(loop);
      g_dbus_connection_signal_unsubscribe(connection, owner_watch);
      g_dbus_connection_unregister_object(connection, root_object);
      for (auto& item : sessions) g_dbus_connection_unregister_object(connection, item.second.object);
      for (auto& item : requests) g_dbus_connection_unregister_object(connection, item.second);
      g_dbus_connection_close_sync(connection, nullptr, nullptr);
      g_clear_object(&connection);
      g_main_context_pop_thread_default(context);
    });
    Wait([&] { return ready.load(); });
  }
  ~Server() {
    Post([this] { g_main_loop_quit(loop); });
    thread.join();
    g_dbus_node_info_unref(info); g_main_loop_unref(loop); g_main_context_unref(context);
  }
  void Post(std::function<void()> fn) {
    auto* value = new std::function<void()>(std::move(fn));
    auto* source = g_idle_source_new();
    g_source_set_callback(source, [](gpointer data) -> gboolean {
      (*static_cast<std::function<void()>*>(data))(); return G_SOURCE_REMOVE;
    }, value, [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
    g_source_attach(source, context); g_source_unref(source);
  }
  void Name(const char* method) {
    GVariant* args = std::string(method) == "RequestName" ? g_variant_new("(su)", bus_name, 0u) : g_variant_new("(s)", bus_name);
    GVariant* reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", method, args, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
    assert(reply); g_variant_unref(reply);
  }
  std::string Path(const char* sender, const char* kind, const char* token) {
    std::string name = sender + 1; for (char& c : name) if (c == '.') c = '_';
    return std::string(root) + "/" + kind + "/" + name + "/" + token;
  }
  void Response(const std::string& owner, const std::string& path, guint code, GVariant* dict) {
    assert(g_dbus_connection_emit_signal(connection, owner.c_str(), path.c_str(),
        "org.freedesktop.portal.Request", "Response", g_variant_new("(u@a{sv})", code, dict), nullptr));
  }
  void Call(const char* sender, const char* path, const char* method, GVariant* args,
             GDBusMethodInvocation* invocation) {
    if (std::string(method) == "Close") {
      if (std::string(path).find("/session/") != std::string::npos) ++explicit_session_closes;
      auto it = sessions.find(path);
      if (it != sessions.end()) {
        g_dbus_connection_unregister_object(connection, it->second.object);
        sessions.erase(it); --sessions_count; ++closed_count;
      }
      g_dbus_method_invocation_return_value(invocation, nullptr); return;
    }
    GVariant* options = nullptr;
    if (std::string(method) == "CreateSession") {
      g_variant_get(args, "(@a{sv})", &options);
      const gchar *token, *session_token;
      assert(g_variant_lookup(options, "handle_token", "&s", &token));
      assert(g_variant_lookup(options, "session_handle_token", "&s", &session_token));
      const auto request = Path(sender, "request", token), session = Path(sender, "session", session_token);
      auto id = g_dbus_connection_register_object(connection, session.c_str(), info->interfaces[1], &table, this, nullptr, nullptr);
      sessions.emplace(session, Session{sender, "", "", id, false}); ++sessions_count;
      GVariantBuilder result; g_variant_builder_init(&result, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add(&result, "{sv}", "session_handle", g_variant_new_string(session.c_str()));
      // Intentionally respond before the method reply to test subscription order.
      Response(sender, request, 0, g_variant_builder_end(&result));
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", request.c_str()));
    } else {
      const gchar *session_path, *parent;
      GVariant* shortcuts;
      g_variant_get(args, "(&o@a(sa{sv})&s@a{sv})", &session_path, &shortcuts, &parent, &options);
      auto& session = sessions.at(session_path);
      assert(!session.bound); session.bound = true; // Bind once per session.
      assert(g_variant_n_children(shortcuts) == 1);
      GVariant* pair = g_variant_get_child_value(shortcuts, 0);
      const gchar* key; GVariant* properties;
      g_variant_get(pair, "(&s@a{sv})", &key, &properties);
      const gchar* description; const gchar* trigger;
      assert(g_variant_lookup(properties, "description", "&s", &description));
      assert(g_variant_lookup(properties, "preferred_trigger", "&s", &trigger));
      assert(std::string(description) == "User-facing action");
      assert(std::string(trigger) == "CTRL+F19");
      session.key = key;
      const gchar* token; assert(g_variant_lookup(options, "handle_token", "&s", &token));
      session.request = Path(sender, "request", token);
      const auto& request = session.request;
      requests[request] = g_dbus_connection_register_object(connection, request.c_str(), info->interfaces[2], &table, this, nullptr, nullptr);
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", request.c_str()));
      ++bind_count;
      if (deny.load()) Approve(session, 1);
      g_variant_unref(properties); g_variant_unref(pair); g_variant_unref(shortcuts);
    }
    g_variant_unref(options);
  }
  void Approve(Session& session, guint code = 0) {
    GVariantBuilder dict, bindings, properties;
    g_variant_builder_init(&dict, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_init(&bindings, G_VARIANT_TYPE("a(sa{sv})"));
    g_variant_builder_init(&properties, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&properties, "{sv}", "trigger_description", g_variant_new_string("Ctrl+F17 (desktop choice)"));
    g_variant_builder_add(&bindings, "(sa{sv})", session.key.c_str(), &properties);
    g_variant_builder_add(&dict, "{sv}", "shortcuts", g_variant_builder_end(&bindings));
    Response(session.owner, session.request, code, g_variant_builder_end(&dict));
  }
  void ApproveAll() { Post([this] { for (auto& item : sessions) if (item.second.bound) Approve(item.second); }); }
  void Activate(bool foreign, bool down = true) {
    Post([this, foreign, down] {
      for (auto& item : sessions) {
        const auto& session = item.second;
        assert(g_dbus_connection_emit_signal(connection, session.owner.c_str(), root, portal, down ? "Activated" : "Deactivated",
          g_variant_new("(ost@a{sv})", foreign ? "/wrong/session" : item.first.c_str(),
            session.key.c_str(), static_cast<guint64>(1), g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)), nullptr));
      }
    });
  }
  void ChangeBinding() {
    Post([this] {
      for (auto& item : sessions) {
        GVariantBuilder bindings, properties;
        g_variant_builder_init(&bindings, G_VARIANT_TYPE("a(sa{sv})"));
        g_variant_builder_init(&properties, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&properties, "{sv}", "trigger_description", g_variant_new_string("Ctrl+F16 (changed)"));
        g_variant_builder_add(&bindings, "(sa{sv})", item.second.key.c_str(), &properties);
        assert(g_dbus_connection_emit_signal(connection, item.second.owner.c_str(), root, portal,
          "ShortcutsChanged", g_variant_new("(oa(sa{sv}))", item.first.c_str(), &bindings), nullptr));
      }
    });
  }
};
}

int main() {
  using nativeapi::detail::ShortcutPortalLinux;
  GTestDBus* bus = g_test_dbus_new(G_TEST_DBUS_NONE); g_test_dbus_up(bus);
  {
    Server server;
    std::atomic<int> succeeded{0}, failed{0}, activated{0}, revoked{0};
    ShortcutPortalLinux client([&](auto, auto signal, const auto&) {
      if (signal == ShortcutPortalLinux::Signal::Activated) ++activated;
      if (signal == ShortcutPortalLinux::Signal::Closed) ++revoked;
    });
    assert(ShortcutPortalLinux::IsSupported());
    std::cout << "PASS: isolated capability query" << std::endl;
    nativeapi::ShortcutOptions options; options.accelerator = "Ctrl+F19"; options.description = "User-facing action";
    // A manager normally assigns the stable key; this fixture uses a real
    // synchronous manager-independent Shortcut with an empty key for transport.
    auto shortcut = std::make_shared<nativeapi::Shortcut>(101, options);
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto completion = [&](bool ok, const auto&) { if (ok) ++succeeded; else ++failed; };
    client.Request(shortcut, "CTRL+F19", cancel, completion);
    Wait([&] { return server.bind_count.load() == 1; });
    assert(succeeded.load() == 0 && failed.load() == 0); // Approval remains pending.
    server.ApproveAll(); Wait([&] { return succeeded.load() == 1; });
    assert(client.TriggerDescription(101) == "Ctrl+F17 (desktop choice)");
    std::cout << "PASS: early response, pending approval and desktop choice" << std::endl;
    server.Activate(true); std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(activated.load() == 0);
    server.Activate(false); Wait([&] { return activated.load() == 1; });
    assert(client.Unregister(101)); Wait([&] { return server.sessions_count.load() == 0; });
    Wait([&] { return server.explicit_session_closes.load() != 0; });
    assert(revoked.load() == 0); // Explicit unregistration is not a provider failure.
    std::cout << "PASS: event filtering and explicit cleanup" << std::endl;

    client.Request(std::make_shared<nativeapi::Shortcut>(102, options), "CTRL+F19", cancel, completion);
    Wait([&] { return server.bind_count.load() == 2; });
    cancel->store(true); client.Cancel(102);
    Wait([&] { return failed.load() == 1 && server.sessions_count.load() == 0; });
    server.deny.store(true);
    client.Request(std::make_shared<nativeapi::Shortcut>(103, options), "CTRL+F19",
                    std::make_shared<std::atomic<bool>>(false), completion);
    Wait([&] { return failed.load() == 2 && server.sessions_count.load() == 0; });
    server.deny.store(false);
    std::cout << "PASS: cancellation and rejection release sessions" << std::endl;
    client.Request(std::make_shared<nativeapi::Shortcut>(104, options), "CTRL+F19",
                    std::make_shared<std::atomic<bool>>(false), completion);
    Wait([&] { return server.bind_count.load() == 4; });
    server.ApproveAll(); Wait([&] { return succeeded.load() == 2; });
    server.Post([&] { server.Name("ReleaseName"); });
    Wait([&] { return revoked.load() == 1; });
    assert(client.Unregister(104));
    client.Shutdown();
    std::cout << "PASS: service loss and transport shutdown" << std::endl;
#ifdef NATIVEAPI_PORTAL_MANAGER_TEST
    // Exercise the real manager, platform selection, parser, dispatcher and
    // public metadata against the same private D-Bus protocol service.
    g_setenv("WAYLAND_DISPLAY", "nativeapi-test", true);
    g_unsetenv("DISPLAY");
    server.Post([&] { server.Name("RequestName"); });
    Wait([&] { return ShortcutPortalLinux::IsSupported(); });
    auto& manager = nativeapi::ShortcutManager::GetInstance();
    std::atomic<int> registered{0}, rejected{0}, downs{0}, ups{0}, callbacks{0};
    auto listener = manager.AddListener<nativeapi::ShortcutEvent>([&](const auto& event) {
      if (dynamic_cast<const nativeapi::ShortcutRegisteredEvent*>(&event)) ++registered;
      if (dynamic_cast<const nativeapi::ShortcutRegistrationFailedEvent*>(&event)) ++rejected;
      if (dynamic_cast<const nativeapi::ShortcutActivatedEvent*>(&event)) ++downs;
      if (dynamic_cast<const nativeapi::ShortcutReleasedEvent*>(&event)) ++ups;
    });
    options.callback = [&] { ++callbacks; };
    // Raw X11 keysyms used by the legacy Dart API must become XDG key names.
    options.accelerator = "Ctrl+NativeKey65488";
    int expected_bindings = server.bind_count.load() + 1;
    auto id = manager.RequestRegistration(options, "stable-action");
    assert(id && manager.IsRegistrationPending(id) && !manager.Get(id));
    Wait([&] { return server.bind_count.load() == expected_bindings; });
    server.ApproveAll(); Wait([&] { return registered.load() == 1; });
    auto handle = manager.Get(id);
    assert(handle && handle->GetRegistrationKey() == "stable-action");
    assert(handle->GetTriggerDescription() == "Ctrl+F17 (desktop choice)");
    assert(handle->GetAccelerator() == "Ctrl+NativeKey65488");
    server.Activate(false); Wait([&] { return downs.load() == 1 && callbacks.load() == 1; });
    server.Activate(false, false); Wait([&] { return ups.load() == 1; });
    server.ChangeBinding();
    Wait([&] { return handle->GetTriggerDescription() == "Ctrl+F16 (changed)"; });
    assert(manager.Unregister(id));
    Wait([&] { return server.sessions_count.load() == 0; });
    assert(!manager.Get(id) && !handle->GetCallback());
    ++expected_bindings;
    auto cancelled_id = manager.RequestRegistration(options, "stable-action");
    Wait([&] { return server.bind_count.load() == expected_bindings; });
    assert(manager.CancelRegistration(cancelled_id));
    Wait([&] { return rejected.load() == 1 && server.sessions_count.load() == 0; });
    ++expected_bindings;
    auto lost = manager.RequestRegistration(options, "stable-action");
    Wait([&] { return server.bind_count.load() == expected_bindings; });
    server.ApproveAll(); Wait([&] { return registered.load() == 2; });
    server.Post([&] { server.Name("ReleaseName"); });
    Wait([&] { return !manager.Get(lost); });
    manager.RemoveListener(listener);
    assert(manager.UnregisterAll() == 0);
    std::cout << "PASS: real manager requests, metadata updates, down/up dispatch, cancellation and service loss" << std::endl;
#endif
  }
  std::cout << "PASS: server shutdown" << std::endl;
  g_test_dbus_down(bus); g_object_unref(bus);
  std::cout << "isolated GlobalShortcuts protocol checks passed\n";
}
