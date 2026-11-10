// An isolated session bus + in-memory settings, never the user's keybindings.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/platform/linux/shortcut_gnome_linux.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "../src/shortcut_manager.h"

using nativeapi::detail::ShortcutGnomeLinux;
using nativeapi::detail::ShortcutPortalLinux;
namespace {
constexpr auto schema = "org.gnome.settings-daemon.plugins.media-keys";
constexpr auto custom = "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding";
template <class Predicate>
void Wait(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    while (g_main_context_iteration(nullptr, false)) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(predicate());
}
std::vector<std::string> Paths(GSettings* root) {
  gchar** list = g_settings_get_strv(root, "custom-keybindings");
  std::vector<std::string> paths;
  for (size_t i = 0; list[i]; ++i)
    paths.emplace_back(list[i]);
  g_strfreev(list);
  return paths;
}
std::string Value(GSettings* settings, const char* key) {
  auto* value = g_settings_get_string(settings, key);
  std::string result = value;
  g_free(value);
  return result;
}
struct Result {
  std::atomic<bool> done{false}, success{false};
  std::string error;
};
std::shared_ptr<std::atomic<bool>> Register(ShortcutGnomeLinux& backend,
                                            uint32_t id,
                                            const std::string& trigger,
                                            Result& result) {
  nativeapi::ShortcutOptions options;
  options.accelerator = trigger;
  options.description = "Test Shortcut";
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  backend.Request(std::make_shared<nativeapi::Shortcut>(id, options), trigger, cancelled,
                  [&result](bool success, const std::string& error) {
                    result.error = error;
                    result.success = success;
                    result.done = true;
                  });
  return cancelled;
}
}  // namespace
int main() {
  g_setenv("GSETTINGS_BACKEND", "memory", true);
  g_setenv("XDG_CURRENT_DESKTOP", "GNOME", true);
  auto* directory = g_dir_make_tmp("nativeapi-gnome-test-XXXXXX", nullptr);
  g_setenv("XDG_DATA_HOME", directory, true);
  assert(std::string(g_get_user_data_dir()) == directory);
  auto* session = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(session);
  auto* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  assert(bus);
  auto* reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
      g_variant_new("(su)", "org.gnome.SettingsDaemon.MediaKeys", 0), G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
  assert(reply);
  g_variant_unref(reply);
  auto* root = g_settings_new(schema);
  const char* user_path =
      "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/user-owned/";
  auto* user = g_settings_new_with_path(custom, user_path);
  g_settings_set_string(user, "name", "Personal command");
  g_settings_set_string(user, "command", "/usr/bin/true");
  g_settings_set_string(user, "binding", "<Control><Alt><Shift>F8");
  const gchar* user_paths[] = {user_path, nullptr};
  g_settings_set_strv(root, "custom-keybindings", user_paths);
  std::atomic<int> activated{0};
  ShortcutGnomeLinux backend([&](auto id, auto signal, const auto&) {
    assert(id == 1);
    if (signal == ShortcutPortalLinux::Signal::Activated)
      ++activated;
  });
  assert(ShortcutGnomeLinux::IsSupported());
  Result result;
  Register(backend, 1, "<Control><Alt><Shift>F9", result);
  Wait([&] { return result.done.load(); });
  assert(result.success && Paths(root).size() == 2);
  auto paths = Paths(root);
  const auto path = paths[0] == user_path ? paths[1] : paths[0];
  auto* owned = g_settings_new_with_path(custom, path.c_str());
  auto command = Value(owned, "command");
  GError* error = nullptr;
  assert(g_spawn_command_line_async(command.c_str(), &error));
  Wait([&] { return activated.load() == 1; });
  assert(backend.TriggerDescription(1).value() == Value(owned, "binding"));
  std::cout << "PASS native command returns activation to its live callback\n";
  Result conflict;
  Register(backend, 2, "<Control><Alt><Shift>F8", conflict);
  Wait([&] { return conflict.done.load(); });
  assert(!conflict.success && conflict.error.find("already") != std::string::npos);
  g_settings_set_string(user, "binding", "<Primary><Alt><Shift>F8");
  Result primary;
  Register(backend, 7, "<Control><Alt><Shift>F8", primary);
  Wait([&] { return primary.done.load(); });
  assert(!primary.success);
  Result builtin;
  Register(backend, 3, "<Alt>F4", builtin);
  Wait([&] { return builtin.done.load(); });
  assert(!builtin.success && Paths(root).size() == 2);
  std::cout << "PASS existing custom and built-in GNOME shortcuts are preserved\n";
  Result cancelled;
  auto cancel = Register(backend, 4, "<Control><Alt><Shift>F10", cancelled);
  cancel->store(true);
  backend.Cancel(4);
  Wait([&] { return cancelled.done.load(); });
  assert(!cancelled.success);
  assert(backend.Unregister(1));
  Wait([&] { return Paths(root).size() == 1; });
  assert(Value(user, "command") == "/usr/bin/true");
  assert(g_spawn_command_line_async(command.c_str(), &error));
  for (int i = 0; i < 20; ++i) {
    while (g_main_context_iteration(nullptr, false)) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(activated.load() == 1);
  std::cout << "PASS cancellation/unregister revoke delivery and remove only owned paths\n";
  Result edited;
  Register(backend, 5, "<Control><Alt><Shift>F11", edited);
  Wait([&] { return edited.done.load(); });
  assert(edited.success);
  const auto after = Paths(root);
  auto edited_path = after[0] == user_path ? after[1] : after[0];
  auto* modified = g_settings_new_with_path(custom, edited_path.c_str());
  g_settings_set_string(modified, "command", "/usr/bin/false");
  backend.Shutdown();
  assert(Paths(root).size() == 2 && Value(modified, "command") == "/usr/bin/false");
  std::cout << "PASS shutdown preserves a command changed by its user\n";
  // Exercise automatic selection through the real public manager when this
  // private session bus has no GlobalShortcuts portal at all.
  g_setenv("WAYLAND_DISPLAY", "nativeapi-test", true);
  g_unsetenv("DISPLAY");
  auto& manager = nativeapi::ShortcutManager::GetInstance();
  std::atomic<int> registered{0}, callbacks{0};
  const auto listener = manager.AddListener<nativeapi::ShortcutEvent>([&](const auto& event) {
    if (dynamic_cast<const nativeapi::ShortcutRegisteredEvent*>(&event))
      ++registered;
  });
  nativeapi::ShortcutOptions options;
  options.accelerator = "Ctrl+Alt+Shift+F12";
  options.description = "Manager fallback";
  options.callback = [&] { ++callbacks; };
  const auto before = Paths(root);
  const auto id = manager.RequestRegistration(options, "nativeapi.test.gnome.manager");
  assert(id);
  Wait([&] { return registered.load() == 1; });
  std::string manager_path;
  for (const auto& candidate : Paths(root))
    if (std::find(before.begin(), before.end(), candidate) == before.end())
      manager_path = candidate;
  assert(!manager_path.empty());
  auto* manager_settings = g_settings_new_with_path(custom, manager_path.c_str());
  const auto manager_command = Value(manager_settings, "command");
  assert(g_spawn_command_line_async(manager_command.c_str(), &error));
  Wait([&] { return callbacks.load() == 1; });
  assert(manager.Unregister(id));
  Wait([&] { return Paths(root) == before; });
  manager.RemoveListener(listener);
  g_object_unref(manager_settings);
  std::cout << "PASS public manager falls back only after missing portal capability\n";

  // Model a previous process dying, including its private lease. Recovery
  // must remove exactly its still-owned path and retain unrelated commands.
  const std::string nonce = "2c08db17-e2c2-4f3a-aed5-6061997dc506";
  const auto stale_path =
      std::string("/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/nativeapi-") +
      nonce + "/";
  auto* stale = g_settings_new_with_path(custom, stale_path.c_str());
  g_settings_set_string(stale, "command", "/usr/bin/true");
  g_settings_set_string(stale, "binding", "<Control><Alt><Shift>F13");
  std::vector<const gchar*> with_stale;
  for (const auto& item : before)
    with_stale.push_back(item.c_str());
  with_stale.push_back(stale_path.c_str());
  with_stale.push_back(nullptr);
  g_settings_set_strv(root, "custom-keybindings", with_stale.data());
  const auto lease_file = std::string(directory) + "/nativeapi/gnome-shortcuts/" + nonce + ".ini";
  const auto lease_data = "[Shortcut]\ntoken=" + nonce + "\npath=" + stale_path +
                          "\ncommand=/usr/bin/true\nbus=:99999.99999\n";
  assert(g_file_set_contents(lease_file.c_str(), lease_data.c_str(), -1, nullptr));
  g_chmod(lease_file.c_str(), 0600);
  ShortcutGnomeLinux recovering([](auto, auto, const auto&) {});
  Result recovered;
  Register(recovering, 6, "<Control><Alt><Shift>F14", recovered);
  Wait([&] { return recovered.done.load(); });
  assert(recovered.success);
  assert(Value(stale, "command").empty());
  recovering.Shutdown();
  assert(Paths(root) == before);
  assert(Value(user, "command") == "/usr/bin/true");
  assert(Value(modified, "command") == "/usr/bin/false");
  g_object_unref(stale);
  std::cout << "PASS crash recovery prunes only a dead owner's unmodified command\n";
  g_object_unref(modified);
  g_object_unref(owned);
  g_object_unref(user);
  g_object_unref(root);
  g_dbus_connection_close_sync(bus, nullptr, nullptr);
  g_object_unref(bus);
  g_test_dbus_down(session);
  g_object_unref(session);
  g_free(directory);
}
