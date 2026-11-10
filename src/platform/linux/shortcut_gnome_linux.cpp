#include "shortcut_gnome_linux.h"

#include <fcntl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nativeapi::detail {
namespace {
constexpr const char* kSchema = "org.gnome.settings-daemon.plugins.media-keys";
constexpr const char* kCustom = "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding";
constexpr const char* kList = "custom-keybindings";
constexpr const char* kPrefix =
    "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/nativeapi-";
constexpr const char* kObject = "/org/nativeapi/Shortcuts";
constexpr const char* kInterface = "org.nativeapi.Shortcuts1";
constexpr const char* kXml =
    "<node><interface name='org.nativeapi.Shortcuts1'><method name='Activate'><arg name='token' "
    "type='s' direction='in'/></method></interface></node>";
using Signal = ShortcutPortalLinux::Signal;

struct ObjectDelete {
  void operator()(gpointer object) const {
    if (object)
      g_object_unref(object);
  }
};
using Setting = std::unique_ptr<GSettings, ObjectDelete>;
struct VariantDelete {
  void operator()(GVariant* value) const {
    if (value)
      g_variant_unref(value);
  }
};
using Variant = std::unique_ptr<GVariant, VariantDelete>;
std::string String(gchar* value) {
  std::string result = value ? value : "";
  g_free(value);
  return result;
}

Setting NewSettings(const char* schema, const char* path = nullptr) {
  auto* source = g_settings_schema_source_get_default();
  GSettingsSchema* found = source ? g_settings_schema_source_lookup(source, schema, true) : nullptr;
  if (!found)
    return {};
  Setting result(g_settings_new_full(found, nullptr, path));
  g_settings_schema_unref(found);
  return result;
}
std::vector<std::string> Paths(GSettings* settings) {
  gchar** values = g_settings_get_strv(settings, kList);
  std::vector<std::string> result;
  for (size_t index = 0; values && values[index]; ++index)
    result.emplace_back(values[index]);
  g_strfreev(values);
  return result;
}
bool SavePaths(GSettings* settings, const std::vector<std::string>& paths) {
  std::vector<const gchar*> values;
  for (const auto& path : paths)
    values.push_back(path.c_str());
  values.push_back(nullptr);
  return g_settings_set_strv(settings, kList, values.data());
}
std::string Canonical(const std::string& accelerator) {
  // gtk_accelerator_name/parse resolve <Primary> through the default display's
  // keymap. This worker must neither touch a GTK display nor require gtk_init.
  // GNOME's primary accelerator is Control; keys use GDK's static keysym table.
  unsigned modifiers = 0;
  size_t offset = 0;
  while (offset < accelerator.size() && accelerator[offset] == '<') {
    const auto end = accelerator.find('>', offset);
    if (end == std::string::npos)
      return {};
    auto modifier = accelerator.substr(offset + 1, end - offset - 1);
    std::transform(modifier.begin(), modifier.end(), modifier.begin(),
                   [](unsigned char ch) { return g_ascii_tolower(ch); });
    if (modifier == "control" || modifier == "ctrl" || modifier == "ctl" || modifier == "primary")
      modifiers |= GDK_CONTROL_MASK;
    else if (modifier == "shift" || modifier == "shft")
      modifiers |= GDK_SHIFT_MASK;
    else if (modifier == "alt" || modifier == "mod1")
      modifiers |= GDK_MOD1_MASK;
    else if (modifier == "super")
      modifiers |= GDK_SUPER_MASK;
    else if (modifier == "meta")
      modifiers |= GDK_META_MASK;
    else if (modifier == "hyper")
      modifiers |= GDK_HYPER_MASK;
    else if (modifier == "release")
      modifiers |= GDK_RELEASE_MASK;
    else if (modifier.size() == 4 && modifier.substr(0, 3) == "mod" && modifier[3] >= '2' &&
             modifier[3] <= '5')
      modifiers |= GDK_MOD2_MASK << (modifier[3] - '2');
    else
      return {};
    offset = end + 1;
  }
  const auto key = gdk_keyval_to_lower(gdk_keyval_from_name(accelerator.substr(offset).c_str()));
  const auto* name = gdk_keyval_name(key);
  if (!key || key == GDK_KEY_VoidSymbol || !name)
    return {};
  std::string result;
  for (const auto& modifier : {std::pair<unsigned, const char*>{GDK_RELEASE_MASK, "<Release>"},
                               {GDK_CONTROL_MASK, "<Control>"},
                               {GDK_SHIFT_MASK, "<Shift>"},
                               {GDK_MOD1_MASK, "<Alt>"},
                               {GDK_MOD2_MASK, "<Mod2>"},
                               {GDK_MOD3_MASK, "<Mod3>"},
                               {GDK_MOD4_MASK, "<Mod4>"},
                               {GDK_MOD5_MASK, "<Mod5>"},
                               {GDK_SUPER_MASK, "<Super>"},
                               {GDK_META_MASK, "<Meta>"},
                               {GDK_HYPER_MASK, "<Hyper>"}})
    if (modifiers & modifier.first)
      result += modifier.second;
  return result + name;
}
std::string StateDirectory() {
  return std::string(g_get_user_data_dir()) + "/nativeapi/gnome-shortcuts";
}
struct Lock {
  int descriptor = -1;
  Lock() {
    const auto directory = StateDirectory();
    if (g_mkdir_with_parents(directory.c_str(), 0700) != 0)
      return;
    struct stat info{};
    if (g_lstat(directory.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 0077))
      return;
    descriptor =
        g_open((directory + "/.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0)
      return;
    for (int attempt = 0; attempt < 100; ++attempt) {
      if (flock(descriptor, LOCK_EX | LOCK_NB) == 0)
        return;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    close(descriptor);
    descriptor = -1;
  }
  ~Lock() {
    if (descriptor >= 0) {
      flock(descriptor, LOCK_UN);
      close(descriptor);
    }
  }
  explicit operator bool() const { return descriptor >= 0; }
};

// A private lease makes crash recovery exact: never infer ownership from a
// user's label or accelerator. Changing the command transfers ownership away
// from nativeapi, so neither normal shutdown nor stale cleanup removes it.
struct Lease {
  std::string token, path, command, bus;
  std::string File() const { return StateDirectory() + "/" + token + ".ini"; }
  bool Owns() const {
    if (path != std::string(kPrefix) + token + "/")
      return false;
    auto settings = NewSettings(kCustom, path.c_str());
    return settings && String(g_settings_get_string(settings.get(), "command")) == command;
  }
  bool Save() const {
    GKeyFile* file = g_key_file_new();
    for (const auto& field : {std::pair<const char*, std::string>{"token", token},
                              {"path", path},
                              {"command", command},
                              {"bus", bus}})
      g_key_file_set_string(file, "Shortcut", field.first, field.second.c_str());
    gsize size = 0;
    gchar* data = g_key_file_to_data(file, &size, nullptr);
    int fd = g_open(File().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    bool success = fd >= 0;
    size_t offset = 0;
    while (success && offset < size) {
      const auto count = write(fd, data + offset, size - offset);
      if (count <= 0)
        success = false;
      else
        offset += count;
    }
    if (fd >= 0)
      close(fd);
    if (!success && fd >= 0)
      g_unlink(File().c_str());
    g_free(data);
    g_key_file_unref(file);
    return success;
  }
  static bool Load(const std::string& filename, Lease& lease) {
    struct stat info{};
    if (g_lstat(filename.c_str(), &info) || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 0077) || info.st_size > 65536)
      return false;
    GKeyFile* file = g_key_file_new();
    bool success = g_key_file_load_from_file(file, filename.c_str(), G_KEY_FILE_NONE, nullptr);
    if (success) {
      lease.token = String(g_key_file_get_string(file, "Shortcut", "token", nullptr));
      lease.path = String(g_key_file_get_string(file, "Shortcut", "path", nullptr));
      lease.command = String(g_key_file_get_string(file, "Shortcut", "command", nullptr));
      lease.bus = String(g_key_file_get_string(file, "Shortcut", "bus", nullptr));
      success = g_uuid_string_is_valid(lease.token.c_str()) && lease.File() == filename &&
                lease.path == std::string(kPrefix) + lease.token + "/" && !lease.command.empty() &&
                g_dbus_is_unique_name(lease.bus.c_str());
    }
    g_key_file_unref(file);
    return success;
  }
  bool Remove(GSettings* root) const {
    if (!Owns()) {
      g_unlink(File().c_str());
      return true;
    }
    auto paths = Paths(root);
    paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
    if (!SavePaths(root, paths))
      return false;
    auto settings = NewSettings(kCustom, path.c_str());
    for (const auto* key : {"binding", "command", "name"})
      g_settings_reset(settings.get(), key);
    g_settings_sync();
    g_unlink(File().c_str());
    return true;
  }
};

bool HasOwner(GDBusConnection* bus, const std::string& name, bool& owner) {
  GError* error = nullptr;
  Variant reply(g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
      g_variant_new("(s)", name.c_str()), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1500,
      nullptr, &error));
  if (!reply) {
    g_clear_error(&error);
    return false;
  }
  gboolean present = false;
  g_variant_get(reply.get(), "(b)", &present);
  owner = present;
  return true;
}
void Recover(GDBusConnection* bus, GSettings* root) {
  GDir* directory = g_dir_open(StateDirectory().c_str(), 0, nullptr);
  if (!directory)
    return;
  while (const auto* name = g_dir_read_name(directory)) {
    if (!g_str_has_suffix(name, ".ini"))
      continue;
    Lease lease;
    bool alive = true;
    if (Lease::Load(StateDirectory() + "/" + name, lease) && HasOwner(bus, lease.bus, alive) &&
        !alive)
      lease.Remove(root);
  }
  g_dir_close(directory);
}
std::string Conflict(const std::string& binding, GSettings* root) {
  // Standard GNOME accelerators, including extension schemas whose names
  // identify their keybinding collections. Values are read, never changed.
  gchar **fixed = nullptr, **relocatable = nullptr;
  auto* source = g_settings_schema_source_get_default();
  g_settings_schema_source_list_schemas(source, true, &fixed, &relocatable);
  for (size_t index = 0; fixed && fixed[index]; ++index) {
    const std::string id = fixed[index];
    if (id.find("org.gnome.") != 0 ||
        (id.find("keybindings") == std::string::npos && id != kSchema))
      continue;
    auto schema = g_settings_schema_source_lookup(source, id.c_str(), true);
    auto settings = NewSettings(id.c_str());
    gchar** keys = g_settings_schema_list_keys(schema);
    for (size_t key_index = 0; keys && keys[key_index]; ++key_index) {
      Variant value(g_settings_get_value(settings.get(), keys[key_index]));
      std::vector<std::string> triggers;
      if (g_variant_is_of_type(value.get(), G_VARIANT_TYPE_STRING_ARRAY)) {
        gsize length = 0;
        auto** values = g_variant_dup_strv(value.get(), &length);
        for (gsize item = 0; item < length; ++item)
          triggers.emplace_back(values[item]);
        g_strfreev(values);
      } else if (g_variant_is_of_type(value.get(), G_VARIANT_TYPE_STRING))
        triggers.emplace_back(g_variant_get_string(value.get(), nullptr));
      for (const auto& trigger : triggers)
        if (!binding.empty() && Canonical(trigger) == binding) {
          std::string reason =
              "GNOME already binds this accelerator: " + id + "/" + keys[key_index];
          g_strfreev(keys);
          g_settings_schema_unref(schema);
          g_strfreev(fixed);
          g_strfreev(relocatable);
          return reason;
        }
    }
    g_strfreev(keys);
    g_settings_schema_unref(schema);
  }
  g_strfreev(fixed);
  g_strfreev(relocatable);
  for (const auto& path : Paths(root)) {
    if (path.empty() || path.front() != '/' || path.back() != '/' ||
        path.find("//") != std::string::npos)
      continue;
    auto settings = NewSettings(kCustom, path.c_str());
    if (settings && Canonical(String(g_settings_get_string(settings.get(), "binding"))) == binding)
      return "GNOME already has a custom shortcut for this accelerator";
  }
  return {};
}
std::string Command(const std::string& bus, const std::string& token) {
  gchar* program = g_find_program_in_path("gdbus");
  if (!program)
    return {};
  std::string result;
  for (const auto& arg :
       {std::string(program), std::string("call"), std::string("--session"), std::string("--dest"),
        bus, std::string("--object-path"), std::string(kObject), std::string("--method"),
        std::string(kInterface) + ".Activate", token}) {
    if (!result.empty())
      result += ' ';
    result += String(g_shell_quote(arg.c_str()));
  }
  g_free(program);
  return result;
}
}  // namespace

struct ShortcutGnomeLinux::State : std::enable_shared_from_this<State> {
  struct Entry {
    std::weak_ptr<State> owner;
    std::shared_ptr<Shortcut> shortcut;
    std::shared_ptr<std::atomic<bool>> cancelled;
    Completion completion;
    Lease lease;
    Setting settings;
    std::string trigger;
    gulong watch = 0;
    std::atomic<bool> active{false};
    bool completed = false;
    mutable std::mutex info_mutex;
    ~Entry() {
      if (watch && settings)
        g_signal_handler_disconnect(settings.get(), watch);
    }
  };
  Listener listener;
  GMainContext* context = g_main_context_new();
  GMainLoop* loop = g_main_loop_new(context, false);
  std::thread worker;
  mutable std::mutex mutex;
  std::mutex queue_mutex;
  size_t pending = 0;
  bool cleaned = false;
  std::unordered_map<ShortcutId, std::shared_ptr<Entry>> entries;
  GDBusConnection* connection = nullptr;
  GDBusNodeInfo* info = nullptr;
  guint object = 0;
  bool started = false;
  std::atomic<bool> stopping{false};
  explicit State(Listener value) : listener(std::move(value)) {}
  ~State() {
    g_main_loop_unref(loop);
    g_main_context_unref(context);
  }
  void Post(std::function<void(State&)> fn, bool force = false) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    if (!force && stopping.load())
      return;
    ++pending;
    auto* call = new std::function<void()>([self = shared_from_this(), fn = std::move(fn)] {
      fn(*self);
      std::lock_guard<std::mutex> lock(self->queue_mutex);
      if (--self->pending == 0 && self->cleaned)
        g_main_loop_quit(self->loop);
    });
    GSource* source = g_idle_source_new();
    g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
          (*static_cast<std::function<void()>*>(data))();
          return G_SOURCE_REMOVE;
        },
        call, [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
    g_source_attach(source, context);
    g_source_unref(source);
  }
  void Start() {
    std::lock_guard<std::mutex> lock(mutex);
    if (started || stopping.load())
      return;
    started = true;
    worker = std::thread([self = shared_from_this()] {
      g_main_context_push_thread_default(self->context);
      g_main_loop_run(self->loop);
      g_main_context_pop_thread_default(self->context);
    });
  }
  std::shared_ptr<Entry> Get(ShortcutId id) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto found = entries.find(id);
    return found == entries.end() ? nullptr : found->second;
  }
  static void Method(GDBusConnection*,
                     const gchar*,
                     const gchar*,
                     const gchar*,
                     const gchar*,
                     GVariant* args,
                     GDBusMethodInvocation* invocation,
                     gpointer data) {
    auto* self = static_cast<State*>(data);
    const gchar* token = nullptr;
    g_variant_get(args, "(&s)", &token);
    std::shared_ptr<Entry> entry;
    {
      std::lock_guard<std::mutex> lock(self->mutex);
      for (const auto& item : self->entries)
        if (item.second->lease.token == token) {
          entry = item.second;
          break;
        }
    }
    if (!entry || !entry->active || entry->cancelled->load() || self->stopping.load()) {
      g_dbus_method_invocation_return_dbus_error(invocation, "org.nativeapi.Shortcuts1.Expired",
                                                 "Shortcut registration expired");
      return;
    }
    g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    self->listener(entry->shortcut->GetId(), Signal::Activated, "");
  }
  bool Connect(std::string& reason) {
    if (connection && object && !g_dbus_connection_is_closed(connection))
      return true;
    if (connection) {
      if (object)
        g_dbus_connection_unregister_object(connection, object);
      if (!g_dbus_connection_is_closed(connection))
        g_dbus_connection_close_sync(connection, nullptr, nullptr);
      g_clear_object(&connection);
      object = 0;
    }
    if (info) {
      g_dbus_node_info_unref(info);
      info = nullptr;
    }
    GError* error = nullptr;
    auto address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (address)
      connection = g_dbus_connection_new_for_address_sync(
          address,
          static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
          nullptr, nullptr, &error);
    g_free(address);
    if (!connection) {
      reason = error ? error->message : "No GNOME session bus";
      g_clear_error(&error);
      return false;
    }
    g_dbus_connection_set_exit_on_close(connection, false);
    bool daemon = false;
    if (!HasOwner(connection, "org.gnome.SettingsDaemon.MediaKeys", daemon) || !daemon) {
      reason = "GNOME media-keys service is not running";
      return false;
    }
    info = g_dbus_node_info_new_for_xml(kXml, &error);
    static const GDBusInterfaceVTable table{Method, nullptr, nullptr, {nullptr}};
    object = info ? g_dbus_connection_register_object(connection, kObject, info->interfaces[0],
                                                      &table, this, nullptr, &error)
                  : 0;
    if (!object) {
      reason = error ? error->message : "Could not export GNOME shortcut delivery";
      g_clear_error(&error);
      return false;
    }
    return true;
  }
  void Complete(const std::shared_ptr<Entry>& entry, bool success, const std::string& reason) {
    if (entry->completed)
      return;
    entry->completed = true;
    auto done = std::move(entry->completion);
    done(success, reason);
  }
  void Remove(const std::shared_ptr<Entry>& entry) {
    // Remove from the dispatch map before changing settings: their change
    // signals must not reenter the manager with an already revoked callback.
    {
      std::lock_guard<std::mutex> lock(mutex);
      entries.erase(entry->shortcut->GetId());
    }
    entry->active = false;
    if (entry->watch) {
      g_signal_handler_disconnect(entry->settings.get(), entry->watch);
      entry->watch = 0;
    }
    if (!entry->lease.token.empty()) {
      Lock lock;
      auto root = NewSettings(kSchema);
      if (lock && root)
        entry->lease.Remove(root.get());
    }
    Complete(entry, false, "GNOME shortcut registration cancelled");
  }
  void Begin(const std::shared_ptr<Entry>& entry) {
    auto fail = [&](const std::string& reason) {
      Complete(entry, false, reason);
      Remove(entry);
    };
    if (stopping.load() || entry->cancelled->load()) {
      fail("GNOME shortcut registration cancelled");
      return;
    }
    std::string reason;
    if (!Connect(reason)) {
      fail(reason);
      return;
    }
    auto root = NewSettings(kSchema);
    if (!root || !g_settings_is_writable(root.get(), kList)) {
      fail("GNOME custom shortcuts are not writable");
      return;
    }
    // Only registration/config mutations are locked. Delivery never waits on
    // the filesystem, settings daemon, or other nativeapi applications.
    {
      Lock lock;
      if (!lock) {
        fail("Could not lock GNOME shortcut registration");
        return;
      }
      Recover(connection, root.get());
      reason = Conflict(entry->trigger, root.get());
      if (!reason.empty()) {
        Complete(entry, false, reason);
      } else {
        entry->lease.token = String(g_uuid_string_random());
        entry->lease.path = std::string(kPrefix) + entry->lease.token + "/";
        entry->lease.bus = g_dbus_connection_get_unique_name(connection);
        entry->lease.command = Command(entry->lease.bus, entry->lease.token);
        entry->settings = NewSettings(kCustom, entry->lease.path.c_str());
        bool writable = entry->settings && !entry->lease.command.empty();
        for (const auto* key : {"name", "command", "binding"})
          writable = writable && g_settings_is_writable(entry->settings.get(), key);
        if (!writable || !entry->lease.Save()) {
          reason = "Could not persist the GNOME shortcut registration";
        } else {
          g_settings_delay(entry->settings.get());
          bool saved =
              g_settings_set_string(entry->settings.get(), "name",
                                    entry->shortcut->GetDescription().c_str()) &&
              g_settings_set_string(entry->settings.get(), "command",
                                    entry->lease.command.c_str()) &&
              g_settings_set_string(entry->settings.get(), "binding", entry->trigger.c_str());
          if (saved)
            g_settings_apply(entry->settings.get());
          else
            g_settings_revert(entry->settings.get());
          auto paths = Paths(root.get());
          paths.push_back(entry->lease.path);
          if (!saved || !SavePaths(root.get(), paths))
            reason = "GNOME rejected the custom shortcut settings";
          g_settings_sync();
        }
        if (!reason.empty())
          entry->lease.Remove(root.get());
      }
    }
    if (!reason.empty() || entry->cancelled->load() || stopping.load()) {
      fail(reason.empty() ? "GNOME shortcut registration cancelled" : reason);
      return;
    }
    entry->active = true;
    entry->watch = g_signal_connect(
        entry->settings.get(), "changed::binding",
        G_CALLBACK(+[](GSettings* settings, gchar*, gpointer data) {
          auto* entry = static_cast<Entry*>(data);
          auto value = String(g_settings_get_string(settings, "binding"));
          {
            std::lock_guard<std::mutex> lock(entry->info_mutex);
            entry->trigger = value;
          }
          if (auto owner = entry->owner.lock())
            owner->listener(entry->shortcut->GetId(), Signal::BindingChanged, value);
        }),
        entry.get());
    Complete(entry, true, "");
  }
};

ShortcutGnomeLinux::ShortcutGnomeLinux(Listener listener)
    : state_(std::make_shared<State>(std::move(listener))) {}
ShortcutGnomeLinux::~ShortcutGnomeLinux() {
  Shutdown();
}
bool ShortcutGnomeLinux::IsSupported() {
  if (g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS) || g_getenv("SNAP"))
    return false;
  std::string desktop = g_getenv("XDG_CURRENT_DESKTOP") ? g_getenv("XDG_CURRENT_DESKTOP") : "";
  std::transform(desktop.begin(), desktop.end(), desktop.begin(),
                 [](unsigned char ch) { return g_ascii_tolower(ch); });
  if (desktop.find("gnome") == std::string::npos && desktop.find("ubuntu") == std::string::npos)
    return false;
  gchar* program = g_find_program_in_path("gdbus");
  bool available = program != nullptr;
  g_free(program);
  return available && NewSettings(kSchema) &&
         NewSettings(
             kCustom,
             "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/nativeapi-probe/");
}
void ShortcutGnomeLinux::Request(std::shared_ptr<Shortcut> shortcut,
                                 std::string trigger,
                                 std::shared_ptr<std::atomic<bool>> cancelled,
                                 Completion completion) {
  auto entry = std::make_shared<State::Entry>();
  entry->owner = state_;
  entry->shortcut = std::move(shortcut);
  entry->trigger = Canonical(trigger);
  entry->cancelled = std::move(cancelled);
  entry->completion = std::move(completion);
  if (entry->trigger.empty()) {
    entry->completion(false, "Invalid GNOME shortcut accelerator");
    return;
  }
  state_->Start();
  bool stopped;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    stopped = state_->stopping.load();
    if (!stopped) {
      state_->entries.emplace(entry->shortcut->GetId(), entry);
      state_->Post([entry](State& state) { state.Begin(entry); });
    }
  }
  if (stopped)
    entry->completion(false, "GNOME shortcut provider is stopping");
}
void ShortcutGnomeLinux::Cancel(ShortcutId id) {
  Unregister(id);
}
bool ShortcutGnomeLinux::Unregister(ShortcutId id) {
  auto entry = state_->Get(id);
  if (!entry)
    return false;
  entry->cancelled->store(true);
  state_->Post([entry](State& state) { state.Remove(entry); });
  return true;
}
std::optional<std::string> ShortcutGnomeLinux::TriggerDescription(ShortcutId id) const {
  auto entry = state_->Get(id);
  if (!entry || !entry->active)
    return std::nullopt;
  std::lock_guard<std::mutex> lock(entry->info_mutex);
  return entry->trigger;
}
void ShortcutGnomeLinux::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(state_->queue_mutex);
    if (state_->stopping.exchange(true))
      return;
  }
  bool started;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    started = state_->started;
  }
  if (!started)
    return;
  state_->Post(
      [](State& state) {
        std::vector<std::shared_ptr<State::Entry>> entries;
        {
          std::lock_guard<std::mutex> lock(state.mutex);
          for (const auto& item : state.entries)
            entries.push_back(item.second);
        }
        for (const auto& entry : entries)
          state.Remove(entry);
        if (state.connection) {
          if (state.object)
            g_dbus_connection_unregister_object(state.connection, state.object);
          g_dbus_connection_close_sync(state.connection, nullptr, nullptr);
          g_clear_object(&state.connection);
        }
        if (state.info) {
          g_dbus_node_info_unref(state.info);
          state.info = nullptr;
        }
        state.cleaned = true;
      },
      true);
  if (state_->worker.joinable())
    state_->worker.join();
}
}  // namespace nativeapi::detail
