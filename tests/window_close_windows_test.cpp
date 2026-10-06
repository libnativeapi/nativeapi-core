// Own hidden HWND, real native message loop, no input and no visible desktop.
#include <windows.h>
#include <iostream>
#include <thread>
#include "nativeapi.h"
namespace {
int failures = 0, host_close = 0, host_query = 0;
bool allow = false;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
LRESULT CALLBACK Host(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  if (message == WM_CLOSE) {
    ++host_close;
    if (allow)
      DestroyWindow(hwnd);
    return 0;
  }
  if (message == WM_QUERYENDSESSION) {
    ++host_query;
    return FALSE;
  }
  return DefWindowProcW(hwnd, message, wp, lp);
}
void Drain() {
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}
}  // namespace
int main() {
  WNDCLASSW cls{};
  cls.lpfnWndProc = Host;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"NativeAPIWindowCloseTest";
  RegisterClassW(&cls);
  HWND hwnd = CreateWindowW(cls.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 160, 100, nullptr,
                            nullptr, cls.hInstance, nullptr);
  Check(hwnd && !IsWindowVisible(hwnd), "native fixture is hidden");
  const DWORD ui = GetCurrentThreadId();
  auto first = std::make_unique<nativeapi::Window>(hwnd);
  nativeapi::Window alias(hwnd);
  enum Mode { Veto, Defer, Accept } mode = Veto;
  int requested = 0, required = 0;
  std::shared_ptr<nativeapi::EventRequest> request;
  std::shared_ptr<nativeapi::EventDecision> a, b;
  size_t listener = 0;
  std::thread register_worker([&] {
    listener = first->AddListener<nativeapi::WindowCloseRequestedEvent>([&](const auto& event) {
      Check(GetCurrentThreadId() == ui, "FFI worker registration delivers on HWND owner");
      request = event.GetRequest();
      if (!request->IsCancelable()) {
        ++required;
        request->Cancel();
        return;
      }
      ++requested;
      if (mode == Veto)
        request->Cancel();
      if (mode == Defer)
        a = request->Defer();
    });
  });
  register_worker.join();
  Drain();
  auto other = alias.AddListener<nativeapi::WindowCloseRequestedEvent>([&](const auto& event) {
    Check(event.GetRequest() == request, "aliases share the native request");
    if (mode == Defer && event.GetRequest()->IsCancelable())
      b = event.GetRequest()->Defer();
  });
  Check(first->Close() && requested == 1 && host_close == 0, "public veto precedes host WM_CLOSE");
  SendMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
  Check(requested == 2 && host_close == 0, "native SC_CLOSE uses the shared confirmation gate");
  mode = Defer;
  first->Close();
  alias.Close();
  Check(requested == 3, "multiple requests coalesce");
  a->Accept();
  std::thread worker([&] { b->Accept(); });
  worker.join();
  Check(host_close == 0, "worker approval waits for HWND owner message loop");
  first->Close();
  Check(requested == 3, "accepted queued request remains coalesced");
  Drain();
  Check(host_close == 1 && IsWindow(hwnd), "approval preserves host close refusal");
  mode = Veto;
  Check(SendMessageW(hwnd, WM_QUERYENDSESSION, 0, 0) == FALSE && host_query == 1 && required == 1,
        "session query preserves the host's reply despite a listener veto");
  std::thread close_worker([&] { Check(first->Close(), "worker close is submitted"); });
  close_worker.join();
  Drain();
  Check(requested == 4 && host_close == 1, "worker close reaches the actual native gate");
  mode = Defer;
  first->Close();
  auto late_a = a, late_b = b;
  allow = true;
  SendMessageW(hwnd, WM_CLOSE, 0, 0);
  late_a->Accept();
  late_b->Accept();
  Drain();
  Check(!IsWindow(hwnd) && host_close == 2 && required == 2,
        "raw WM_CLOSE cannot be vetoed and invalidates old approvals");
  Check(!first->Close(), "destroyed HWND cannot accept a request");
  first->RemoveListener(listener);
  alias.RemoveListener(other);
  first.reset();
  hwnd = CreateWindowW(cls.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 160, 100, nullptr,
                       nullptr, cls.hInstance, nullptr);
  auto abandoned = std::make_unique<nativeapi::Window>(hwnd);
  int abandoned_callbacks = 0;
  std::thread abandoned_worker([&] {
    abandoned->AddListener<nativeapi::WindowCloseRequestedEvent>(
        [&](const auto&) { ++abandoned_callbacks; });
    Check(abandoned->Close(), "queued close before wrapper teardown is submitted");
  });
  abandoned_worker.join();
  abandoned.reset();
  Drain();
  Check(abandoned_callbacks == 0 && host_close == 2 && IsWindow(hwnd),
        "wrapper teardown fences queued foreign-thread setup and close");
  DestroyWindow(hwnd);
  return failures ? 1 : 0;
}
