#include <stdexcept>
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include "../../file_dialog_impl.h"
#include "string_utils_windows.h"
#ifdef NATIVEAPI_ENABLE_WINUI3
#include "winui3_runtime_windows.h"
#include "winrt_wait_windows.h"
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Foundation.Collections.h>
#endif

namespace nativeapi {
namespace {
struct FallbackPickerWindow {
  HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"File picker", WS_POPUP,
      0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  ~FallbackPickerWindow() { if (window) DestroyWindow(window); }
};
struct PickerOwner {
  HWND window = nullptr;
  bool created = false;
  explicit PickerOwner(const std::shared_ptr<Window>& parent) {
    window = parent ? static_cast<HWND>(parent->GetNativeObject()) : nullptr;
    if (parent && (!window || !IsWindow(window))) throw std::runtime_error("Invalid picker parent");
    if (!window || !IsWindowVisible(window)) {
      // WinRT brokers can retain the owner during asynchronous dismissal. Keep
      // one fallback HWND per STA so a subsequent picker never sees a stale owner.
      thread_local FallbackPickerWindow fallback;
      window = fallback.window;
      if (!window) throw std::runtime_error("Cannot create picker owner");
      created = true;
      SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }
  }
  ~PickerOwner() { if (created) ShowWindow(window, SW_HIDE); }
};
void Check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("File picker HRESULT " + std::to_string(result));
}
}
bool FileDialog::IsSupported() { return true; }
FileDialog::Impl::~Impl() = default;
bool FileDialog::Impl::Close() {
  try { return open && cancel && cancel(); }
#ifdef NATIVEAPI_ENABLE_WINUI3
  catch (const winrt::hresult_error& e) { error = winrt::to_string(e.message()) + " (HRESULT " + std::to_string(static_cast<int32_t>(e.code())) + ")"; }
#endif
  catch (const std::exception& e) { error = e.what(); }
  return false;
}
bool FileDialog::Impl::Open() {
  open = true;
  struct Cleanup {
    Impl& self;
    ~Cleanup() { self.cancel = {}; self.open = false; }
  } cleanup{*this};
  const char* phase = "Creating picker owner";
  try {
    PickerOwner owner(parent);
    const auto thread = GetCurrentThreadId();
#ifdef NATIVEAPI_ENABLE_WINUI3
    InitializeWinUI3();
    namespace P = winrt::Windows::Storage::Pickers;
    auto prepare = [hwnd = owner.window, &phase](const auto& picker) {
      phase = "Initializing picker owner";
      winrt::check_hresult(picker.template as<IInitializeWithWindow>()->Initialize(hwnd));
    };
    auto wait = [&](const auto& operation) {
      phase = "Waiting for picker result";
      cancel = [operation, thread]() {
        if (GetCurrentThreadId() != thread) return false;
        operation.Cancel();
        return true;
      };
      WaitForWinRT(operation);
    };
    if (mode == FileDialogMode::SelectFolder) {
      P::FolderPicker picker;
      prepare(picker);
      picker.FileTypeFilter().Append(L"*");
      auto operation = picker.PickSingleFolderAsync();
      wait(operation);
      if (auto folder = operation.GetResults()) paths.push_back(winrt::to_string(folder.Path()));
    } else if (mode == FileDialogMode::SaveFile) {
      P::FileSavePicker picker;
      prepare(picker);
      auto types = winrt::single_threaded_vector<winrt::hstring>();
      if (extensions.empty()) types.Append(L".txt");
      for (const auto& ext : extensions) types.Append(winrt::to_hstring(ext));
      picker.FileTypeChoices().Insert(L"Files", types);
      picker.SuggestedFileName(winrt::to_hstring(suggested_name));
      auto operation = picker.PickSaveFileAsync();
      wait(operation);
      if (auto file = operation.GetResults()) paths.push_back(winrt::to_string(file.Path()));
    } else {
      P::FileOpenPicker picker;
      prepare(picker);
      if (extensions.empty()) picker.FileTypeFilter().Append(L"*");
      for (const auto& ext : extensions) picker.FileTypeFilter().Append(winrt::to_hstring(ext));
      if (mode == FileDialogMode::OpenFiles) {
        auto operation = picker.PickMultipleFilesAsync();
        wait(operation);
        for (const auto& file : operation.GetResults()) paths.push_back(winrt::to_string(file.Path()));
      } else {
        auto operation = picker.PickSingleFileAsync();
        wait(operation);
        if (auto file = operation.GetResults()) paths.push_back(winrt::to_string(file.Path()));
      }
    }
#else
    const HRESULT com_init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    struct ComGuard {
      HRESULT hr;
      ~ComGuard() {
        if (SUCCEEDED(hr)) CoUninitialize();
      }
    } com_guard{com_init};
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    Check(CoCreateInstance(mode == FileDialogMode::SaveFile ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
                           nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    DWORD flags = 0;
    Check(dialog->GetOptions(&flags));
    flags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (mode == FileDialogMode::SelectFolder) flags |= FOS_PICKFOLDERS;
    else if (mode == FileDialogMode::SaveFile) flags |= FOS_OVERWRITEPROMPT;
    else flags |= FOS_FILEMUSTEXIST;
    if (mode == FileDialogMode::OpenFiles) flags |= FOS_ALLOWMULTISELECT;
    Check(dialog->SetOptions(flags));
    std::wstring pattern;
    for (const auto& ext : extensions) {
      if (!pattern.empty()) pattern += L";";
      pattern += ext == "*" ? L"*.*" : L"*" + StringToWString(ext);
    }
    if (!pattern.empty() && mode != FileDialogMode::SelectFolder) {
      COMDLG_FILTERSPEC filter{L"Files", pattern.c_str()};
      Check(dialog->SetFileTypes(1, &filter));
      if (mode == FileDialogMode::SaveFile && !extensions.empty())
        Check(dialog->SetDefaultExtension(StringToWString(extensions.front().substr(1)).c_str()));
    }
    if (!suggested_name.empty()) Check(dialog->SetFileName(StringToWString(suggested_name).c_str()));
    cancel = [dialog, thread] {
      if (GetCurrentThreadId() != thread) return false;
      // Show can dispatch callbacks before its native dialog is ready. Wait
      // for its HWND, then defer dismissal to the dialog message loop; calling
      // IFileDialog::Close synchronously inside such callbacks can stall Show.
      Microsoft::WRL::ComPtr<IOleWindow> native_window;
      HWND hwnd = nullptr;
      if (FAILED(dialog.As(&native_window)) || FAILED(native_window->GetWindow(&hwnd)) ||
          !hwnd || !IsWindowVisible(hwnd)) return false;
      return PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE;
    };
    const HRESULT shown = dialog->Show(owner.window);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { result = FileDialogResult::Cancelled; return true; }
    Check(shown);
    auto append = [&](IShellItem* item) {
      PWSTR path = nullptr;
      Check(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
      std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(path, CoTaskMemFree);
      paths.push_back(WCharArrayToString(path));
    };
    if (mode == FileDialogMode::OpenFiles) {
      Microsoft::WRL::ComPtr<IFileOpenDialog> picker;
      Check(dialog.As(&picker));
      Microsoft::WRL::ComPtr<IShellItemArray> items;
      Check(picker->GetResults(&items));
      DWORD count = 0;
      Check(items->GetCount(&count));
      for (DWORD i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IShellItem> item;
        Check(items->GetItemAt(i, &item));
        append(item.Get());
      }
    } else {
      Microsoft::WRL::ComPtr<IShellItem> item;
      Check(dialog->GetResult(&item));
      append(item.Get());
    }
#endif
    result = paths.empty() ? FileDialogResult::Cancelled : FileDialogResult::Accepted;
    return true;
#ifdef NATIVEAPI_ENABLE_WINUI3
  } catch (const winrt::hresult_error& e) {
    if (e.code() == HRESULT_FROM_WIN32(ERROR_CANCELLED) || e.code() == E_ABORT) {
      result = FileDialogResult::Cancelled;
      return true;
    }
    error = std::string(phase) + ": " + winrt::to_string(e.message()) + " (HRESULT " + std::to_string(static_cast<int32_t>(e.code())) + ")";
#endif
  } catch (const std::exception& e) { error = e.what(); }
  paths.clear();
  result = FileDialogResult::Failed;
  return false;
}
}
