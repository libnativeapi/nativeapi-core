#include <windows.h>
#include <cmath>
#include <iostream>
#include "nativeapi.h"
#include "../src/window_registry.h"
using namespace nativeapi;
namespace {
MessageDialog* active = nullptr;
FileDialog* picker = nullptr;
HWND parent = nullptr;
bool callback_ok = false;
void CALLBACK CloseDialog(HWND, UINT, UINT_PTR timer, DWORD) {
  KillTimer(nullptr, timer);
  callback_ok = active->IsOpen() && IsWindowEnabled(parent) &&
      !active->SetButtons("changed", "", "") && active->SetProgress(0.8) &&
      active->SetInputText("Updated input") && active->SetCheckbox("Checked", true) && active->Close();
}
void CALLBACK ClosePicker(HWND driver, UINT, UINT_PTR timer, DWORD) {
  // The native picker can pump messages while its COM shell is still opening.
  // Retry until its Close entry point is ready, instead of losing the timer.
  callback_ok = !picker->SetFileTypes({".png"}) && !picker->Open() && picker->Close();
  if (callback_ok) KillTimer(driver, timer);
}
bool Check(bool result, const char* text) {
  if (!result) std::cerr << text << '\n';
  return result;
}
}
int main(int argc, char** argv) {
  Application::GetInstance();
  MessageDialog dialog("Extended test", "Input, checkbox and progress");
  const bool modern = MessageDialog::IsExtendedSupported();
  if (!Check(!dialog.SetProgress(NAN), "NaN progress accepted") ||
      !Check(!dialog.SetProgress(2), "Invalid progress accepted") ||
      !Check(!dialog.SetButtons("", "", ""), "Empty buttons accepted") ||
      !Check(dialog.SetButtons("Save", "Discard", "Cancel") == modern, "Capability mismatch")) return 1;
  FileDialog save(FileDialogMode::SaveFile);
  if (!Check(!save.SetFileTypes({"*"}) && !save.SetFileTypes({"txt"}), "Invalid save types") ||
      !Check(save.SetFileTypes({".txt", ".md"}), "Valid types rejected") ||
      !Check(!save.SetSuggestedFileName("../outside.txt"), "Path accepted as filename")) return 1;
  save.SetModality(DialogModality::None);
  if (!Check(!save.Open() && save.GetResult() == FileDialogResult::Failed, "Invalid modality accepted")) return 1;
  auto handle = native_file_dialog_create(NATIVE_FILE_DIALOG_MODE_OPEN_FILE);
  if (!Check(handle != NATIVE_INVALID_FILE_DIALOG, "C handle allocation failed")) return 1;
  auto paths = native_file_dialog_get_paths(handle);
  if (!Check(paths.count == 0, "Fresh C picker has results")) return 1;
  native_string_list_free(&paths);
  native_file_dialog_free(handle);
  if (!Check(!native_file_dialog_open(handle), "Stale C handle accepted")) return 1;
  {
    auto effect_window = std::make_shared<Window>();
    // Every effect either takes and reads back, or is refused and leaves the last one.
    if (!Check(Window::IsVisualEffectSupported(VisualEffect::Blur), "Blur is on every Windows"))
      return 1;
    VisualEffect expected = VisualEffect::None;
    for (VisualEffect effect : {VisualEffect::Blur, VisualEffect::Acrylic, VisualEffect::Mica,
                                VisualEffect::MicaAlt, VisualEffect::Hud, VisualEffect::Popover,
                                VisualEffect::Menu}) {
      const bool supported = Window::IsVisualEffectSupported(effect);
      if (!Check(effect_window->SetVisualEffect(effect) == supported,
                 "SetVisualEffect disagrees with IsVisualEffectSupported"))
        return 1;
      if (supported) expected = effect;
      if (!Check(effect_window->GetVisualEffect() == expected,
                 "GetVisualEffect is not the effect in force"))
        return 1;
    }
    // The effect belongs to the HWND: another wrapper of it reads the same one.
    if (!Check(Window(effect_window->GetNativeObject()).GetVisualEffect() == expected,
               "A second wrapper lost the visual effect")) return 1;
    // A see-through background set meanwhile waits, and survives the effect.
    effect_window->SetBackgroundColor(Color::FromRGBA(10, 20, 30, 128));
    if (!Check(effect_window->SetVisualEffect(VisualEffect::None) &&
                   effect_window->GetVisualEffect() == VisualEffect::None,
               "Removing the effect failed")) return 1;
    const Color kept = effect_window->GetBackgroundColor();
    if (!Check(kept.r == 10 && kept.g == 20 && kept.b == 30 && kept.a == 128,
               "The background color did not survive the visual effect")) return 1;
    effect_window->SetBackgroundColor(Color::White);
  }
  if (argc == 1) return 0;
  const std::string mode = argv[1];
  if (mode == "--notify") {
    auto& manager = NotificationManager::GetInstance();
    if (!Check(manager.Initialize(), manager.GetLastError().c_str())) return 1;
    const bool shown = manager.Show("nativeapi test", "Notification delivery smoke test", "smoke", "");
    const bool removed = manager.Remove("smoke");
    manager.Shutdown();
    if (!Check(shown && removed, "Notification show/remove failed")) return 1;
    std::cout << "Notification test passed\n";
    return 0;
  }
  if (mode == "--pickers") {
    HWND driver = CreateWindowW(L"STATIC", L"Picker test timer", WS_POPUP, 0, 0, 1, 1,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    for (auto type : {FileDialogMode::OpenFile, FileDialogMode::OpenFiles,
                      FileDialogMode::SaveFile, FileDialogMode::SelectFolder}) {
      std::cout << "Picker mode " << static_cast<int>(type) << std::endl;
      FileDialog file(type);
      picker = &file;
      callback_ok = false;
      auto timer = SetTimer(driver, 1, 1800, ClosePicker);
      if (!timer) return 1;
      const bool opened = file.Open();
      KillTimer(driver, timer);
      if (!Check(opened && callback_ok && file.GetResult() == FileDialogResult::Cancelled &&
                 file.GetPaths().empty(), file.GetLastError().c_str())) return 1;
    }
    DestroyWindow(driver);
    std::cout << "Picker cancellation tests passed\n";
    return 0;
  }
  if (!modern) return 1;
  auto window = std::make_shared<Window>();
  window->SetTitle("nativeapi title test");
  if (!Check(window->GetTitle() == "nativeapi title test", "Window title was truncated")) return 1;
  parent = static_cast<HWND>(window->GetNativeObject());
  SetWindowPos(parent, nullptr, 100, 100, 650, 500, SWP_NOZORDER | SWP_SHOWWINDOW);
  window->SetTitleBarStyle(TitleBarStyle::Hidden);
  if (!Check(window->GetTitleBarStyle() == TitleBarStyle::Hidden, "Hidden title bar failed")) return 1;
  window->SetTitleBarStyle(TitleBarStyle::Normal);
  // Taking the title bar into the content area is a macOS thing; here it is refused
  // and nothing changes.
  if (!Check(!Window::IsContentUnderTitleBarSupported(),
             "Windows claims it can extend content into the title bar")) return 1;
  if (!Check(!window->SetContentUnderTitleBar(true) &&
                 !window->IsContentUnderTitleBar(),
             "A refused SetContentUnderTitleBar was recorded anyway")) return 1;
  if (!Check(window->SetTitleBarColors(Color::Blue, Color::White) && window->ResetTitleBarColors(),
             "Title bar colors failed")) return 1;
  dialog.SetParentWindow(window);
  dialog.SetModality(DialogModality::Window);
  dialog.SetInputEnabled(true);
  dialog.SetInputText("Initial input");
  dialog.SetCheckbox("Remember", false);
  dialog.SetProgress(-1);
  active = &dialog;
  auto timer = SetTimer(nullptr, 0, 1500, CloseDialog);
  if (!timer) return 1;
  const bool opened = dialog.Open();
  KillTimer(nullptr, timer);
  if (!Check(opened && callback_ok && !dialog.IsOpen() &&
      dialog.GetResult() == MessageDialogResult::Close && dialog.IsCheckboxChecked() &&
      dialog.GetInputText() == "Updated input" && IsWindowEnabled(parent), "Extended dialog failed")) return 1;
  const auto id = window->GetId();
  WindowRegistry::GetInstance().Add(id, window);
  { Window duplicate(parent); }
  if (!Check(WindowRegistry::GetInstance().Contains(id), "Temporary wrapper unregistered HWND")) return 1;
  DestroyWindow(parent);
  if (!Check(!WindowRegistry::GetInstance().Contains(id), "Destroyed HWND still registered")) return 1;
  std::cout << "Window and extended dialog tests passed" << std::endl;
}
