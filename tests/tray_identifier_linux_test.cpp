// Exercise the actual SNI protocol on a private session bus, without a panel
// or display server. A watcher reads Id as soon as the icon registers.
#include "nativeapi.h"
#include "../src/capi/tray_icon_c.h"
#include "../src/capi/string_utils_c.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
void Require(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  if (!ok) std::exit(1);
}

bool Wait(const std::function<bool()>& done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    while (g_main_context_iteration(nullptr, FALSE)) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return done();
}

GDBusConnection* Connect() {
  GError* error = nullptr;
  auto* connection = g_dbus_connection_new_for_address_sync(
      g_getenv("DBUS_SESSION_BUS_ADDRESS"),
      static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                        G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
      nullptr, nullptr, &error);
  if (error) std::cerr << error->message << '\n';
  Require(connection != nullptr, "connect to private bus");
  g_dbus_connection_set_exit_on_close(connection, FALSE);
  return connection;
}

std::string ReadId(GDBusConnection* client, const char* destination) {
  struct Read { bool done = false; std::string id; } read;
  g_dbus_connection_call(client, destination, "/StatusNotifierItem",
      "org.freedesktop.DBus.Properties", "Get",
      g_variant_new("(ss)", "org.kde.StatusNotifierItem", "Id"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr,
      [](GObject* source, GAsyncResult* result, gpointer data) {
        auto* read = static_cast<Read*>(data);
        GError* error = nullptr;
        auto* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
        if (reply) {
          GVariant* value = nullptr;
          g_variant_get(reply, "(v)", &value);
          read->id = g_variant_get_string(value, nullptr);
          g_variant_unref(value);
          g_variant_unref(reply);
        }
        if (error) { std::cerr << error->message << '\n'; g_error_free(error); }
        read->done = true;
      }, &read);
  Require(Wait([&] { return read.done; }), "read SNI Id before timeout");
  return read.id;
}

class Watcher {
 public:
  Watcher() : thread_([this] { Run(); }) {
    Require(Wait([&] { return ready_.load(); }), "watcher ready before icon construction");
  }
  ~Watcher() {
    g_main_context_invoke(context_, [](gpointer data) -> gboolean {
      g_main_loop_quit(static_cast<GMainLoop*>(data)); return G_SOURCE_REMOVE;
    }, loop_);
    thread_.join();
  }
  std::vector<std::string> FirstIds() {
    std::lock_guard<std::mutex> lock(mutex_);
    return first_ids_;
  }

 private:
  void Run() {
    context_ = g_main_context_new();
    g_main_context_push_thread_default(context_);
    loop_ = g_main_loop_new(context_, FALSE);
    auto* connection = Connect();
    auto* info = g_dbus_node_info_new_for_xml(
        "<node><interface name='org.kde.StatusNotifierWatcher'>"
        "<method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>"
        "</interface></node>", nullptr);
    static const GDBusInterfaceVTable vtable = {
      [](GDBusConnection* connection, const gchar*, const gchar*, const gchar*,
         const gchar*, GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data) {
        const char* service = nullptr;
        g_variant_get(parameters, "(&s)", &service);
        // The library's registration call is synchronous. Return first, then
        // query asynchronously so both peers can dispatch their main contexts.
        g_dbus_method_invocation_return_value(invocation, nullptr);
        g_dbus_connection_call(connection, service, "/StatusNotifierItem",
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", "org.kde.StatusNotifierItem", "Id"),
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr,
            [](GObject* source, GAsyncResult* result, gpointer data) {
              auto* watcher = static_cast<Watcher*>(data);
              auto* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, nullptr);
              if (!reply) return;
              GVariant* value = nullptr;
              g_variant_get(reply, "(v)", &value);
              { std::lock_guard<std::mutex> lock(watcher->mutex_);
                watcher->first_ids_.emplace_back(g_variant_get_string(value, nullptr)); }
              g_variant_unref(value);
              g_variant_unref(reply);
            }, data);
      }, nullptr, nullptr
    };
    const guint registration = g_dbus_connection_register_object(connection,
        "/StatusNotifierWatcher", info->interfaces[0], &vtable, this, nullptr, nullptr);
    Require(registration != 0, "export test watcher");
    auto* reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
        g_variant_new("(su)", "org.kde.StatusNotifierWatcher", 0u),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
    Require(reply != nullptr, "own test watcher name");
    g_variant_unref(reply);
    ready_.store(true);
    g_main_loop_run(loop_);
    g_dbus_connection_unregister_object(connection, registration);
    g_dbus_node_info_unref(info);
    g_dbus_connection_close_sync(connection, nullptr, nullptr);
    g_object_unref(connection);
    g_main_loop_unref(loop_);
    g_main_context_pop_thread_default(context_);
    g_main_context_unref(context_);
  }
  std::atomic<bool> ready_{false};
  GMainContext* context_ = nullptr;
  GMainLoop* loop_ = nullptr;
  std::mutex mutex_;
  std::vector<std::string> first_ids_;
  std::thread thread_;
};
int ChildIcon() {
  auto* loop = g_main_loop_new(nullptr, FALSE);
  struct Stop {
    GMainLoop* loop;
    bool timeout = false;
  } stop{loop};
  const auto input = g_unix_fd_add(
      STDIN_FILENO, G_IO_HUP,
      [](gint, GIOCondition, gpointer data) -> gboolean {
        g_main_loop_quit(static_cast<Stop*>(data)->loop);
        return G_SOURCE_REMOVE;
      },
      &stop);
  const auto watchdog = g_timeout_add_seconds(
      10,
      [](gpointer data) -> gboolean {
        auto* stop = static_cast<Stop*>(data);
        stop->timeout = true;
        g_main_loop_quit(stop->loop);
        return G_SOURCE_REMOVE;
      },
      &stop);
  {
    nativeapi::TrayIcon icon;
    g_main_loop_run(loop);
  }
  if (stop.timeout)
    g_source_remove(input);
  else
    g_source_remove(watchdog);
  g_main_loop_unref(loop);
  return stop.timeout ? 2 : 0;
}

void CheckSeparateApplications(Watcher& watcher) {
  auto* executable = g_file_read_link("/proc/self/exe", nullptr);
  auto* directory = g_dir_make_tmp("nativeapi-tray-apps-XXXXXX", nullptr);
  Require(executable && directory, "create isolated executable fixtures");
  const std::filesystem::path folder(directory);
  const std::vector<std::string> names{"nativeapi-tray-alpha", "nativeapi-tray-beta"};
  for (const auto& name : names)
    std::filesystem::copy_file(executable, folder / name);
  g_free(executable);
  g_free(directory);
  for (int session = 0; session < 2; ++session) {
    const auto before = watcher.FirstIds().size();
    GSubprocess* children[2];
    for (size_t index = 0; index < names.size(); ++index) {
      const auto path = (folder / names[index]).string();
      children[index] = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE, nullptr, path.c_str(),
                                         "--sni-child", nullptr);
      Require(children[index], "launch a separate tray application");
    }
    Require(Wait([&] { return watcher.FirstIds().size() == before + names.size(); }),
            "watcher reads both concurrent application Id properties");
    const auto all = watcher.FirstIds();
    std::vector<std::string> current(all.begin() + before, all.end());
    std::sort(current.begin(), current.end());
    Require(current == names,
            session == 0 ? "different executables expose different SNI Ids"
                         : "both applications retain their SNI Ids across process restarts");
    bool exited[2]{};
    for (size_t index = 0; index < names.size(); ++index) {
      g_output_stream_close(g_subprocess_get_stdin_pipe(children[index]), nullptr, nullptr);
      g_subprocess_wait_async(
          children[index], nullptr,
          [](GObject* process, GAsyncResult* result, gpointer data) {
            *static_cast<bool*>(data) =
                g_subprocess_wait_finish(G_SUBPROCESS(process), result, nullptr);
          },
          &exited[index]);
    }
    Require(Wait([&] { return exited[0] && exited[1]; }),
            "both application fixtures exit gracefully");
    for (auto* child : children) {
      Require(g_subprocess_get_successful(child), "application shutdown succeeds");
      g_object_unref(child);
    }
  }
  std::filesystem::remove_all(folder);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--sni-child") return ChildIcon();
  auto* bus = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(bus);
  {
    Watcher watcher;
    auto* client = Connect();
    auto* app = g_application_new("org.nativeapi.TrayIdentifierTest", G_APPLICATION_NON_UNIQUE);
    g_application_set_default(app);
    {
      auto first = std::make_unique<nativeapi::TrayIcon>();
      auto second = std::make_unique<nativeapi::TrayIcon>();
      nativeapi::TrayIcon explicit_icon(std::string("org.nativeapi.TrayIdentifierTest.sync"));
      const auto first_id = first->GetId();
      Require(first->GetIdentifier() == "org.nativeapi.TrayIdentifierTest", "default uses application ID");
      Require(second->GetIdentifier() == "org.nativeapi.TrayIdentifierTest-2", "second icon has stable suffix");
      Require(first->GetIdentifier() != second->GetIdentifier(), "live default identifiers are distinct");
      Require(first_id != second->GetId(), "numeric identity remains distinct");
      Require(Wait([&] { return watcher.FirstIds().size() == 3; }), "watcher reads each initial Id");
      const auto initial_ids = watcher.FirstIds();
      for (const auto& id : {first->GetIdentifier(), second->GetIdentifier(), explicit_icon.GetIdentifier()}) {
        Require(std::find(initial_ids.begin(), initial_ids.end(), id) != initial_ids.end(),
                "identifier fixed before watcher registration");
      }
      auto* connection = static_cast<GDBusConnection*>(first->GetNativeObject());
      const std::string destination = g_dbus_connection_get_unique_name(connection);
      Require(ReadId(client, destination.c_str()) == first->GetIdentifier(), "D-Bus Id matches getter");
      first->SetTitle("Changed display title");
      first->SetTooltip("Changed tooltip");
      first->SetVisible(false);
      first->SetVisible(true);
      Require(ReadId(client, destination.c_str()) == "org.nativeapi.TrayIdentifierTest",
              "title tooltip and visibility never change Id");
      Require(first->GetId() == first_id, "persistent name does not replace object ID");
      second.reset();
      nativeapi::TrayIcon recreated;
      Require(recreated.GetIdentifier() == "org.nativeapi.TrayIdentifierTest-2", "released suffix reused");
      Require(first->GetIdentifier() == "org.nativeapi.TrayIdentifierTest", "other icons are never renamed");
      const auto handle = native_tray_icon_create_with_identifier("org.nativeapi.TrayIdentifierTest.capi");
      Require(handle != 0, "C ABI constructor accepts explicit identifier");
      char* name = native_tray_icon_get_identifier(handle);
      Require(name && std::string(name) == "org.nativeapi.TrayIdentifierTest.capi", "C ABI string round trip");
      free_c_str(name);
      Require(Wait([&] { return watcher.FirstIds().size() == 5; }), "all registrations dispatched");
      native_tray_icon_free(handle);
    }
    {
      nativeapi::TrayIcon reserved(std::string("org.nativeapi.TrayIdentifierTest"));
      nativeapi::TrayIcon automatic;
      Require(automatic.GetIdentifier() == "org.nativeapi.TrayIdentifierTest-2", "defaults avoid explicit names");
      Require(Wait([&] { return watcher.FirstIds().size() == 7; }), "explicit and default registered");
    }
    {
      nativeapi::TrayIcon recreated;
      Require(recreated.GetIdentifier() == "org.nativeapi.TrayIdentifierTest", "single icon recreation retains base name");
      Require(Wait([&] { return watcher.FirstIds().size() == 8; }), "recreated base registered");
    }
    g_application_set_default(nullptr);
    g_object_unref(app);
    app = g_application_new("org.nativeapi.OtherTrayTest", G_APPLICATION_NON_UNIQUE);
    g_application_set_default(app);
    {
      nativeapi::TrayIcon other;
      Require(other.GetIdentifier() == "org.nativeapi.OtherTrayTest", "another application has another default");
      Require(Wait([&] { return watcher.FirstIds().size() == 9; }), "other application registered");
    }
    g_application_set_default(nullptr);
    g_object_unref(app);
    {
      nativeapi::TrayIcon fallback;
      Require(fallback.GetIdentifier() == "tray_identifier_linux_test", "executable basename fallback");
      Require(Wait([&] { return watcher.FirstIds().size() == 10; }), "fallback registered");
    }
    CheckSeparateApplications(watcher);
    g_dbus_connection_close_sync(client, nullptr, nullptr);
    g_object_unref(client);
  }
  g_test_dbus_down(bus);
  g_object_unref(bus);
  std::cout << "ALL PASS\n";
}
