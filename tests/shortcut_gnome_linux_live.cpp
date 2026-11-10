// Opt-in real GNOME compositor test; driven by tools/gui/gnome_shortcuts_test.py.
#include <gio/gio.h>
#include <iostream>
#include "../src/shortcut_manager.h"

int main() {
  auto& manager = nativeapi::ShortcutManager::GetInstance();
  manager.AddListener<nativeapi::ShortcutRegisteredEvent>(
      [](const auto& event) { std::cout << "REGISTERED " << event.GetShortcutId() << std::endl; });
  manager.AddListener<nativeapi::ShortcutRegistrationFailedEvent>(
      [](const auto& event) { std::cout << "FAILED " << event.GetShortcutId() << std::endl; });
  for (int index = 0; index < 4; ++index) {
    nativeapi::ShortcutOptions options;
    options.accelerator = "Ctrl+Alt+Shift+F" + std::to_string(9 + index);
    options.description = "Nativeapi GNOME live test " + std::to_string(index + 1);
    options.callback = [index] { std::cout << "ACTIVATED " << index + 1 << std::endl; };
    if (!manager.RequestRegistration(options, "nativeapi.test.gnome." + std::to_string(index)))
      return 1;
  }
  auto* loop = g_main_loop_new(nullptr, false);
  auto* input = g_io_channel_unix_new(0);
  g_io_add_watch(
      input, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP),
      [](GIOChannel*, GIOCondition, gpointer data) -> gboolean {
        g_main_loop_quit(static_cast<GMainLoop*>(data));
        return false;
      },
      loop);
  const auto timeout = g_timeout_add_seconds(
      90,
      [](gpointer data) -> gboolean {
        g_main_loop_quit(static_cast<GMainLoop*>(data));
        return false;
      },
      loop);
  g_main_loop_run(loop);
  g_source_remove(timeout);
  manager.UnregisterAll();
  g_io_channel_unref(input);
  g_main_loop_unref(loop);
  std::cout << "STOPPED" << std::endl;
}
