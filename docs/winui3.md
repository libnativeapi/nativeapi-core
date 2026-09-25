# Optional WinUI 3 backend on Windows

`NATIVEAPI_ENABLE_WINUI3=ON` selects WinUI 3 for new Windows menus,
message dialogs and views, plus Windows App SDK title-bar and notification integration. Menus use `Microsoft.UI.Xaml.Controls.MenuFlyout`; message
dialogs use `Microsoft.UI.Xaml.Controls.ContentDialog`. Both share a lazy XAML
runtime and use private XAML Islands, including in tray-only applications.
Future WinUI components should use this same option and runtime.
File dialogs use the Windows system picker; they are not XAML controls.

With the option OFF (the default), menus, dialogs and views retain their Win32
implementations. Public C++ and C interfaces stay platform independent.

## Build

The optional backend requires MSVC, a Windows 10/11 SDK, and Windows App SDK 1.6.
The default build, including MinGW, has no new dependencies. This integration
currently pins the 1.6 package layout; newer SDK versions require validation before
changing the version guard.

From the repository root, in PowerShell:

```powershell
./cmake/RestoreWinUI3.ps1
$packages = (Resolve-Path build/winui3-packages).Path
cmake -S . -B build/menu-modern -G "Visual Studio 17 2022" -A x64 `
  -DNATIVEAPI_ENABLE_WINUI3=ON `
  "-DNATIVEAPI_WINAPPSDK_DIR=$packages/winappsdk" `
  "-DNATIVEAPI_CPPWINRT_EXE=$packages/cppwinrt/bin/cppwinrt.exe" `
  "-DNATIVEAPI_WEBVIEW2_DIR=$packages/webview2"
cmake --build build/menu-modern --config Debug --target menu_example message_dialog_example menu_backend_test winui3_dialog_test
./build/menu-modern/examples/menu_example/Debug/menu_example.exe
```

The restore script verifies pinned package hashes. It downloads build dependencies
only, without installing a runtime. WebView2 metadata is required to generate WinUI
projections; the menu does not instantiate WebView2 or require its browser runtime.

Install the matching [Windows App Runtime 1.6](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads-archive)
on deployment machines. For each consuming executable, call
`nativeapi_deploy_winui3(your_executable_target)` after `add_subdirectory(nativeapi)`
and creating the target. This copies the bootstrap DLL beside the executable.
For a DLL/plugin, deploy beside the **host executable**. The library dynamically
loads the bootstrap DLL only on first WinUI component use; a missing runtime does not
prevent native menus from working. An existing WinUI host's package graph and
dispatcher are reused.

The host executable must declare **PerMonitorV2** DPI awareness in its application
manifest to render crisp text at high display scaling. The bundled examples and
WinUI smoke test embed `cmake/winui3-example.manifest`. Add equivalent DPI settings
to your own application's manifest (a DLL manifest cannot set the host's DPI mode).
The library does not change process-wide DPI settings, since that would affect
other windows owned by the host. Without DPI awareness Windows bitmap-scales the
finished menu, which makes text and edges blurry.

## C++ and C usage

To try the tray integration, build and run the tray example instead:

```powershell
cmake --build build/menu-modern --config Debug --target tray_icon_example
./build/menu-modern/examples/tray_icon_example/Debug/tray_icon_example.exe
```

Right-click the tray icon whose tooltip is **nativeapi WinUI3 Tray Test - right-click**
(it may be in the tray overflow). Select **Exit** to stop the example.

```cpp
Application::GetInstance(); // Initializes COM on the UI thread.
auto menu = std::make_shared<Menu>();
menu->AddItem(std::make_shared<MenuItem>("Open"));
menu->Open(PositioningStrategy::CursorPosition()); // WinUI3 when compiled ON.

MessageDialog dialog("Information", "This is a real WinUI 3 ContentDialog.");
dialog.SetModality(DialogModality::Application);
dialog.Open();
```

```c
native_menu_t menu = native_menu_create();
/* The build option selects the default backend for the C API too. */
/* Add items, register listeners, and open using the existing C API. */
native_menu_free(menu);
```

`IsBackendSupported()` reports compile-time capability. `SetBackend()` rejects
unsupported backends, changes while open, and modern presentation for menus wrapping
an external `HMENU`. Runtime initialization failure makes `Open()` return false
and logs a diagnostic. Submenus use their root's backend.

## Message dialogs

Run `./build/menu-modern/examples/message_dialog_example/Debug/message_dialog_example.exe`.
No runtime switch or new API is required.

- With a visible parent supplied by `SetParentWindow()`, or the calling thread's
  active visible window, the dialog is hosted in a transparent XAML Island over
  that window's client area. It has no additional top-level window or caption.
  The existing content remains visible through the dimming layer. The island
  follows the parent's size/DPI and leaves its title and window styles intact.
- The parent HWND stays enabled so the island can receive input. Its underlying
  child controls are temporarily disabled; focus and their previous enabled states
  are restored on dismissal. Native parent destruction tears down the island first.
  Only one active dialog per parent is allowed; cross-thread parents are rejected.
- `None` returns after presentation; it still blocks interaction with the covered
  content. Keep the dialog alive and dispatch the UI message loop (normally
  `Application::Run()`). Use timers to test live updates and `Close()`.
- `Window` pumps messages until dismissal and blocks only the covered content.
- `Application` additionally disables other visible, enabled top-level windows
  in this process until dismissal. It never blocks other applications.
- Without a visible parent, tray-only callers retain the standalone host window.
- `SetTitle` and `SetMessage` update displayed content. Long messages scroll.
  The dialog buttons and `Close()` dismiss it; the standalone fallback also has
  its own title-bar close button. Previously
  enabled windows are restored on dismissal, initialization failure, or destruction.
- Create, mutate, open, close, and destroy on the same STA UI thread. Reopening
  an already open instance is rejected. WinUI runtime errors return false from
  `Open()` and are logged, with no silent fallback to Win32.
- Hosts and example executables use PerMonitorV2 sizing, including monitor changes.

The implementation follows Microsoft's [ContentDialog hosting requirements](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.controls.contentdialog?view=windows-app-sdk-1.6).

## Views

`View`, `Label`, `Button`, `TextField` and `ImageView` use WinUI 3 XAML
controls. Nothing changes in the calling code:

```cpp
auto root = window->GetContentView();   // a XAML Island over the client area
auto button = std::make_shared<Button>("Sign in");
root->AddSubview(button);               // a Microsoft.UI.Xaml.Controls.Button
std::cout << (root->GetBackend() == ViewBackend::WinUI3);  // 1
```

- The backend is chosen when a view is created, from `View::GetDefaultBackend()`:
  WinUI3 in this build. `View::SetDefaultBackend(ViewBackend::Native)` makes the
  views created afterwards Win32 controls again (`view_example --native`). A view
  keeps its backend; `AddSubview` ignores a view of the other backend.
- If the Windows App Runtime cannot start on the calling thread, views created
  there fall back to Native, `GetBackend()` says so, and one diagnostic goes to
  stderr. The thread must be an STA (`Application::GetInstance()` sets that up).
- A window's root view is a `DesktopWindowXamlSource` covering the whole client
  area with a `Canvas`; it follows the window's size and DPI. It sits above any
  child window a host framework put there, and it takes the input over the whole
  area, so a WinUI 3 root does not suit a window whose content is another
  framework's view. Use `ViewBackend::Native` there.
- Containers are `Canvas`es; Label is a `TextBlock` in a `Border`, TextField a
  `TextBox` with a `PasswordBox` shown in its place while secure, ImageView an
  `Image`. `GetNativeObject()` returns the element as an `IInspectable*` (the
  root still returns the window's `HWND`).
- Differences from the Win32 controls: backgrounds apply to every control;
  a secure TextField has no text alignment, multi-line mode or read-only mode of
  its own (read-only disables it); XAML's own font sizes and paddings make the
  intrinsic sizes larger.
- `Focus()` before the window is shown takes effect when the control loads.

## Menu behavior and limits

- Call creation, opening, closing, and mutations on the same STA UI thread. Keep
  the root menu alive until `Open()` returns. One modern menu session can run per
  thread; nested root opens are rejected.
- `Open()` continues pumping messages until dismissal. `Close()`, clicks outside,
  and normal WinUI dismissal end the session. `WM_QUIT` is preserved for the host.
- Normal items, separators, checkbox/radio items, icons, tooltips, disabled items,
  and nested submenus use WinUI controls. Checked state remains application-owned,
  matching the existing example: update it in the click listener.
- Labels, icons, enabled/checked state, tooltips, and shortcut text update while
  displayed. Structural edits (insert/remove/replace submenu) are reflected on the
  next root open. Root opening listeners run before constructing the item tree;
  submenu lifecycle events follow the child presenter's loading/unloading.
- Keyboard accelerator labels are displayed. This backend does not register
  application/global shortcuts; use the existing shortcut API for those.
- WinUI checkbox controls are two-state. Mixed checkbox state is currently shown
  as unchecked. Use the native backend if tri-state presentation is required.
- `GetNativeObject()` still returns the library-owned `HMENU`, never a WinRT
  pointer. It represents the native mirror; external direct `HMENU` changes do not
  update the modern presentation. Do not destroy this borrowed handle.
- Positioning uses the existing screen/DIP conversion followed by WinUI placement
  and edge avoidance. Themes and accessibility come from WinUI. Appearance can
  differ from File Explorer's shell menu. The temporary menu island is topmost
  while open so tray menus appear above the notification overflow panel; closing
  destroys that host without changing the application's topmost state.

## Verification

`ctest --test-dir build/menu-modern -C Debug -R menu_backend_test --output-on-failure`
checks backend selection without requiring the runtime. The explicit interactive
smoke test checks lifecycle/reentry and presentation above a topmost panel,
including host cleanup and preservation of the application's window level:

```powershell
./build/menu-modern/tests/Debug/menu_backend_test.exe --winui3
```

The test also exercises live property updates and cancellation from an opening
listener. Add `--interactive` to keep each popup open for up to a minute in a
preview window.

The same selection test should pass in a default build with the feature disabled.

The explicit desktop dialog integration test covers modal and modeless opening,
reentry rejection, live updates, programmatic/title-bar close, owner restoration,
destruction while open, and sharing the runtime with a menu:

```powershell
./build/menu-modern/tests/Debug/winui3_dialog_test.exe
```

## Extended desktop features

Build `desktop_features_example` and run it with one of these arguments:

| Argument | Example |
|---|---|
| `dialog` (default) | Save/Discard/Cancel buttons, editable input, checkbox, progress |
| `window` | Existing HWND with AppWindow title-bar colors and DWM Mica |
| `open` / `multiple` | Select one or several files |
| `save` | Select a save destination |
| `folder` | Select a folder |
| `notify` | Deliver a system notification; clicking it prints activation arguments and exits |

```powershell
cmake --build build/menu-modern --config Debug --target desktop_features_example desktop_features_test
./build/menu-modern/examples/desktop_features_example/Debug/desktop_features_example.exe dialog
```

`MessageDialog::IsExtendedSupported()` reports whether the extended controls are
compiled in. Configure buttons, default button and parent before opening. Input,
checkbox and progress can be updated while open. `SetProgress(-2)` hides progress,
`-1` is indeterminate, and values from 0 to 1 are determinate. Invalid values are
rejected. After dismissal, read `GetResult()`, `GetInputText()` and
`IsCheckboxChecked()`. Escape, close button and programmatic close return `Close`;
`None` means no result yet. On a modeless dialog, wait until `IsOpen()` is false.
Use all operations, including destruction, on the UI thread.

`Window::SetTitleBarColors()` and `ResetTitleBarColors()` operate on the existing
HWND through AppWindow; they do not replace an embedding framework's content.
Title-bar visibility uses OverlappedPresenter. Mica/Acrylic continue to use DWM,
so availability depends on the Windows version. Opaque content painted by a host
framework can cover the backdrop. The native HWND lifetime now controls removal
from WindowRegistry; destroying a temporary wrapper does not unregister the HWND.

`FileDialog` supports `OpenFile`, `OpenFiles`, `SaveFile`, and `SelectFolder`.
`Open()` blocks with message dispatch and returns true for acceptance **or user
cancellation**; distinguish them with `GetResult()`. `GetPaths()` returns UTF-8
filesystem paths. Failures return false with `GetLastError()`. Configure an explicit
parent with `SetParentWindow()` when one exists. Without a visible parent, a small
reusable owner is hosted on the calling STA. Keep the FileDialog alive until Open
returns. Only Window modality is supported; other modality values fail explicitly.

With WinUI3 enabled, the implementation uses the OS
[Windows.Storage.Pickers APIs with HWND interop](https://learn.microsoft.com/en-us/windows/apps/develop/ui/display-ui-objects),
which work with the pinned App SDK 1.6. It does **not** use the newer
Microsoft.Windows.Storage.Pickers namespace introduced in App SDK 1.8. WinRT file
pickers inherit the OS restrictions (including elevated-process restrictions).
The Win32 build uses IFileDialog, including native multiselect and folder picking.
SaveFile initially filters `.txt`; supply extensions explicitly for other formats.
Clearing the extension list uses `.txt` in the WinRT backend and all files in Win32. The WinRT save picker can create an empty file
when the selection is confirmed; callers should then write its contents.

`NotificationManager` uses Microsoft.Windows.AppNotifications, with optional
notification buttons and `NotificationActivatedEvent`. Subscribe before
`Initialize()` at every app startup, including notification-triggered launches.
Pump the normal application UI loop for activation callbacks. Initialize, Show,
Remove and Shutdown belong on the main STA. Call Shutdown before stopping the loop;
it unregisters this process but preserves the OS registration needed to relaunch
it from delivered notifications. Registration is owned by this manager: applications
already managing AppNotificationManager themselves should not initialize a second
registration through this library. Tags must be 1–16 ASCII letters/digits/`_`/`-`.
Show replaces notifications with the same tag in the library's `nativeapi` group.
Success means the OS accepted the notification, not that a banner was visible:
Windows notification settings and Do Not Disturb may suppress it. Both the runtime
framework and its notification/Singleton components must be installed.

### Platform support

| Feature | Windows, WinUI3 ON | Windows, OFF | Other platforms |
|---|---|---|---|
| Extended MessageDialog | Supported | Setters return false; basic dialog retained | Setters return false; existing basic implementation retained |
| Title-bar colors | AppWindow | Returns false | Returns false |
| FileDialog | System WinRT picker | IFileDialog | IsSupported returns false |
| NotificationManager | App SDK notifications | IsSupported returns false | IsSupported returns false |

All six platform directories define the new module entry points. C ABI files are
generated; the generator's header catalog in the sibling workspace now includes
`file_dialog.h` and `notification_manager.h`. This checkout is outside workspace/core,
so generation is driven through the workspace codegen wrapper with CORE pointing
at this checkout; downstream submodules are uninitialized and are not synced here.

### Additional verification

The default `desktop_features_test` checks option validation and C handle lifetime
without displaying UI. Explicit desktop integration modes are:

```powershell
./build/menu-modern/tests/Debug/desktop_features_test.exe --ui
./build/menu-modern/tests/Debug/desktop_features_test.exe --pickers
./build/menu-modern/tests/Debug/desktop_features_test.exe --notify
```

These exercise extended dialog state and live updates, HWND registration lifetime,
AppWindow title-bar changes, Mica, cancellation of all four picker modes, and
notification submission/removal. `--notify` briefly sends a test notification.
