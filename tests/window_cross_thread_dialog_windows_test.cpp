// #29: wrapping a HWND must not wait for its thread while an owned COM file
// dialog is initializing. Uses real windows/message loops; no synthetic input.
#include <windows.h>
#include <shobjidl.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "nativeapi.h"
#include "../src/platform/windows/ui_thread_windows.h"
#include "../src/window_registry.h"

namespace {
using Clock = std::chrono::steady_clock;
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
double Milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void Pump() {
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}
bool Until(const std::function<bool()>& done, int milliseconds = 5000) {
  const auto start = Clock::now();
  while (!done() && Milliseconds(start) < milliseconds) {
    Pump();
    Sleep(1);
  }
  return done();
}
constexpr wchar_t kClass[] = L"NativeApiCrossThreadDialogTest";
HWND Create(HWND owner = nullptr) {
  return CreateWindowExW(0, kClass, L"NativeAPI issue 29", WS_OVERLAPPEDWINDOW,
                        60, 60, 400, 250, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void Construction() {
  std::atomic<HWND> other{nullptr};
  std::atomic<bool> resume{false}, finish{false};
  std::thread worker([&] {
    HWND hwnd = Create();
    other = hwnd;
    // Deliver synchronous Win32 queries, but deliberately withhold posted work.
    // This reproduces the important difference during cross-thread focus work.
    while (!resume.load()) {
      MSG message;
      PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
      Sleep(1);
    }
    while (!finish.load()) {
      Pump();
      Sleep(1);
    }
    DestroyWindow(hwnd);
  });
  Until([&] { return other.load() != nullptr; });
  const auto start = Clock::now();
  auto window = std::make_shared<nativeapi::Window>(other.load());
  const double elapsed = Milliseconds(start);
  std::cout << "METRIC cross_thread_wrap_ms=" << elapsed << std::endl;
  Check(elapsed < 1000, "wrapping does not wait for posted work on the HWND thread");
  const auto id = window->GetId();
  nativeapi::WindowRegistry::GetInstance().Add(id, window);
  std::atomic<int> title_events{0};
  auto& manager = nativeapi::WindowManager::GetInstance();
  const auto listener = manager.AddListener<nativeapi::WindowPropertyChangedEvent>(
      [&](const auto& event) {
        if (event.GetWindowId() == id && event.GetProperty() == nativeapi::WindowProperty::Title)
          ++title_events;
      });
  std::atomic<bool> seeded{false}, changed{false};
  const DWORD thread = GetWindowThreadProcessId(other.load(), nullptr);
  nativeapi::detail::PostToThread(thread, [&] { seeded = true; });
  resume = true;
  Check(Until([&] { return seeded.load(); }), "deferred tracking runs when the HWND thread resumes");
  Check(title_events == 0, "initial snapshot does not emit a property change");
  nativeapi::detail::PostToThread(thread, [&] {
    SetWindowTextW(other.load(), L"changed on the native owner thread");
    changed = true;
  });
  Check(Until([&] { return changed.load(); }), "native property mutation completes");
  Check(title_events == 1, "native property mutation is observed exactly once after deferred setup");
  finish = true;
  // Keep processing owner-thread traffic while the worker destroys its HWND.
  Check(Until([&] { return !IsWindow(other.load()); }), "worker window is destroyed");
  worker.join();
  Check(!nativeapi::WindowRegistry::GetInstance().Contains(id), "deferred lifetime subclass removes the registry entry");
  manager.RemoveListener(listener);
}

struct DialogRun {
  IFileDialog* dialog = nullptr;
  HWND owner = nullptr;
  Clock::time_point start;
  double ready_ms = -1;
  bool owned = false;
  bool modal = false;
};
thread_local DialogRun* active_dialog = nullptr;
void CALLBACK CancelDialog(HWND, UINT, UINT_PTR, DWORD) {
  if (!active_dialog) return;
  auto& run = *active_dialog;
  HWND hwnd = nullptr;
  IOleWindow* ole = nullptr;
  if (SUCCEEDED(run.dialog->QueryInterface(IID_PPV_ARGS(&ole)))) {
    ole->GetWindow(&hwnd);
    ole->Release();
  }
  if (!hwnd || !IsWindowVisible(hwnd)) {
    if (Milliseconds(run.start) > 10000) run.dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    return;
  }
  // A timer on the dialog's own STA proves its modal loop is responsive.
  if (run.ready_ms < 0) run.ready_ms = Milliseconds(run.start);
  run.owned = GetWindow(hwnd, GW_OWNER) == run.owner;
  run.modal = run.owner && !IsWindowEnabled(run.owner);
  // Exercise the dialog's Cancel command, without injecting desktop input.
  // Keep retrying until Show returns: visibility can precede initialization.
  PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED),
               reinterpret_cast<LPARAM>(GetDlgItem(hwnd, IDCANCEL)));
}

void FileDialog(HWND owner, bool save, bool owned) {
  std::atomic<bool> done{false};
  std::atomic<DWORD> dialog_thread{0};
  HRESULT shown = E_FAIL;
  DialogRun result;
  int created = 0;
  auto& manager = nativeapi::WindowManager::GetInstance();
  const auto listener = manager.AddListener<nativeapi::WindowCreatedEvent>([&](const auto& event) {
    const auto window = manager.Get(event.GetWindowId());
    if (window && GetWindowThreadProcessId(static_cast<HWND>(window->GetNativeObject()), nullptr) == dialog_thread)
      ++created;
  });
  std::thread worker([&] {
    dialog_thread = GetCurrentThreadId();
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileDialog* dialog = nullptr;
    const HRESULT made = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
        nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(made)) {
      result.dialog = dialog;
      result.owner = owned ? owner : nullptr;
      dialog->SetTitle(L"NativeAPI issue 29 - automatic cancellation");
      result.start = Clock::now();
      active_dialog = &result;
      const auto timer = SetTimer(nullptr, 0, 50, CancelDialog);
      shown = dialog->Show(result.owner);
      KillTimer(nullptr, timer);
      active_dialog = nullptr;
      dialog->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    done = true;
  });
  const bool ended = Until([&] { return done.load(); }, 60000);
  if (!ended) {
    std::cerr << "FAIL COM dialog failed to terminate" << std::endl;
    // Do not hang the test runner or leave a dialog behind after a fatal failure.
    ExitProcess(1);
  }
  worker.join();
  Pump();
  std::cout << "METRIC " << (owned ? "owned_" : "unowned_") << (save ? "save" : "open")
            << "_ready_ms=" << result.ready_ms << std::endl;
  Check(result.ready_ms >= 0 && result.ready_ms < 3000, "COM file dialog responds within three seconds");
  Check(shown == HRESULT_FROM_WIN32(ERROR_CANCELLED), "dialog is cancelled without saving or opening a file");
  Check(result.owned, "dialog keeps the requested HWND owner");
  Check(owned ? result.modal : IsWindowEnabled(owner), "owner modality matches the requested mode");
  Check(IsWindowEnabled(owner), "owner is enabled again after cancellation");
  Check(created > 0, "WindowManager still reports the dialog window");
  manager.RemoveListener(listener);
}
}  // namespace

int main(int argc, char** argv) {
  WNDCLASSW cls{};
  cls.lpfnWndProc = DefWindowProcW;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = kClass;
  RegisterClassW(&cls);
  HWND owner = Create();
  if (!owner) return 1;
  ShowWindow(owner, SW_SHOWNORMAL);
  // Install WinEvent observation on the owner's platform thread before making
  // a window/dialog on a separate thread, like the Flutter/file_picker case.
  auto& manager = nativeapi::WindowManager::GetInstance();
  const auto listener = manager.AddListener<nativeapi::WindowCreatedEvent>([](const auto&) {});
  Construction();
  if (!(argc > 1 && std::string(argv[1]) == "--construction-only")) {
    FileDialog(owner, true, true);
    FileDialog(owner, true, true);
    FileDialog(owner, true, false);
    FileDialog(owner, false, true);
  }
  manager.RemoveListener(listener);
  DestroyWindow(owner);
  std::cout << (failures ? "FAILED " : "ALL PASS ") << failures << " failure(s)" << std::endl;
  return failures ? 1 : 0;
}
