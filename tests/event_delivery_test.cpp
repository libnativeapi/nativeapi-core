// Lifetime and request decisions across foreign-thread asynchronous callbacks.
// No native windows, input, display server or event loop is needed.
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../src/capi/display_manager_c.h"
#include "../src/capi/event_delivery.h"
#include "../src/capi/shortcut_manager_c.h"
#include "../src/display_manager.h"
#include "../src/foundation/dispatcher.h"
#include "../src/foundation/event_request_dispatch.h"
#include "../src/foundation/handle_table.h"
#include "../src/shortcut_manager.h"

namespace {
int failures = 0;
int checks = 0;
std::mutex posted_mutex;
std::vector<std::function<void()>> posted;
void Check(bool condition, const char* description) {
  ++checks;
  if (!condition) { ++failures; std::cerr << "FAIL: " << description << '\n'; }
}
void Drain() {
  std::vector<std::function<void()>> work;
  { std::lock_guard<std::mutex> lock(posted_mutex); work.swap(posted); }
  for (auto& fn : work) fn();
}
void Release(void* value) { ++*static_cast<int*>(value); }

struct QueuedShortcut {
  const native_shortcut_event_t* event = nullptr;
  native_event_delivery_t delivery = 0;
};
QueuedShortcut shortcut;
void QueueShortcut(const native_shortcut_event_t* event, native_event_delivery_t delivery, void*) {
  shortcut = {event, delivery};
}
struct QueuedDisplay {
  const native_display_event_t* event = nullptr;
  native_event_delivery_t delivery = 0;
};
QueuedDisplay display;
void QueueDisplay(const native_display_event_t* event, native_event_delivery_t delivery, void*) {
  display = {event, delivery};
}

void TestGeneratedStringDelivery() {
  int released = 0;
  const auto initial = nativeapi::HandleTable::GetInstance().LiveCount();
  auto id = native_shortcut_manager_add_listener_async(QueueShortcut, &released, Release);
  Check(id != 0, "asynchronous listener registers");
  std::thread producer([] {
    nativeapi::ShortcutManager::GetInstance().Emit(nativeapi::ShortcutRegistrationFailedEvent(
        123, "Ctrl+Shift+ForeignThread", "a payload retained after the emitter returns"));
  });
  producer.join();
  Check(shortcut.delivery != 0 && native_event_delivery_is_active(shortcut.delivery),
        "foreign-thread callback receives active retained delivery");
  Check(std::string(shortcut.event->accelerator) == "Ctrl+Shift+ForeignThread" &&
        std::string(shortcut.event->data.registration_failed.error_message) ==
            "a payload retained after the emitter returns", "both owned strings survive native dispatch");
  Check(native_shortcut_manager_remove_listener(id), "remove queued listener");
  Drain();
  Check(!native_event_delivery_is_active(shortcut.delivery), "removal is visible before queued delivery runs");
  Check(released == 0, "removal does not close a callable with outstanding deliveries");
  Check(std::string(shortcut.event->accelerator) == "Ctrl+Shift+ForeignThread", "removal retains queued payload");
  Check(native_event_delivery_complete(shortcut.delivery, false), "discard queued payload");
  Check(released == 0, "release is posted, not inline inside acknowledgment");
  Drain();
  Check(released == 1, "user_data released exactly once after acknowledgment");
  Check(!native_event_delivery_complete(shortcut.delivery, true) &&
        !native_event_delivery_is_active(shortcut.delivery), "late duplicate acknowledgment fails safely");
  Check(nativeapi::HandleTable::GetInstance().LiveCount() == initial, "string delivery leaks no handles");
}

void TestGeneratedBorrowedHandle() {
  auto id = native_display_manager_add_listener_async(QueueDisplay, nullptr, nullptr);
  auto object = std::make_shared<nativeapi::Display>(nullptr);
  auto expected_id = object->GetId();
  std::weak_ptr<nativeapi::Display> weak = object;
  nativeapi::DisplayManager::GetInstance().Emit(nativeapi::DisplayAddedEvent(object));
  object.reset();
  auto handle = display.event->display;
  Check(!weak.expired() && native_display_get_id(handle) == expected_id,
        "queued payload retains its borrowed identity after emitter returns");
  Check(!native_event_delivery_complete(handle, false), "type-confused completion cannot release event object");
  Check(native_display_get_id(handle) == expected_id, "type confusion leaves borrowed object alive");
  Check(native_event_delivery_complete(display.delivery, true), "acknowledge borrowed handle delivery");
  Check(weak.expired() && native_display_get_id(handle) == 0, "acknowledgment invalidates borrowed handle");
  native_display_manager_remove_listener(id);
}

void TestRemovalDuringNativeDispatch() {
  std::mutex mutex;
  std::condition_variable condition;
  bool blocked = false, resume = false;
  auto& emitter = nativeapi::ShortcutManager::GetInstance();
  auto asynchronous = native_shortcut_manager_add_listener_async(QueueShortcut, nullptr, nullptr);
  auto blocker = emitter.AddListener<nativeapi::ShortcutEvent>([&](const auto&) {
    std::unique_lock<std::mutex> lock(mutex);
    blocked = true;
    condition.notify_all();
    condition.wait(lock, [&] { return resume; });
  });
  std::thread producer([&] {
    emitter.Emit(nativeapi::ShortcutActivatedEvent(123, "Ctrl+B"));
  });
  {
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [&] { return blocked; });
  }
  Check(native_shortcut_manager_remove_listener(asynchronous), "remove listener while another native callback holds snapshot");
  Check(!native_event_delivery_is_active(shortcut.delivery), "removal immediately invalidates queued delivery despite live native snapshot");
  Check(native_event_delivery_complete(shortcut.delivery, false), "discard delivery while native snapshot remains alive");
  { std::lock_guard<std::mutex> lock(mutex); resume = true; }
  condition.notify_all();
  producer.join();
  emitter.RemoveListener(blocker);
}

uint64_t Lease(const std::shared_ptr<nativeapi::EventRequest>& request,
               std::shared_ptr<void> payload = {}) {
  auto context = std::make_shared<nativeapi::capi::EventDeliveryContext>(
      nativeapi::capi::UserData::Make(nullptr, nullptr));
  auto vote = request->IsCancelable() ? request->Defer() : nullptr;
  return nativeapi::HandleTable::GetInstance().Insert(
      std::make_shared<nativeapi::capi::EventDelivery>(context, std::move(payload), std::move(vote)));
}
void TestRequestVotes() {
  using Dispatch = nativeapi::detail::EventRequestDispatch;
  int outcome = -1;
  auto request = Dispatch::Create(true, [&](bool accept) { outcome = accept ? 1 : 0; });
  auto first = Lease(request);
  auto second = Lease(request);
  Dispatch::Finish(request);
  Check(outcome == -1 && request->IsPending(), "queued callbacks hold implicit request votes");
  native_event_delivery_complete(first, true);
  Check(outcome == -1, "first asynchronous callback cannot approve other pending callbacks");
  native_event_delivery_complete(second, false);
  Check(outcome == 0 && request->IsCancelled(), "failed asynchronous callback vetoes request");

  outcome = -1;
  request = Dispatch::Create(true, [&](bool accept) { outcome = accept ? 1 : 0; });
  auto lease = Lease(request);
  Dispatch::Finish(request);
  auto explicit_vote = request->Defer();
  Check(explicit_vote != nullptr, "async callback can create an independently owned decision");
  native_event_delivery_complete(lease, true);
  Check(outcome == -1 && explicit_vote->IsPending(), "owned decision outlives callback payload");
  explicit_vote->Accept();
  Check(outcome == 1, "owned decision completes request after callback returns");

  outcome = -1;
  request = Dispatch::Create(false, [&](bool accept) { outcome = accept ? 1 : 0; });
  lease = Lease(request);
  Dispatch::Finish(request);
  Check(outcome == 1 && !request->IsPending(), "required shutdown never waits for asynchronous observers");
  native_event_delivery_complete(lease, false);
  Check(outcome == 1, "failed required observer cannot veto completed shutdown");
}

void TestConcurrentAcknowledgment() {
  using Dispatch = nativeapi::detail::EventRequestDispatch;
  std::atomic<int> completed{0}, freed{0}, successful{0};
  auto request = Dispatch::Create(true, [&](bool) {
    Check(freed == 1, "payload freed before request continuation runs");
    ++completed;
    // Reentrant table access must not deadlock during completion.
    nativeapi::HandleTable::GetInstance().LiveCount();
  });
  auto payload = std::shared_ptr<void>(new int(1), [&](void* pointer) {
    delete static_cast<int*>(pointer); ++freed;
  });
  auto delivery = Lease(request, std::move(payload));
  Dispatch::Finish(request);
  std::vector<std::thread> responders;
  for (int index = 0; index < 32; ++index) responders.emplace_back([&] {
    if (native_event_delivery_complete(delivery, true)) ++successful;
  });
  for (auto& responder : responders) responder.join();
  Check(successful == 1 && completed == 1 && freed == 1, "concurrent completion releases payload and votes once");
}
void TestThrowingCallback() {
  const auto initial = nativeapi::HandleTable::GetInstance().LiveCount();
  int released = 0;
  auto id = native_shortcut_manager_add_listener_async(
      [](const native_shortcut_event_t*, native_event_delivery_t, void*) { throw 1; }, &released, Release);
  nativeapi::ShortcutManager::GetInstance().Emit(nativeapi::ShortcutActivatedEvent(123, "Ctrl+A"));
  Check(nativeapi::HandleTable::GetInstance().LiveCount() == initial, "throwing callback releases retained payload");
  native_shortcut_manager_remove_listener(id);
  Drain();
  Check(released == 1, "throwing callback eventually releases user_data once");
  native_shortcut_manager_add_listener_async(nullptr, &released, Release);
  Drain();
  Check(released == 2, "null callback releases failed registration");
  Check(!native_event_delivery_complete(0, false), "zero handle fails safely");
}
}  // namespace

int main() {
  nativeapi::SetMainThreadDispatcher([](std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(posted_mutex); posted.push_back(std::move(fn)); return true;
  }, [] { return true; });
  TestGeneratedStringDelivery();
  TestGeneratedBorrowedHandle();
  TestRemovalDuringNativeDispatch();
  TestRequestVotes();
  TestConcurrentAcknowledgment();
  TestThrowingCallback();
  Drain();
  nativeapi::SetMainThreadDispatcher(nullptr, nullptr);
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
