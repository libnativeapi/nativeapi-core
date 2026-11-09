#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

#include "../../protocol_handler_dispatch.h"
#include "string_utils_windows.h"
#include "ui_thread_windows.h"

// Windows starts a new process for every activation, with the URL as an
// argument. The first process reads it as its launch URL. A later process
// forwards its URL to the running one and exits, which the host does before it
// creates its own window (the Flutter plugin exports a helper for main.cpp).
//
// Forwarding contract, kept stable because the sender may be another build:
// find the message-only window of class "NativeAPI.ProtocolHandler" whose title
// is the receiver's full executable path in lower case, and send it WM_COPYDATA
// with dwData 0x4E415055 ('NAPU') and the UTF-8 URL, without a terminator.

namespace nativeapi::detail::protocol_platform {

namespace {

constexpr wchar_t kWindowClass[] = L"NativeAPI.ProtocolHandler";
constexpr ULONG_PTR kCopyDataUrl = 0x4E415055;

std::wstring ExecutablePath() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
      return {};
    if (length < buffer.size())
      return std::wstring(buffer.data(), length);
    buffer.resize(buffer.size() * 2);
  }
}

bool IsPackagedApp() {
  using GetCurrentPackageFullNameFn = LONG(WINAPI*)(UINT32*, PWSTR);
  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  auto get_name = kernel32 ? reinterpret_cast<GetCurrentPackageFullNameFn>(
                                 GetProcAddress(kernel32, "GetCurrentPackageFullName"))
                           : nullptr;
  if (!get_name)
    return false;
  UINT32 length = 0;
  return get_name(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
}

// A URL argument, as opposed to a path ("C:\x" has a one-letter scheme) or a flag.
bool LooksLikeUrl(const std::wstring& value) {
  size_t colon = value.find(L':');
  if (colon == std::wstring::npos || colon < 2)
    return false;
  for (size_t i = 0; i < colon; ++i) {
    wchar_t c = value[i];
    bool alpha = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
    bool other = (c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'.';
    if (!alpha && (i == 0 || !other))
      return false;
  }
  return true;
}

LRESULT CALLBACK ReceiverProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_COPYDATA) {
    auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
    if (data && data->dwData == kCopyDataUrl && data->lpData && data->cbData > 0) {
      std::string url(static_cast<const char*>(data->lpData), data->cbData);
      while (!url.empty() && url.back() == '\0')
        url.pop_back();
      ProtocolHandlerDispatch::Deliver(url, false);
      return TRUE;
    }
    return FALSE;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void CreateReceiver() {
  static HWND receiver = nullptr;
  if (receiver)
    return;
  WNDCLASSW klass{};
  klass.lpfnWndProc = ReceiverProc;
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpszClassName = kWindowClass;
  if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return;
  std::wstring title = ExecutablePath();
  if (title.empty())
    return;
  CharLowerBuffW(title.data(), static_cast<DWORD>(title.size()));
  receiver = CreateWindowExW(0, kWindowClass, title.c_str(), 0, 0, 0, 0, 0, HWND_MESSAGE,
                             nullptr, klass.hInstance, nullptr);
  // Senders run at the same or lower integrity; let their WM_COPYDATA through.
  if (receiver)
    ChangeWindowMessageFilterEx(receiver, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
}

bool SetString(HKEY parent, const std::wstring& subkey, const wchar_t* name,
               const std::wstring& value) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(parent, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS)
    return false;
  LONG result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                               static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

}  // namespace

bool IsSupported() {
  return true;
}

void StartCapture() {
  // The receiver has to live on the thread that pumps messages, which is not
  // the UI isolate's thread in a Flutter app.
  DWORD ui_thread = FindUiThreadId();
  if (ui_thread == 0 || ui_thread == GetCurrentThreadId() ||
      !PostToThread(ui_thread, CreateReceiver))
    CreateReceiver();
}

bool Register(const std::string& scheme) {
  // A package's registry writes are virtualized; MSIX declares the scheme as a
  // uap:Protocol extension in its manifest instead.
  if (IsPackagedApp())
    return false;
  const std::wstring exe = ExecutablePath();
  if (exe.empty())
    return false;
  const std::wstring name = StringToWString(scheme);
  const std::wstring root = L"Software\\Classes\\" + name;
  bool ok = SetString(HKEY_CURRENT_USER, root, nullptr, L"URL:" + name) &&
            SetString(HKEY_CURRENT_USER, root, L"URL Protocol", L"") &&
            SetString(HKEY_CURRENT_USER, root + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0") &&
            SetString(HKEY_CURRENT_USER, root + L"\\shell\\open\\command", nullptr,
                      L"\"" + exe + L"\" \"%1\"");
  if (ok)
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return ok;
}

std::optional<std::string> ReadLaunchUrl() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv)
    return std::nullopt;
  std::optional<std::string> url;
  for (int i = 1; i < argc && !url; ++i) {
    if (LooksLikeUrl(argv[i]))
      url = WStringToString(argv[i]);
  }
  LocalFree(argv);
  return url;
}

}  // namespace nativeapi::detail::protocol_platform
