// Exercises our Carbon handler without posting keyboard input to the desktop.
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../src/shortcut_manager.h"
#include "../src/capi/shortcut_manager_c.h"
#include "../src/foundation/dispatcher.h"

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
  std::cout << (condition ? "PASS: " : "FAIL: ") << message << '\n';
  if (!condition) ++failures;
}

OSStatus SendHotKey(nativeapi::ShortcutId id, UInt32 kind, FourCharCode signature = 'ntap') {
  EventRef event = nullptr;
  OSStatus result = CreateEvent(nullptr, kEventClassKeyboard, kind, GetCurrentEventTime(),
                                kEventAttributeNone, &event);
  if (result != noErr) return result;
  EventHotKeyID hotkey = {signature, static_cast<UInt32>(id)};
  SetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, sizeof(hotkey), &hotkey);
  result = SendEventToEventTarget(event, GetApplicationEventTarget());
  ReleaseEvent(event);
  nativeapi::RunMainThreadLoopFor(30);
  return result;
}
struct Delivery {
  native_shortcut_event_type_t type;
  native_shortcut_id_t id;
  std::string accelerator;
};
}  // namespace

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    auto& manager = nativeapi::ShortcutManager::GetInstance();
    for (int key : {81, 71, 10, 72, 73, 74}) {
      auto native = manager.Register("Ctrl+Shift+NativeKey" + std::to_string(key), [] {});
      Check(native != nullptr, "register a legacy native Carbon keycode");
      if (native) Check(manager.Unregister(native->GetId()), "release native keycode");
    }
    int invocations = 0;
    auto shortcut = manager.Register("Ctrl+Shift+F19", [&] { ++invocations; });
    Check(shortcut != nullptr, "register a real Carbon hotkey");
    if (!shortcut) return EXIT_FAILURE;
    nativeapi::RunMainThreadLoopFor(30);

    std::vector<Delivery> events;
    auto listener = native_shortcut_manager_add_listener(
        [](const native_shortcut_event_t* event, void* data) {
          if (event->type != NATIVE_SHORTCUT_EVENT_TYPE_ACTIVATED &&
              event->type != NATIVE_SHORTCUT_EVENT_TYPE_RELEASED) return;
          static_cast<std::vector<Delivery>*>(data)->push_back(
              {event->type, event->shortcut_id, event->accelerator});
        }, &events, nullptr);
    Check(SendHotKey(shortcut->GetId(), kEventHotKeyPressed) == noErr, "handle press");
    Check(SendHotKey(shortcut->GetId(), kEventHotKeyReleased) == noErr, "handle release");
    Check(invocations == 1, "activation callback runs on press only");
    Check(events.size() == 2 && events[0].type == NATIVE_SHORTCUT_EVENT_TYPE_ACTIVATED &&
              events[1].type == NATIVE_SHORTCUT_EVENT_TYPE_RELEASED,
          "C ABI delivers down then up");
    Check(events.size() == 2 && events[1].id == shortcut->GetId() &&
              events[1].accelerator == "Ctrl+Shift+F19", "release payload retains identity");

    shortcut->SetEnabled(false);
    SendHotKey(shortcut->GetId(), kEventHotKeyPressed);
    SendHotKey(shortcut->GetId(), kEventHotKeyReleased);
    Check(events.size() == 2 && invocations == 1, "disabled shortcut suppresses both events");
    shortcut->SetEnabled(true);
    manager.SetEnabled(false);
    SendHotKey(shortcut->GetId(), kEventHotKeyPressed);
    SendHotKey(shortcut->GetId(), kEventHotKeyReleased);
    Check(events.size() == 2 && invocations == 1, "disabled manager suppresses both events");
    manager.SetEnabled(true);

    Check(SendHotKey(shortcut->GetId(), kEventHotKeyReleased, 'host') == eventNotHandledErr,
          "leave host hotkeys to the host");
    Check(events.size() == 2, "host hotkey emits no library event");
    manager.Unregister(shortcut->GetId());
    nativeapi::RunMainThreadLoopFor(30);
    events.clear();
    SendHotKey(shortcut->GetId(), kEventHotKeyPressed);
    SendHotKey(shortcut->GetId(), kEventHotKeyReleased);
    Check(events.empty() && invocations == 1, "unregistered shortcut emits no queued events");
    native_shortcut_manager_remove_listener(listener);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }
}
