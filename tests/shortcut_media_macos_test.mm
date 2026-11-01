// No keyboard events are posted to the OS. Native AppKit dispatch stays in
// this process; the Quartz callback is called directly with a process-local event.
#import <Cocoa/Cocoa.h>
#include <iostream>
#include <vector>
#include <utility>
#include <thread>
#include <chrono>
#include "../src/platform/macos/shortcut_media_macos.h"
#include "../src/capi/shortcut_manager_c.h"

namespace nativeapi::detail {
struct MacMediaShortcutsTestPeer {
  static void RevokePermission(MacMediaShortcuts& monitor) {
    auto state = monitor.global_.state;
    { std::lock_guard<std::mutex> lock(state->mutex);
      state->permission_required = true;
      state->permission_check = [] { return false; };
    }
    state->CheckPermission();
  }
  static bool PermissionLost(MacMediaShortcuts& monitor) {
    auto state = monitor.global_.state;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->permission_lost;
  }
  static void StartBlockedPermissionCheck(MacMediaShortcuts& monitor, std::atomic<bool>& entered, std::atomic<bool>& release) {
    auto state = monitor.global_.state;
    state->permission_required = true;
    state->permission_check = [&] {
      entered.store(true);
      while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return true;
    };
    state->CheckPermission();
  }
  static void DisableWhileChecking(MacMediaShortcuts& monitor) {
    CGEventTapEnable(monitor.global_.tap, false);
    MacMediaShortcuts::TapCallback(nullptr, kCGEventTapDisabledByTimeout, nullptr, monitor.global_.state.get());
  }
  static bool TapEnabled(MacMediaShortcuts& monitor) {
    return monitor.global_.tap && CGEventTapIsEnabled(monitor.global_.tap);
  }
  static bool HasGlobalTap(const MacMediaShortcuts& monitor) { return monitor.global_.tap != nullptr; }
  static bool Route(MacMediaShortcuts& monitor, NSEvent* event) {
    return MacMediaShortcuts::TapCallback(nullptr, static_cast<CGEventType>(NX_SYSDEFINED), event.CGEvent, monitor.global_.state.get()) == nullptr;
  }
  static void DecoderOnlyGlobal(MacMediaShortcuts& monitor, ShortcutId id, uint32_t key, UInt32 modifiers) {
    if (!monitor.global_.state) monitor.global_.state = std::make_shared<MacMediaShortcuts::State>(monitor.delivery_);
    monitor.global_.state->entries.emplace(id, MacMediaShortcuts::Entry{key, modifiers, ShortcutScope::Global, false, NSProcessInfo.processInfo.systemUptime});
  }
};
}
namespace {
int failures = 0;
void Check(bool value, const char* name) {
  std::cout << (value ? "PASS: " : "FAIL: ") << name << '\n';
  if (!value) ++failures;
}
NSEvent* Media(int code, bool down, NSEventModifierFlags flags = 0, bool repeat = false, short subtype = 8) {
  return [NSEvent otherEventWithType:NSEventTypeSystemDefined location:NSZeroPoint
      modifierFlags:flags timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:0 context:nil
      subtype:subtype data1:(code << 16) | ((down ? NX_KEYDOWN : NX_KEYUP) << 8) | (repeat ? 1 : 0) data2:0];
}
void Pump() { for (int i = 0; i < 4; ++i) nativeapi::RunMainThreadLoopFor(5); }
}
int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    using namespace nativeapi;
    using namespace nativeapi::detail;
    uint32_t code = 0;
    Check(LookupMacMediaKey("mediaplaypause", code) && code == NX_KEYTYPE_PLAY, "named media key maps to NX media code");
    Check(LookupMacMediaKey("nativemediakey0", code) && code == NX_KEYTYPE_SOUND_UP, "media zero is volume up, not an invalid Carbon fallback");
    Check(!LookupMacMediaKey("nativemediakey9999999999", code) &&
          !LookupMacMediaKey("nativemediakey16junk", code) && !LookupMacMediaKey("nativemediakey999", code),
          "reject overflow, partial and unsupported media codes");
    std::vector<std::pair<ShortcutId, int>> routed;
    MacMediaShortcuts transport([&](ShortcutId id, int phase) { routed.emplace_back(id, phase); return true; });
    const bool tap_ready = transport.Add(100, NX_KEYTYPE_PLAY, controlKey, ShortcutScope::Global);
    std::cout << "GLOBAL_TAP_CREATED=" << tap_ready << '\n';
    if (!tap_ready) {
      // Headless CI lacks input/accessibility consent. Still test the exact
      // production callback decoder; this does NOT certify OS tap installation.
      MacMediaShortcutsTestPeer::DecoderOnlyGlobal(transport, 100, NX_KEYTYPE_PLAY, controlKey);
    }
    Check(!MacMediaShortcutsTestPeer::Route(transport, Media(NX_KEYTYPE_PLAY, true)), "unmatched modifier passes through");
    Check(!MacMediaShortcutsTestPeer::Route(transport, Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl, false, 7)), "ignore other system event subtypes");
    Check(MacMediaShortcutsTestPeer::Route(transport, Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl)), "global tap callback consumes matched down");
    Check(MacMediaShortcutsTestPeer::Route(transport, Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl, true)), "global repeat is recognized separately");
    Check(MacMediaShortcutsTestPeer::Route(transport, Media(NX_KEYTYPE_PLAY, false)), "global up survives modifier release");
    Check(routed == std::vector<std::pair<ShortcutId,int>>{{100,1},{100,2},{100,0}}, "tap callback preserves identity and phases");
    Check(!transport.Add(101, NX_KEYTYPE_PLAY, controlKey, ShortcutScope::Global), "canonical duplicate cannot steal a registered chord");
    Check(transport.Add(102, NX_KEYTYPE_PLAY, controlKey, ShortcutScope::Application), "global and application scopes coexist");
    Check(transport.Remove(100), "release global registration");
    Check(!MacMediaShortcutsTestPeer::HasGlobalTap(transport), "global tap is released even while application scope remains");
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl)];
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, false)];
    Check(routed.size() == 5 && routed[3] == std::make_pair<ShortcutId,int>(102,1) && routed[4] == std::make_pair<ShortcutId,int>(102,0), "application monitor survives global transport cleanup");
    Check(transport.Remove(102), "release the remaining application monitor");

    auto& manager = ShortcutManager::GetInstance();
    ShortcutOptions options;
    options.accelerator = "Ctrl+NativeMediaKey16";
    options.scope = ShortcutScope::Application;
    int invokes = 0;
    options.callback = [&] { ++invokes; };
    auto shortcut = manager.Register(options);
    Check(shortcut != nullptr, "real AppKit application registration requires no global input permission");
    if (!shortcut) return 1;
    std::vector<native_shortcut_event_type_t> events;
    auto listener = native_shortcut_manager_add_listener([](const native_shortcut_event_t* event, void* context) {
      if (event->type == NATIVE_SHORTCUT_EVENT_TYPE_ACTIVATED || event->type == NATIVE_SHORTCUT_EVENT_TYPE_RELEASED)
        static_cast<std::vector<native_shortcut_event_type_t>*>(context)->push_back(event->type);
    }, &events, nullptr);
    Pump();
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl | NSEventModifierFlagCapsLock)];
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl, true)];
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, false)];
    Pump();
    Check(invokes == 1 && events == std::vector<native_shortcut_event_type_t>{NATIVE_SHORTCUT_EVENT_TYPE_ACTIVATED, NATIVE_SHORTCUT_EVENT_TYPE_RELEASED},
          "AppKit native event -> manager callback -> C ABI down/up (repeat suppressed, CapsLock ignored)");
    shortcut->SetEnabled(false);
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl)];
    shortcut->SetEnabled(true);
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl, true)];
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, false)];
    Pump();
    Check(events.size() == 2 && invokes == 1, "re-enabling a held media key does not emit an orphan release");
    manager.SetEnabled(false);
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl)];
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, false)];
    manager.SetEnabled(true);
    Pump();
    Check(events.size() == 2 && invokes == 1, "disabled manager leaves media events unhandled");
    Check(manager.Unregister(shortcut->GetId()), "remove real AppKit registration");
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, true, NSEventModifierFlagControl)];
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, false)];
    Pump();
    Check(events.size() == 2 && invokes == 1, "cleanup removes queued media callbacks");
    native_shortcut_manager_remove_listener(listener);
    // Source registration on a worker thread must not synchronously wait on UI.
    bool worker_ok = false;
    std::thread worker([&] {
      auto key = manager.Register(options);
      worker_ok = key && manager.Unregister(key->GetId());
    });
    worker.join(); Pump();
    Check(worker_ok && manager.GetAll().empty(), "worker register/cleanup does not deadlock under manager lock");
    std::vector<std::pair<ShortcutId,int>> focus_events;
    MacMediaShortcuts focus([&](ShortcutId id, int phase) { focus_events.emplace_back(id,phase); return true; });
    Check(focus.Add(400, NX_KEYTYPE_PLAY, 0, ShortcutScope::Application), "application focus fixture registered");
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, true)];
    [NSNotificationCenter.defaultCenter postNotificationName:NSApplicationDidResignActiveNotification object:NSApp];
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, true)];
    [NSApp sendEvent:Media(NX_KEYTYPE_PLAY, false)];
    Check(focus_events == std::vector<std::pair<ShortcutId,int>>{{400,1},{400,0},{400,1},{400,0}},
          "application blur releases held media state and preserves the next chord");
    focus.Remove(400);

    MacMediaShortcuts recovery([](ShortcutId, int) { return true; });
    if (recovery.Add(300, NX_KEYTYPE_PLAY, 0, ShortcutScope::Global)) {
      std::atomic<bool> entered{false}, release{false};
      MacMediaShortcutsTestPeer::StartBlockedPermissionCheck(recovery, entered, release);
      for (int i = 0; i < 200 && !entered.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      MacMediaShortcutsTestPeer::DisableWhileChecking(recovery);
      release.store(true);
      for (int i = 0; i < 100 && !MacMediaShortcutsTestPeer::TapEnabled(recovery); ++i) Pump();
      Check(MacMediaShortcutsTestPeer::TapEnabled(recovery), "tap timeout during an in-flight permission check is re-enabled without losing the request");
      recovery.Remove(300);
    }
    int lost = 0;
    MacMediaShortcuts* current = nullptr;
    MacMediaShortcuts revocation([&](ShortcutId id, int phase) {
      if (phase == 3) { ++lost; current->Remove(id); }
      return true;
    });
    current = &revocation;
    MacMediaShortcutsTestPeer::DecoderOnlyGlobal(revocation, 200, NX_KEYTYPE_PLAY, 0);
    MacMediaShortcutsTestPeer::RevokePermission(revocation);
    for (int i = 0; i < 200 && !MacMediaShortcutsTestPeer::PermissionLost(revocation); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(!revocation.Add(201, NX_KEYTYPE_NEXT, 0, ShortcutScope::Global), "permission loss rejects registrations racing queued cleanup");
    for (int i = 0; i < 100 && lost == 0; ++i) Pump();
    Check(lost == 1 && !MacMediaShortcutsTestPeer::HasGlobalTap(revocation), "permission revocation retires registration on UI without changing OS permissions");
    return failures ? 1 : 0;
  }
}
