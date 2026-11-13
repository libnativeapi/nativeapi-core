// Real owning-thread scheduling; no synthetic desktop input.
#include <windows.h>
#include <chrono>
#include <iostream>
#include <memory>
#include "nativeapi.h"

namespace {
int failures = 0;
int dispatched = 0;
void Check(bool value, const char* label) {
  std::cout << (value ? "PASS " : "FAIL ") << label << '\n';
  failures += !value;
}
void Pump(int milliseconds) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < end) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      ++dispatched;
      DispatchMessageW(&message);
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
  }
}
}  // namespace

int main() {
  using namespace nativeapi;
  Application::GetInstance();
  auto view = std::make_shared<View>();
  const auto thread = GetCurrentThreadId();
  int updates = 0;
  bool requeue = false;
  view->AddListener<ViewUpdatingEvent>([&](const auto& event) {
    Check(GetCurrentThreadId() == thread && event.GetViewId() == view->GetId(),
          "update arrives on its owner with the correct view ID");
    Check(!view->IsUpdatePending(), "delivery clears pending before calling listeners");
    ++updates;
    if (requeue && updates < 4) {
      const auto before = updates;
      Check(view->RequestUpdate(), "listener may request another update");
      Check(updates == before, "request from listener never recurses");
    }
  });
  bool accepted = true;
  for (int i = 0; i < 1000; ++i)
    accepted &= view->RequestUpdate();
  Check(accepted, "all coalesced requests accepted");
  Check(updates == 0 && view->IsUpdatePending(), "1000 requests remain asynchronous");
  Pump(40);
  Check(updates == 1 && !view->IsUpdatePending(), "1000 requests deliver once");
  view->RequestUpdate();
  view->CancelUpdate();
  Pump(40);
  Check(updates == 1, "cancelled generation is not delivered");
  view->RequestUpdate();
  view->CancelUpdate();
  view->RequestUpdate();
  Pump(40);
  Check(updates == 2, "cancel and requeue delivers only the new generation");
  requeue = true;
  view->RequestUpdate();
  Pump(100);
  Check(updates == 4, "requests made during delivery survive for subsequent frames");
  {
    auto doomed = std::make_shared<View>();
    doomed->AddListener<ViewUpdatingEvent>([](const auto&) { Check(false, "destroyed callback"); });
    doomed->RequestUpdate();
  }
  Pump(40);
  View unowned;
  Check(!unowned.RequestUpdate(), "view without shared ownership fails explicitly");
  SetMainThread();
  int completed = 0;
  bool fifo = true, outer_finished = false, later = false;
  const auto before = dispatched;
  for (int i = 0; i < 1000; ++i)
    RunOnMainThread([&, i] { fifo &= completed++ == i; });
  Check(completed == 0, "batched dispatch remains asynchronous");
  RunOnMainThread([&] {
    RunOnMainThread([&] { later = outer_finished; });
    Check(!later, "a nested post does not execute inline");
    outer_finished = true;
  });
  Pump(100);
  Check(completed == 1000 && fifo && later, "batched dispatch preserves FIFO and nested posts");
  Check(dispatched - before < 64, "1000 callbacks share a bounded number of native wake-ups");
  return failures ? 1 : 0;
}
