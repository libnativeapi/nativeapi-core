#include "shortcut_portal_linux.h"

#include <gio/gio.h>
#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nativeapi::detail {
namespace {
constexpr const char* kBus = "org.freedesktop.portal.Desktop";
constexpr const char* kRoot = "/org/freedesktop/portal/desktop";
constexpr const char* kInterface = "org.freedesktop.portal.GlobalShortcuts";
constexpr const char* kRequest = "org.freedesktop.portal.Request";
constexpr const char* kSession = "org.freedesktop.portal.Session";
std::string Token() {
  gchar* value = g_uuid_string_random();
  std::string result = "nativeapi_" + std::string(value);
  g_free(value);
  std::replace(result.begin(), result.end(), '-', '_');
  return result;
}
bool Utf8(const std::string& value) {
  return g_utf8_validate(value.data(), static_cast<gssize>(value.size()), nullptr);
}
}

struct ShortcutPortalLinux::State : std::enable_shared_from_this<State> {
  struct Entry {
    std::weak_ptr<State> owner;
    std::shared_ptr<Shortcut> shortcut;
    std::shared_ptr<std::atomic<bool>> cancelled;
    Completion completion;
    std::string trigger, request_path, session_path, trigger_description, application_id;
    GDBusConnection* connection = nullptr;
    GCancellable* transport = g_cancellable_new();
    std::vector<guint> subscriptions;
    std::atomic<bool> active{false}, closed{false};
    bool completed = false;
    std::mutex info_mutex;
    ~Entry() { g_clear_object(&connection); g_clear_object(&transport); }
  };
  struct Rpc {
    std::shared_ptr<Entry> entry;
    std::function<void(GVariant*, GError*)> done;
  };
  explicit State(Listener value) : listener(std::move(value)) {
    context = g_main_context_new();
    loop = g_main_loop_new(context, false);
  }
  ~State() { g_main_loop_unref(loop); g_main_context_unref(context); }
  GMainContext* context;
  GMainLoop* loop;
  Listener listener;
  std::thread worker;
  std::mutex mutex;
  std::unordered_map<ShortcutId, std::shared_ptr<Entry>> entries;
  bool started = false;
  std::atomic<bool> stopping{false};
  std::atomic<size_t> pending_posts{0};
  size_t outstanding = 0; // Worker-context only.

  void Start() {
    std::lock_guard<std::mutex> lock(mutex);
    if (started || stopping.load()) return;
    started = true;
    worker = std::thread([self = shared_from_this()] {
      g_main_context_push_thread_default(self->context);
      g_main_loop_run(self->loop);
      g_main_context_pop_thread_default(self->context);
    });
  }
  void Post(std::function<void(State&)> fn, bool force = false) {
    if (!force && stopping.load()) return;
    ++pending_posts;
    auto weak = weak_from_this();
    auto* task = new std::function<void()>([weak, fn = std::move(fn)] {
      if (auto self = weak.lock()) {
        fn(*self);
        --self->pending_posts;
        self->MaybeQuit();
      }
    });
    // Unlike g_main_context_invoke, attaching a source never calls inline.
    GSource* source = g_idle_source_new();
    g_source_set_callback(source, [](gpointer data) -> gboolean {
      (*static_cast<std::function<void()>*>(data))();
      return G_SOURCE_REMOVE;
    }, task, [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
    g_source_attach(source, context);
    g_source_unref(source);
  }
  void MaybeQuit() {
    if (stopping.load() && outstanding == 0 && pending_posts.load() == 0) g_main_loop_quit(loop);
  }
  static void RpcDone(GObject* object, GAsyncResult* result, gpointer data) {
    std::unique_ptr<Rpc> call(static_cast<Rpc*>(data));
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, &error);
    if (auto self = call->entry->owner.lock()) {
      --self->outstanding;
      if (!call->entry->closed.load() && !self->stopping.load()) call->done(reply, error);
      self->MaybeQuit();
    }
    if (reply) g_variant_unref(reply);
    g_clear_error(&error);
  }
  void Call(const std::shared_ptr<Entry>& entry, const char* interface_name,
            const char* method, GVariant* args, const GVariantType* expected,
            std::function<void(GVariant*, GError*)> done) {
    ++outstanding;
    g_dbus_connection_call(entry->connection, kBus, kRoot, interface_name, method,
                           args, expected, G_DBUS_CALL_FLAGS_NONE, 5000,
                           entry->transport, RpcDone, new Rpc{entry, std::move(done)});
  }
  static void IgnoreClose(GDBusConnection* connection, const std::string& path,
                          const char* interface_name) {
    if (!path.empty() && !g_dbus_connection_is_closed(connection)) {
      GDBusMessage* message = g_dbus_message_new_method_call(kBus, path.c_str(), interface_name, "Close");
      g_dbus_message_set_flags(message, static_cast<GDBusMessageFlags>(
          G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED | G_DBUS_MESSAGE_FLAGS_NO_AUTO_START));
      g_dbus_connection_send_message(connection, message, G_DBUS_SEND_MESSAGE_FLAGS_NONE, nullptr, nullptr);
      g_object_unref(message);
    }
  }
  void CloseConnection(GDBusConnection* connection) {
    if (g_dbus_connection_is_closed(connection)) return;
    ++outstanding;
    g_dbus_connection_close(connection, nullptr,
        [](GObject* object, GAsyncResult* result, gpointer data) {
          std::unique_ptr<std::weak_ptr<State>> weak(static_cast<std::weak_ptr<State>*>(data));
          GError* error = nullptr;
          g_dbus_connection_close_finish(G_DBUS_CONNECTION(object), result, &error);
          g_clear_error(&error);
          if (auto self = weak->lock()) { --self->outstanding; self->MaybeQuit(); }
        }, new std::weak_ptr<State>(weak_from_this()));
  }
  void FlushAndClose(GDBusConnection* connection) {
    if (g_dbus_connection_is_closed(connection)) return;
    ++outstanding;
    g_dbus_connection_flush(connection, nullptr,
        [](GObject* object, GAsyncResult* result, gpointer data) {
          std::unique_ptr<std::weak_ptr<State>> weak(static_cast<std::weak_ptr<State>*>(data));
          GError* error = nullptr;
          g_dbus_connection_flush_finish(G_DBUS_CONNECTION(object), result, &error);
          g_clear_error(&error);
          if (auto self = weak->lock()) {
            --self->outstanding;
            self->CloseConnection(G_DBUS_CONNECTION(object));
            self->MaybeQuit();
          }
        }, new std::weak_ptr<State>(weak_from_this()));
  }
  void Finish(const std::shared_ptr<Entry>& entry, bool success, const std::string& reason) {
    if (entry->completed) return;
    entry->completed = true;
    auto completion = std::move(entry->completion);
    completion(success, reason);
  }
  void Close(const std::shared_ptr<Entry>& entry, const std::string& reason) {
    if (entry->closed.exchange(true)) return;
    g_cancellable_cancel(entry->transport);
    if (entry->connection) {
      for (auto id : entry->subscriptions) {
        g_dbus_connection_signal_unsubscribe(entry->connection, id);
      }
      entry->subscriptions.clear();
      IgnoreClose(entry->connection, entry->request_path, kRequest);
      IgnoreClose(entry->connection, entry->session_path, kSession);
      // Closing the private connection releases even sessions that the server
      // creates after a cancelled CreateSession reply. Other actions are isolated.
      // Closing a connection discards unsent messages. Deliver Request.Close
      // and Session.Close before releasing the peer, even during shutdown.
      FlushAndClose(entry->connection);
    }
    Finish(entry, false, reason);
    if (entry->active.load()) {
      if (!stopping.load()) listener(entry->shortcut->GetId(), Signal::Closed, reason);
    } else {
      std::lock_guard<std::mutex> lock(mutex);
      entries.erase(entry->shortcut->GetId());
    }
  }
  using SignalFn = std::function<void(State&, const std::shared_ptr<Entry>&, GVariant*)>;
  struct Watch { std::weak_ptr<Entry> entry; SignalFn fn; };
  void WatchSignal(const std::shared_ptr<Entry>& entry, const char* sender,
                    const char* interface_name, const char* member,
                    const char* path, const char* arg0, SignalFn fn) {
    auto* watch = new Watch{entry, std::move(fn)};
    const guint id = g_dbus_connection_signal_subscribe(
        entry->connection, sender, interface_name, member, path, arg0,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
           GVariant* parameters, gpointer data) {
          auto* value = static_cast<Watch*>(data);
          auto fn = value->fn;
          if (auto entry = value->entry.lock()) {
            if (auto self = entry->owner.lock()) {
              if (!entry->closed.load()) fn(*self, entry, parameters);
            }
          }
        }, watch, [](gpointer data) { delete static_cast<Watch*>(data); });
    entry->subscriptions.push_back(id);
  }
  void Request(const std::shared_ptr<Entry>& entry, const char* method,
                GVariant* args, std::string predicted_path, SignalFn response) {
    entry->request_path = predicted_path;
    // Subscribe before sending: the Response can precede the method reply.
    WatchSignal(entry, kBus, kRequest, "Response", predicted_path.c_str(), nullptr,
                [response = std::move(response), predicted_path](State& self,
                    const std::shared_ptr<Entry>& item, GVariant* result) {
      if (item->request_path != predicted_path) return;
      if (!g_variant_is_of_type(result, G_VARIANT_TYPE("(ua{sv})"))) {
        self.Close(item, "Invalid desktop shortcut response"); return;
      }
      item->request_path.clear();
      response(self, item, result);
    });
    Call(entry, kInterface, method, args, G_VARIANT_TYPE("(o)"),
         [this, entry, predicted_path](GVariant* reply, GError* error) {
      if (error) { Close(entry, error->message); return; }
      const gchar* path = nullptr;
      g_variant_get(reply, "(&o)", &path);
      // The handle_token contract makes this path predictable and race-free.
      if (predicted_path != path) Close(entry, "Desktop returned an unexpected request handle");
    });
  }
  static std::string Sender(const std::shared_ptr<Entry>& entry) {
    std::string sender = g_dbus_connection_get_unique_name(entry->connection);
    sender.erase(0, 1);
    std::replace(sender.begin(), sender.end(), '.', '_');
    return sender;
  }
  void Create(const std::shared_ptr<Entry>& entry) {
    const auto token = Token(), session_token = Token();
    entry->session_path = std::string(kRoot) + "/session/" + Sender(entry) + "/" + session_token;
    WatchSignal(entry, kBus, kSession, "Closed", entry->session_path.c_str(), nullptr,
                [](State& self, const std::shared_ptr<Entry>& item, GVariant*) {
      self.Close(item, "Desktop closed the shortcut session");
    });
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
    g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(session_token.c_str()));
    Request(entry, "CreateSession", g_variant_new("(a{sv})", &options),
            std::string(kRoot) + "/request/" + Sender(entry) + "/" + token,
            [](State& self, const std::shared_ptr<Entry>& item, GVariant* reply) {
      guint response = 0; GVariant* results = nullptr;
      g_variant_get(reply, "(u@a{sv})", &response, &results);
      GVariant* value = g_variant_lookup_value(results, "session_handle", nullptr);
      const bool valid = value &&
          (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) ||
           g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH)) &&
          g_variant_is_object_path(g_variant_get_string(value, nullptr));
      if (response || !valid) {
        self.Close(item, "Desktop did not create a shortcut session");
      } else {
        const std::string actual = g_variant_get_string(value, nullptr);
        if (item->session_path != actual) {
          item->session_path = actual;
          self.Close(item, "Desktop returned an unexpected session handle");
        } else {
          self.Bind(item);
        }
      }
      if (value) g_variant_unref(value);
      g_variant_unref(results);
    });
  }
  void Bind(const std::shared_ptr<Entry>& entry) {
    const auto token = Token();
    GVariantBuilder shortcuts, properties, options;
    g_variant_builder_init(&shortcuts, G_VARIANT_TYPE("a(sa{sv})"));
    g_variant_builder_init(&properties, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&properties, "{sv}", "description",
                          g_variant_new_string(entry->shortcut->GetDescription().c_str()));
    g_variant_builder_add(&properties, "{sv}", "preferred_trigger",
                          g_variant_new_string(entry->trigger.c_str()));
    g_variant_builder_add(&shortcuts, "(sa{sv})",
                          entry->shortcut->GetRegistrationKey().c_str(), &properties);
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
    Request(entry, "BindShortcuts",
            g_variant_new("(oa(sa{sv})sa{sv})", entry->session_path.c_str(), &shortcuts, "", &options),
            std::string(kRoot) + "/request/" + Sender(entry) + "/" + token,
            [](State& self, const std::shared_ptr<Entry>& item, GVariant* reply) {
      guint response = 0; GVariant* results = nullptr;
      g_variant_get(reply, "(u@a{sv})", &response, &results);
      GVariant* bindings = g_variant_lookup_value(results, "shortcuts", G_VARIANT_TYPE("a(sa{sv})"));
      bool found = false;
      if (!response && bindings) {
        GVariantIter iterator; const gchar* key; GVariant* properties;
        g_variant_iter_init(&iterator, bindings);
        while (g_variant_iter_next(&iterator, "(&s@a{sv})", &key, &properties)) {
          if (item->shortcut->GetRegistrationKey() == key) {
            found = true;
            const gchar* description = nullptr;
            if (g_variant_lookup(properties, "trigger_description", "&s", &description)) {
              std::lock_guard<std::mutex> lock(item->info_mutex);
              item->trigger_description = description;
            }
          }
          g_variant_unref(properties);
        }
      }
      if (bindings) g_variant_unref(bindings);
      g_variant_unref(results);
      if (!found || item->cancelled->load()) {
        self.Close(item, response == 1 ? "Shortcut approval cancelled" : "Shortcut was not approved");
      } else {
        item->active.store(true);
        self.Finish(item, true, "");
      }
    });
  }
  void Subscribe(const std::shared_ptr<Entry>& entry) {
    for (const auto* member : {"Activated", "Deactivated"}) {
      const bool down = std::string(member) == "Activated";
      WatchSignal(entry, kBus, kInterface, member, kRoot, nullptr,
                  [down](State& self, const std::shared_ptr<Entry>& item, GVariant* value) {
        if (!item->active.load() || !g_variant_is_of_type(value, G_VARIANT_TYPE("(osta{sv})"))) return;
        const gchar *session, *key; guint64 timestamp; GVariant* options;
        g_variant_get(value, "(&o&st@a{sv})", &session, &key, &timestamp, &options);
        if (item->session_path == session && item->shortcut->GetRegistrationKey() == key) {
          self.listener(item->shortcut->GetId(), down ? Signal::Activated : Signal::Released, "");
        }
        g_variant_unref(options);
      });
    }
    WatchSignal(entry, kBus, kInterface, "ShortcutsChanged", kRoot, nullptr,
                [](State& self, const std::shared_ptr<Entry>& item, GVariant* value) {
      if (!item->active.load() || !g_variant_is_of_type(value, G_VARIANT_TYPE("(oa(sa{sv}))"))) return;
      const gchar* session; GVariant* bindings;
      g_variant_get(value, "(&o@a(sa{sv}))", &session, &bindings);
      if (item->session_path == session) {
        GVariantIter iterator; const gchar* key; GVariant* properties;
        g_variant_iter_init(&iterator, bindings);
        while (g_variant_iter_next(&iterator, "(&s@a{sv})", &key, &properties)) {
          if (item->shortcut->GetRegistrationKey() == key) {
            const gchar* description = "";
            g_variant_lookup(properties, "trigger_description", "&s", &description);
            const std::string label = description;
            { std::lock_guard<std::mutex> lock(item->info_mutex); item->trigger_description = label; }
            self.listener(item->shortcut->GetId(), Signal::BindingChanged, label);
          }
          g_variant_unref(properties);
        }
      }
      g_variant_unref(bindings);
    });
    WatchSignal(entry, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                "/org/freedesktop/DBus", kBus,
                [](State& self, const std::shared_ptr<Entry>& item, GVariant* value) {
      const gchar *name, *old_owner, *new_owner;
      g_variant_get(value, "(&s&s&s)", &name, &old_owner, &new_owner);
      if (*old_owner && std::string(old_owner) != new_owner) {
        self.Close(item, "Desktop shortcut service restarted");
      }
    });
  }
  void CheckVersion(const std::shared_ptr<Entry>& entry) {
    Call(entry, "org.freedesktop.DBus.Properties", "Get",
         g_variant_new("(ss)", kInterface, "version"), G_VARIANT_TYPE("(v)"),
         [this, entry](GVariant* reply, GError* error) {
      if (error) { Close(entry, error->message); return; }
      GVariant* version = nullptr;
      g_variant_get(reply, "(v)", &version);
      const bool supported = g_variant_is_of_type(version, G_VARIANT_TYPE_UINT32) &&
                             g_variant_get_uint32(version) >= 1;
      g_variant_unref(version);
      if (!supported) { Close(entry, "Desktop does not support global shortcuts"); return; }
      Create(entry);
    });
  }
  void RegisterHost(const std::shared_ptr<Entry>& entry) {
    if (entry->application_id.empty() || g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS) || g_getenv("SNAP")) {
      CheckVersion(entry); return;
    }
    Call(entry, "org.freedesktop.host.portal.Registry", "Register",
         g_variant_new("(s@a{sv})", entry->application_id.c_str(),
                       g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)),
         G_VARIANT_TYPE("()"), [this, entry](GVariant*, GError* error) {
      if (error) {
        gchar* name = g_dbus_error_get_remote_error(error);
        const bool old_portal = name &&
            (g_str_has_suffix(name, ".UnknownMethod") || g_str_has_suffix(name, ".UnknownInterface"));
        g_free(name);
        if (!old_portal) { Close(entry, error->message); return; }
      }
      CheckVersion(entry);
    });
  }
  void Begin(const std::shared_ptr<Entry>& entry) {
    if (stopping.load() || entry->cancelled->load()) { Close(entry, "Registration cancelled"); return; }
    if (!Utf8(entry->shortcut->GetRegistrationKey()) || !Utf8(entry->shortcut->GetDescription())) {
      Close(entry, "Shortcut name and description must be valid UTF-8"); return;
    }
    GError* error = nullptr;
    gchar* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, entry->transport, &error);
    if (!address) { Close(entry, error ? error->message : "No desktop session bus"); g_clear_error(&error); return; }
    ++outstanding;
    g_dbus_connection_new_for_address(address,
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                         G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, entry->transport,
        [](GObject*, GAsyncResult* result, gpointer data) {
          std::unique_ptr<std::shared_ptr<Entry>> held(static_cast<std::shared_ptr<Entry>*>(data));
          const auto& item = *held;
          GError* error = nullptr;
          GDBusConnection* connection = g_dbus_connection_new_for_address_finish(result, &error);
          if (auto self = item->owner.lock()) {
            --self->outstanding;
            if (item->closed.load() || self->stopping.load() || !connection) {
              if (connection) { self->CloseConnection(connection); g_object_unref(connection); }
              self->Close(item, error ? error->message : "Registration cancelled");
            } else {
              item->connection = connection;
              g_dbus_connection_set_exit_on_close(connection, false);
              g_signal_connect_data(connection, "closed",
                  G_CALLBACK(+[](GDBusConnection*, gboolean, GError*, gpointer data) {
                    auto* weak = static_cast<std::weak_ptr<Entry>*>(data);
                    if (auto entry = weak->lock()) {
                      if (auto self = entry->owner.lock()) self->Close(entry, "Desktop session bus disconnected");
                    }
                  }), new std::weak_ptr<Entry>(item),
                  +[](gpointer data, GClosure*) { delete static_cast<std::weak_ptr<Entry>*>(data); },
                  G_CONNECT_DEFAULT);
              self->Subscribe(item);
              self->RegisterHost(item);
            }
            self->MaybeQuit();
          } else if (connection) { g_dbus_connection_close_sync(connection, nullptr, nullptr); g_object_unref(connection); }
          g_clear_error(&error);
        }, new std::shared_ptr<Entry>(entry));
    g_free(address);
  }
};

ShortcutPortalLinux::ShortcutPortalLinux(Listener listener) : state_(std::make_shared<State>(std::move(listener))) {}
ShortcutPortalLinux::~ShortcutPortalLinux() { Shutdown(); }
bool ShortcutPortalLinux::IsSupported() {
  GError* error = nullptr;
  gchar* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!address) { g_clear_error(&error); return false; }
  GDBusConnection* bus = g_dbus_connection_new_for_address_sync(address,
      static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                       G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
      nullptr, nullptr, &error);
  g_free(address);
  if (!bus) { g_clear_error(&error); return false; }
  GVariant* reply = g_dbus_connection_call_sync(bus, kBus, kRoot, "org.freedesktop.DBus.Properties", "Get",
      g_variant_new("(ss)", kInterface, "version"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE,
      1500, nullptr, &error);
  bool supported = false;
  if (reply) {
    GVariant* version = nullptr;
    g_variant_get(reply, "(v)", &version);
    supported = g_variant_is_of_type(version, G_VARIANT_TYPE_UINT32) && g_variant_get_uint32(version) >= 1;
    g_variant_unref(version); g_variant_unref(reply);
  }
  g_clear_error(&error);
  g_dbus_connection_close_sync(bus, nullptr, nullptr);
  g_object_unref(bus);
  return supported;
}
void ShortcutPortalLinux::Request(std::shared_ptr<Shortcut> shortcut, std::string trigger,
                                  std::shared_ptr<std::atomic<bool>> cancelled, Completion completion) {
  auto entry = std::make_shared<State::Entry>();
  entry->owner = state_; entry->shortcut = std::move(shortcut); entry->trigger = std::move(trigger);
  entry->cancelled = std::move(cancelled); entry->completion = std::move(completion);
  if (GApplication* app = g_application_get_default()) {
    const gchar* id = g_application_get_application_id(app);
    if (id) entry->application_id = id;
  }
  bool stopping;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    stopping = state_->stopping.load();
    if (!stopping) state_->entries.emplace(entry->shortcut->GetId(), entry);
  }
  if (stopping) { entry->completion(false, "Shortcut provider is stopping"); return; }
  state_->Start();
  state_->Post([entry](State& self) { self.Begin(entry); });
}
void ShortcutPortalLinux::Cancel(ShortcutId id) {
  state_->Post([id](State& self) {
    std::shared_ptr<State::Entry> entry;
    { std::lock_guard<std::mutex> lock(self.mutex); auto it = self.entries.find(id); if (it != self.entries.end()) entry = it->second; }
    if (entry) self.Close(entry, "Registration cancelled");
  });
}
bool ShortcutPortalLinux::Unregister(ShortcutId id) {
  std::shared_ptr<State::Entry> entry;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto it = state_->entries.find(id);
    if (it == state_->entries.end() || !it->second->active.load()) return false;
    entry = it->second; state_->entries.erase(it); entry->active.store(false);
  }
  state_->Post([entry](State& self) { self.Close(entry, "Shortcut unregistered"); });
  return true;
}
std::string ShortcutPortalLinux::TriggerDescription(ShortcutId id) const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  auto it = state_->entries.find(id);
  if (it == state_->entries.end()) return "";
  std::lock_guard<std::mutex> info_lock(it->second->info_mutex);
  return it->second->trigger_description;
}
void ShortcutPortalLinux::Shutdown() {
  bool started;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stopping.exchange(true)) return;
    started = state_->started;
  }
  if (!started) {
    std::vector<std::shared_ptr<State::Entry>> entries;
    { std::lock_guard<std::mutex> lock(state_->mutex); for (auto& item : state_->entries) entries.push_back(item.second); state_->entries.clear(); }
    for (auto& item : entries) state_->Finish(item, false, "Shortcut provider stopped");
    return;
  }
  state_->Post([](State& self) {
    std::vector<std::shared_ptr<State::Entry>> entries;
    { std::lock_guard<std::mutex> lock(self.mutex); for (auto& item : self.entries) entries.push_back(item.second); self.entries.clear(); }
    for (auto& item : entries) self.Close(item, "Shortcut provider stopped");
    self.MaybeQuit();
  }, true);
  if (state_->worker.joinable()) state_->worker.join();
}
}  // namespace nativeapi::detail
