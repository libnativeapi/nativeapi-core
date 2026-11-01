#pragma once

// Private AppKit/Quartz transport. Not part of the generated public API.
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <IOKit/hidsystem/ev_keymap.h>
#import <IOKit/hidsystem/IOLLEvent.h>
#import <IOKit/hidsystem/IOHIDLib.h>

#include <charconv>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include "../../shortcut_manager.h"
#include "../../foundation/dispatcher.h"

namespace nativeapi::detail {

inline uint32_t CanonicalMacMediaKey(uint32_t code) {
  // Apple transport keys use FAST/REWIND; other keyboards send NEXT/PREVIOUS.
  if (code == NX_KEYTYPE_FAST) return NX_KEYTYPE_NEXT;
  if (code == NX_KEYTYPE_REWIND) return NX_KEYTYPE_PREVIOUS;
  return code;
}

inline bool LookupMacMediaKey(const std::string& token, uint32_t& code) {
  static const std::unordered_map<std::string, uint32_t> keys = {
      {"mediaplaypause", NX_KEYTYPE_PLAY}, {"medianexttrack", NX_KEYTYPE_NEXT},
      {"mediaprevioustrack", NX_KEYTYPE_PREVIOUS}, {"mediafastforward", NX_KEYTYPE_FAST},
      {"mediarewind", NX_KEYTYPE_REWIND}, {"volumeup", NX_KEYTYPE_SOUND_UP},
      {"volumedown", NX_KEYTYPE_SOUND_DOWN}, {"volumemute", NX_KEYTYPE_MUTE},
      {"brightnessup", NX_KEYTYPE_BRIGHTNESS_UP}, {"brightnessdown", NX_KEYTYPE_BRIGHTNESS_DOWN},
      {"eject", NX_KEYTYPE_EJECT},
  };
  auto found = keys.find(token);
  if (found != keys.end()) { code = CanonicalMacMediaKey(found->second); return true; }
  constexpr size_t prefix_length = 14;
  if (token.compare(0, prefix_length, "nativemediakey") != 0 || token.size() == prefix_length) return false;
  auto first = token.data() + prefix_length;
  auto last = token.data() + token.size();
  if (*first < '0' || *first > '9') return false;
  uint32_t raw = 0;
  auto parsed = std::from_chars(first, last, raw);
  if (parsed.ec != std::errc() || parsed.ptr != last) return false;
  for (const auto& key : keys) {
    if (key.second == raw) { code = CanonicalMacMediaKey(raw); return true; }
  }
  return false;
}

inline UInt32 MacMediaModifiers(NSEventModifierFlags flags) {
  UInt32 result = 0;
  if (flags & NSEventModifierFlagShift) result |= shiftKey;
  if (flags & NSEventModifierFlagControl) result |= controlKey;
  if (flags & NSEventModifierFlagOption) result |= optionKey;
  if (flags & NSEventModifierFlagCommand) result |= cmdKey;
  if (flags & NSEventModifierFlagCapsLock) result |= alphaLock;
  if (flags & NSEventModifierFlagFunction) result |= kEventKeyModifierFnMask;
  return result;
}

class MacMediaShortcuts {
 public:
  // phase: 1 = down, 0 = up, 2 = repeat, 3 = native permission loss.
  using Delivery = std::function<bool(ShortcutId, int)>;
  explicit MacMediaShortcuts(Delivery delivery) : delivery_(std::move(delivery)) {}
  ~MacMediaShortcuts() { Close(); }

  bool Add(ShortcutId id, uint32_t code, UInt32 modifiers, ShortcutScope scope) {
    std::lock_guard<std::mutex> resource_lock(resources_mutex_);
    auto& group = scope == ShortcutScope::Global ? global_ : application_;
    if (!group.state) group.state = std::make_shared<State>(delivery_);
    auto state = group.state;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (!state->active || state->permission_lost) return false;
      for (const auto& item : state->entries) {
        const auto& entry = item.second;
        if (entry.code == code && entry.modifiers == modifiers) return false;
      }
    }
    if (scope == ShortcutScope::Global && !group.tap) {
      group.tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
          kCGEventTapOptionDefault, CGEventMaskBit(NX_SYSDEFINED), TapCallback, state.get());
      if (!group.tap) { RetireIfEmpty(group); return false; }
      group.source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, group.tap, 0);
      if (!group.source) { CFRelease(group.tap); group.tap = nullptr; RetireIfEmpty(group); return false; }
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->tap = group.tap;
        if (@available(macOS 10.15, *)) {
          state->permission_required = IOHIDCheckAccess(kIOHIDRequestTypePostEvent) == kIOHIDAccessTypeGranted;
          if (!state->permission_required && IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted) {
            state->permission_required = true;
            state->permission_type = kIOHIDRequestTypeListenEvent;
          }
        }
        state->application = application_.state;
      }
      CFRunLoopAddSource(CFRunLoopGetMain(), group.source, kCFRunLoopCommonModes);
      CGEventTapEnable(group.tap, true);
      if (!CGEventTapIsEnabled(group.tap)) { Retire(group); return false; }
    }
    if (!group.local) {
      group.local = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskSystemDefined
          handler:^NSEvent*(NSEvent* event) {
            state->Handle(event, scope);
            return event;
          }];
      if (!group.local) { RetireIfEmpty(group); return false; }
      if (scope == ShortcutScope::Application) {
        group.focus_observer = [NSNotificationCenter.defaultCenter
            addObserverForName:NSApplicationDidResignActiveNotification object:NSApp queue:nil
            usingBlock:^(NSNotification*) { state->ReleaseHeld(); }];
      }
      if (scope == ShortcutScope::Application && global_.state) {
        std::lock_guard<std::mutex> lock(global_.state->mutex);
        global_.state->application = state;
      }
    }
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (!state->active || state->permission_lost) return false;
      state->entries.emplace(id, Entry{code, modifiers, scope, false, NSProcessInfo.processInfo.systemUptime});
    }
    return true;
  }

  bool Remove(ShortcutId id) {
    std::lock_guard<std::mutex> resource_lock(resources_mutex_);
    for (auto* group : {&global_, &application_}) {
      if (!group->state) continue;
      bool removed;
      { std::lock_guard<std::mutex> lock(group->state->mutex); removed = group->state->entries.erase(id) != 0; }
      if (removed) { RetireIfEmpty(*group); return true; }
    }
    return false;
  }

 private:
  friend struct MacMediaShortcutsTestPeer;
  struct Entry { uint32_t code; UInt32 modifiers; ShortcutScope scope; bool held; double registered_at; };
  struct State : std::enable_shared_from_this<State> {
    explicit State(Delivery value) : delivery(std::move(value)) {
      permission_check = [this] {
        if (@available(macOS 10.15, *))
          return IOHIDCheckAccess(permission_type) == kIOHIDAccessTypeGranted;
        return true;
      };
    }
    std::mutex mutex;
    bool active = true;
    bool permission_required = false;
    bool permission_lost = false;
    std::atomic<bool> permission_check_running{false};
    std::atomic<bool> reenable_requested{false};
    IOHIDRequestType permission_type = kIOHIDRequestTypePostEvent;
    std::function<bool()> permission_check;
    CFMachPortRef tap = nullptr; // Owned by the transport; pin under mutex before using.
    std::unordered_map<ShortcutId, Entry> entries;
    std::weak_ptr<State> application;
    Delivery delivery;

    void CheckPermission(bool reenable = false) {
      { std::lock_guard<std::mutex> lock(mutex);
        if (!active || permission_lost || !permission_required) return;
      }
      if (reenable) reenable_requested.store(true);
      if (permission_check_running.exchange(true)) return;
      auto state = shared_from_this();
      dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        const bool granted = state->permission_check();
        if (!granted) {
          CFMachPortRef tap = nullptr;
          std::vector<ShortcutId> ids;
          { std::lock_guard<std::mutex> lock(state->mutex);
            if (state->active) {
              state->permission_lost = true;
              if (state->tap) { tap = state->tap; CFRetain(tap); }
              for (const auto& item : state->entries) ids.push_back(item.first);
            }
          }
          // Stop the tap before scheduling manager cleanup. Do not re-enable a
          // default tap after its Accessibility permission has been revoked.
          if (tap) { CGEventTapEnable(tap, false); CFMachPortInvalidate(tap); CFRelease(tap); }
          RunOnMainThread([state, ids] {
            for (auto id : ids) {
              { std::lock_guard<std::mutex> lock(state->mutex); if (!state->active) break; }
              state->delivery(id, 3);
            }
          });
        }
        state->permission_check_running.store(false);
        if (granted && state->reenable_requested.exchange(false)) {
          RunOnMainThread([state] {
            CFMachPortRef tap = nullptr;
            { std::lock_guard<std::mutex> lock(state->mutex);
              if (state->active && !state->permission_lost && state->tap) { tap = state->tap; CFRetain(tap); }
            }
            if (tap) { CGEventTapEnable(tap, true); CFRelease(tap); }
          });
        }
      });
    }

    void ReleaseHeld() {
      auto lifetime = shared_from_this();
      std::vector<ShortcutId> held;
      { std::lock_guard<std::mutex> lock(mutex);
        if (!active) return;
        for (auto& item : entries) {
          if (item.second.held) { held.push_back(item.first); item.second.held = false; }
        }
      }
      // Application deactivation logically releases held app keys; otherwise a
      // missing physical up would suppress the first key-down after refocus.
      for (auto id : held) delivery(id, 0);
    }

    bool Handle(NSEvent* event, ShortcutScope scope) {
      auto lifetime = shared_from_this();
      if (!event || event.type != NSEventTypeSystemDefined ||
          event.subtype != NX_SUBTYPE_AUX_CONTROL_BUTTONS) return false;
      const uint32_t data = static_cast<uint32_t>(event.data1);
      const uint32_t code = CanonicalMacMediaKey(data >> 16);
      const uint32_t status = (data >> 8) & 0xff;
      if (status != NX_KEYDOWN && status != NX_KEYUP) return false;
      const bool down = status == NX_KEYDOWN;
      const bool repeat = (data & 1) != 0;
      const auto modifiers = MacMediaModifiers(event.modifierFlags);
      std::vector<std::pair<ShortcutId, int>> matches;
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (!active || permission_lost) return false;
        for (auto& item : entries) {
          auto& entry = item.second;
          if (entry.scope != scope || entry.code != code || event.timestamp < entry.registered_at) continue;
          if (down) {
            const auto effective = (entry.modifiers & alphaLock) ? modifiers : (modifiers & ~alphaLock);
            if (entry.modifiers != effective) continue;
            // A repeat first observed after enabling must not arm an orphan up.
            const int phase = (repeat || entry.held) ? 2 : 1;
            if (!repeat) entry.held = true;
            matches.emplace_back(item.first, phase);
          } else if (entry.held) {
            // Releasing modifiers first must not lose the primary key's up.
            entry.held = false;
            matches.emplace_back(item.first, 0);
          }
        }
      }
      bool consumed = false;
      for (const auto& match : matches) {
        const bool delivered = delivery(match.first, match.second);
        consumed |= delivered;
        if (!delivered && down) {
          std::lock_guard<std::mutex> lock(mutex);
          auto it = entries.find(match.first);
          if (it != entries.end()) it->second.held = false;
        }
      }
      return consumed;
    }
  };

  static CGEventRef TapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void* info) {
    // The owner survives until a main-queue retirement after source removal.
    auto state = static_cast<State*>(info)->shared_from_this();
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
      bool permission_required;
      { std::lock_guard<std::mutex> lock(state->mutex); permission_required = state->permission_required; }
      if (permission_required) { state->CheckPermission(true); return event; }
      CFMachPortRef tap = nullptr;
      { std::lock_guard<std::mutex> lock(state->mutex);
        if (state->active && !state->permission_lost && state->tap) { tap = state->tap; CFRetain(tap); }
      }
      if (tap) { CGEventTapEnable(tap, true); CFRelease(tap); }
      return event;
    }
    if (type != NX_SYSDEFINED || !event) return event;
    state->CheckPermission();
    @autoreleasepool {
      NSEvent* local = [NSEvent eventWithCGEvent:event];
      const bool was_active = NSApp.isActive;
      const bool consumed = state->Handle(local, ShortcutScope::Global);
      // A consumed global event will not reach AppKit's local monitor. Deliver
      // matching application registrations as well when this app has focus.
      std::shared_ptr<State> application;
      { std::lock_guard<std::mutex> lock(state->mutex); application = state->application.lock(); }
      const bool up = local.type == NSEventTypeSystemDefined &&
          ((static_cast<uint32_t>(local.data1) >> 8) & 0xff) == NX_KEYUP;
      if (application && ((consumed && was_active) || up))
        application->Handle(local, ShortcutScope::Application);
      return consumed ? nullptr : event;
    }
  }

  struct Group {
    std::shared_ptr<State> state;
    CFMachPortRef tap = nullptr;
    CFRunLoopSourceRef source = nullptr;
    id local = nil;
    id focus_observer = nil;
  };
  void RetireIfEmpty(Group& group) {
    if (!group.state) return;
    { std::lock_guard<std::mutex> lock(group.state->mutex); if (!group.state->entries.empty()) return; }
    Retire(group);
  }
  void Close() {
    std::lock_guard<std::mutex> resource_lock(resources_mutex_);
    Retire(global_);
    Retire(application_);
  }
  void Retire(Group& group) {
    if (!group.state) return;
    auto state = std::move(group.state);
    { std::lock_guard<std::mutex> lock(state->mutex);
      state->active = false; state->tap = nullptr; state->entries.clear();
    }
    auto tap = group.tap; auto source = group.source; id local = group.local; id observer = group.focus_observer;
    group.tap = nullptr; group.source = nullptr; group.local = nil; group.focus_observer = nil;
    auto retire = [state, tap, source, local, observer] {
      if (tap) CFMachPortInvalidate(tap);
      if (source) { CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes); CFRelease(source); }
      if (local) [NSEvent removeMonitor:local];
      if (observer) [NSNotificationCenter.defaultCenter removeObserver:observer];
      if (tap) CFRelease(tap);
    };
    if ([NSThread isMainThread]) retire(); else RunOnMainThread(std::move(retire));
  }

  Delivery delivery_;
  std::mutex resources_mutex_;
  Group global_;
  Group application_;
};
}  // namespace nativeapi::detail
