#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <string>
#include <unordered_map>

#include <dwmapi.h>
#include <psapi.h>
#include <cmath>
#include "../../window.h"
#include "../../window_manager.h"
#include "../../window_registry.h"
#include "dpi_utils_windows.h"
#include "string_utils_windows.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dwmapi.lib")

namespace nativeapi {

// Property name for storing window ID in HWND (must match window_windows.cpp)
static const wchar_t* kWindowIdProperty = L"NativeAPIWindowId";

// Helper function to get window ID from HWND
// First tries to read from custom property, then creates Window object if needed
static WindowId GetWindowIdFromHwnd(HWND hwnd) {
  if (!hwnd) {
    return IdAllocator::kInvalidId;
  }

  // First, try to get window ID from HWND's custom property. A window can carry
  // an ID without being registered (created by Window(), or wrapped by a
  // binding), so make sure WindowManager::Get() can find it either way.
  HANDLE prop_handle = GetPropW(hwnd, kWindowIdProperty);
  if (prop_handle) {
    WindowId window_id = static_cast<WindowId>(reinterpret_cast<uintptr_t>(prop_handle));
    if (window_id != IdAllocator::kInvalidId && window_id != 0) {
      if (!WindowRegistry::GetInstance().Get(window_id)) {
        WindowRegistry::GetInstance().Add(window_id, std::make_shared<Window>(hwnd));
      }
      return window_id;
    }
  }

  // If property doesn't exist, create a new Window object and register it
  // Use shared_ptr so it can be properly registered in WindowRegistry
  auto window = std::make_shared<Window>(hwnd);
  WindowId window_id = window->GetId();

  // Register the window manually since constructor's shared_from_this() fails
  // during construction (shared_ptr control block not fully initialized yet)
  if (window_id != IdAllocator::kInvalidId) {
    WindowRegistry::GetInstance().Add(window_id, window);
  }

  return window_id;
}

namespace {

// Foreground-change tracking for WindowFocusedEvent / WindowBlurredEvent.
// The hook is system-wide because a window of this process loses focus exactly
// when some *other* process's window becomes foreground; a process-scoped hook
// would never report that transition. Foreign windows are filtered out below,
// so no window ids are ever allocated for them.
static HWINEVENTHOOK g_foreground_hook = nullptr;
static HWND g_focused_hwnd = nullptr;

// The WinEvent callback is a free function and cannot name the private
// WindowManager::Impl, so it dispatches through this trampoline, installed by
// Impl::StartEventListening().
using ForegroundChangedFn = void (*)(void* impl, HWND hwnd);
static ForegroundChangedFn g_foreground_changed_fn = nullptr;
static void* g_foreground_changed_context = nullptr;

static bool IsOwnProcessWindow(HWND hwnd) {
  if (!hwnd) {
    return false;
  }
  DWORD process_id = 0;
  GetWindowThreadProcessId(hwnd, &process_id);
  return process_id == GetCurrentProcessId();
}

static void CALLBACK ForegroundEventProc(HWINEVENTHOOK hook,
                                         DWORD event,
                                         HWND hwnd,
                                         LONG id_object,
                                         LONG id_child,
                                         DWORD event_thread,
                                         DWORD event_time) {
  (void)hook;
  (void)event_thread;
  (void)event_time;

  if (event != EVENT_SYSTEM_FOREGROUND || id_object != OBJID_WINDOW ||
      id_child != CHILDID_SELF || !hwnd) {
    return;
  }
  if (g_foreground_changed_fn && g_foreground_changed_context) {
    g_foreground_changed_fn(g_foreground_changed_context, hwnd);
  }
}

// Geometry and show-state tracking for WindowMinimizedEvent, WindowMaximizedEvent,
// WindowRestoredEvent, WindowMovedEvent and WindowResizedEvent. The windows are
// usually owned by someone else (Flutter's runner, the host application), so
// the library subclasses each one it reports on and reads WM_WINDOWPOSCHANGED,
// which every change of a window's rectangle goes through, minimizing and
// maximizing included. The snapshot kept per window tells which of the five
// events a change amounts to.
//
// Not a WinEvent hook on EVENT_OBJECT_LOCATIONCHANGE: out-of-context WinEvents
// are queued to this thread and delivered whenever it next reads a message —
// in the middle of Flutter's resize, which pumps messages while it waits for a
// frame of the new size, among other places — and the listeners called from
// there (Dart ones synchronously) made every resize step late. The subclass
// reports after the window's own procedure, and so Flutter, is done with it.
struct WindowSnapshot {
  bool minimized;
  bool maximized;
  RECT rect;
};
static HWINEVENTHOOK g_lifetime_hook = nullptr;
static std::unordered_map<HWND, WindowSnapshot> g_window_snapshots;
// Windows seen on screen, which is what WindowCreatedEvent and WindowClosedEvent
// are about. The ID is kept because the destroy event is delivered after the
// HWND, and the property holding its ID, are gone.
static std::unordered_map<HWND, WindowId> g_shown_windows;

using WindowChangedFn = void (*)(void* impl, DWORD event, HWND hwnd);
static WindowChangedFn g_window_changed_fn = nullptr;
static void* g_window_changed_context = nullptr;

// Top-level windows that a listener can make sense of: anything already known
// to the library, otherwise what the user sees as a window. Tooltips, menus and
// other helper windows move and resize as well and must not get an id for it.
static bool IsReportableWindow(HWND hwnd) {
  if (!IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) {
    return false;
  }
  if (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD) {
    return false;
  }
  if (GetPropW(hwnd, kWindowIdProperty)) {
    return true;
  }
  if (!IsWindowVisible(hwnd)) {
    return false;
  }
  return !(GetWindowLongPtr(hwnd, GWL_EXSTYLE) & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE));
}

static LRESULT CALLBACK WindowGeometryProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                           UINT_PTR subclass_id, DWORD_PTR) {
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, WindowGeometryProc, subclass_id);
  } else if (message == WM_WINDOWPOSCHANGED) {
    const LRESULT result = DefSubclassProc(hwnd, message, wp, lp);
    if (g_window_changed_fn && g_window_changed_context) {
      g_window_changed_fn(g_window_changed_context, EVENT_OBJECT_LOCATIONCHANGE, hwnd);
    }
    return result;
  }
  return DefSubclassProc(hwnd, message, wp, lp);
}

// Starts reporting geometry changes of `hwnd`. Only windows of the calling
// thread can be subclassed; the others are tracked for creation and closing
// only.
static void WatchWindowGeometry(HWND hwnd) {
  SetWindowSubclass(hwnd, WindowGeometryProc, reinterpret_cast<UINT_PTR>(&WindowGeometryProc), 0);
}

static void UnwatchWindowGeometry(HWND hwnd) {
  RemoveWindowSubclass(hwnd, WindowGeometryProc, reinterpret_cast<UINT_PTR>(&WindowGeometryProc));
}

static void CALLBACK WindowChangedEventProc(HWINEVENTHOOK hook,
                                            DWORD event,
                                            HWND hwnd,
                                            LONG id_object,
                                            LONG id_child,
                                            DWORD event_thread,
                                            DWORD event_time) {
  (void)hook;
  (void)event_thread;
  (void)event_time;

  // OBJID_WINDOW leaves out the caret and the cursor, which report showing
  // through the same event.
  if (id_object != OBJID_WINDOW || id_child != CHILDID_SELF || !hwnd) {
    return;
  }
  if (g_window_changed_fn && g_window_changed_context) {
    g_window_changed_fn(g_window_changed_context, event, hwnd);
  }
}

using PFN_ShowWindow = BOOL(WINAPI*)(HWND, int);
using PFN_ShowWindowAsync = BOOL(WINAPI*)(HWND, int);

static PFN_ShowWindow g_original_show_window = nullptr;
static PFN_ShowWindowAsync g_original_show_window_async = nullptr;
static bool g_hooks_installed = false;

static bool IsShowCommand(int cmd) {
  switch (cmd) {
    case SW_SHOW:
    case SW_SHOWNORMAL:
    case SW_SHOWDEFAULT:
    case SW_SHOWMAXIMIZED:
    case SW_SHOWNOACTIVATE:
    case SW_RESTORE:
      return true;
    default:
      return false;
  }
}

// Intercept show/hide commands and invoke hooks if registered
// Returns true if hook handled the operation (skip original implementation)
static bool TryHandleWithHook(HWND hwnd, int cmd) {
  WindowId window_id = GetWindowIdFromHwnd(hwnd);
  if (window_id == IdAllocator::kInvalidId) {
    return false;
  }

  auto& manager = WindowManager::GetInstance();

  if (cmd == SW_HIDE && manager.HasWillHideHook()) {
    manager.HandleWillHide(window_id);
    return true;
  }

  if (IsShowCommand(cmd) && manager.HasWillShowHook()) {
    manager.HandleWillShow(window_id);
    return true;
  }

  return false;
}

static BOOL WINAPI HookedShowWindow(HWND hwnd, int nCmdShow) {
  if (TryHandleWithHook(hwnd, nCmdShow)) {
    return TRUE;
  }

  if (g_original_show_window) {
    return g_original_show_window(hwnd, nCmdShow);
  }

  auto p = reinterpret_cast<PFN_ShowWindow>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "ShowWindow"));
  return p ? p(hwnd, nCmdShow) : FALSE;
}

static BOOL WINAPI HookedShowWindowAsync(HWND hwnd, int nCmdShow) {
  if (TryHandleWithHook(hwnd, nCmdShow)) {
    return TRUE;
  }

  if (g_original_show_window_async) {
    return g_original_show_window_async(hwnd, nCmdShow);
  }

  auto p = reinterpret_cast<PFN_ShowWindowAsync>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "ShowWindowAsync"));
  return p ? p(hwnd, nCmdShow) : FALSE;
}

static bool CaseInsensitiveEquals(const char* a, const char* b) {
  if (!a || !b)
    return false;
  while (*a && *b) {
    char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
    if (ca != cb)
      return false;
    ++a;
    ++b;
  }
  return *a == *b;
}

static void PatchIATInModule(HMODULE module,
                             FARPROC target,
                             FARPROC replacement,
                             const char* func_name) {
  if (!module)
    return;

  auto base = reinterpret_cast<BYTE*>(module);
  auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
  if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
    return;

  auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
  if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
    return;

  auto& import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (import_dir.VirtualAddress == 0)
    return;

  auto import_desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + import_dir.VirtualAddress);
  for (; import_desc->Name != 0; ++import_desc) {
    auto dll_name = reinterpret_cast<const char*>(base + import_desc->Name);
    // Only hook USER32.dll to reduce risk
    if (!dll_name)
      continue;
    if (!(CaseInsensitiveEquals(dll_name, "user32.dll")))
      continue;

    auto orig_thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + import_desc->OriginalFirstThunk);
    auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + import_desc->FirstThunk);
    if (!orig_thunk || !thunk)
      continue;

    for (; orig_thunk->u1.AddressOfData != 0; ++orig_thunk, ++thunk) {
      if (IMAGE_SNAP_BY_ORDINAL(orig_thunk->u1.Ordinal)) {
        continue;  // Skip ordinals
      }
      auto import = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + orig_thunk->u1.AddressOfData);
      if (!import || !import->Name)
        continue;
      const char* name = reinterpret_cast<const char*>(import->Name);
      if (!CaseInsensitiveEquals(name, func_name))
        continue;

      // Change protection and write new function pointer
      DWORD old_protect;
      if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old_protect)) {
        // Store original (first time only)
        (void)target;  // target kept for symmetry; not used here
        thunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
        VirtualProtect(&thunk->u1.Function, sizeof(void*), old_protect, &old_protect);
        FlushInstructionCache(GetCurrentProcess(), &thunk->u1.Function, sizeof(void*));
      }
    }
  }
}

static void RestoreIATInModule(HMODULE module, FARPROC original, const char* func_name) {
  if (!module || !original)
    return;

  auto base = reinterpret_cast<BYTE*>(module);
  auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
  if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
    return;

  auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
  if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
    return;

  auto& import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (import_dir.VirtualAddress == 0)
    return;

  auto import_desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + import_dir.VirtualAddress);
  for (; import_desc->Name != 0; ++import_desc) {
    auto dll_name = reinterpret_cast<const char*>(base + import_desc->Name);
    if (!dll_name)
      continue;
    if (!(CaseInsensitiveEquals(dll_name, "user32.dll")))
      continue;

    auto orig_thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + import_desc->OriginalFirstThunk);
    auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + import_desc->FirstThunk);
    if (!orig_thunk || !thunk)
      continue;

    for (; orig_thunk->u1.AddressOfData != 0; ++orig_thunk, ++thunk) {
      if (IMAGE_SNAP_BY_ORDINAL(orig_thunk->u1.Ordinal)) {
        continue;
      }
      auto import = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + orig_thunk->u1.AddressOfData);
      if (!import || !import->Name)
        continue;
      const char* name = reinterpret_cast<const char*>(import->Name);
      if (!CaseInsensitiveEquals(name, func_name))
        continue;

      DWORD old_protect;
      if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old_protect)) {
        thunk->u1.Function = reinterpret_cast<ULONG_PTR>(original);
        VirtualProtect(&thunk->u1.Function, sizeof(void*), old_protect, &old_protect);
        FlushInstructionCache(GetCurrentProcess(), &thunk->u1.Function, sizeof(void*));
      }
    }
  }
}

static void ForEachProcessModule(std::function<void(HMODULE)> fn) {
  HMODULE modules[1024];
  DWORD bytes_needed = 0;
  if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes_needed)) {
    // Fallback: at least patch main module
    fn(GetModuleHandle(nullptr));
    return;
  }
  size_t count = bytes_needed / sizeof(HMODULE);
  for (size_t i = 0; i < count; ++i) {
    fn(modules[i]);
  }
}

static void InstallHooks() {
  if (g_hooks_installed)
    return;

  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (!user32)
    user32 = LoadLibraryW(L"user32.dll");
  if (!user32)
    return;

  g_original_show_window = reinterpret_cast<PFN_ShowWindow>(GetProcAddress(user32, "ShowWindow"));
  g_original_show_window_async =
      reinterpret_cast<PFN_ShowWindowAsync>(GetProcAddress(user32, "ShowWindowAsync"));
  if (!g_original_show_window)
    return;

  ForEachProcessModule([](HMODULE m) {
    PatchIATInModule(m, reinterpret_cast<FARPROC>(g_original_show_window),
                     reinterpret_cast<FARPROC>(HookedShowWindow), "ShowWindow");
    if (g_original_show_window_async) {
      PatchIATInModule(m, reinterpret_cast<FARPROC>(g_original_show_window_async),
                       reinterpret_cast<FARPROC>(HookedShowWindowAsync), "ShowWindowAsync");
    }
  });

  g_hooks_installed = true;
}

static void UninstallHooks() {
  if (!g_hooks_installed)
    return;

  ForEachProcessModule([](HMODULE m) {
    if (g_original_show_window) {
      RestoreIATInModule(m, reinterpret_cast<FARPROC>(g_original_show_window), "ShowWindow");
    }
    if (g_original_show_window_async) {
      RestoreIATInModule(m, reinterpret_cast<FARPROC>(g_original_show_window_async),
                         "ShowWindowAsync");
    }
  });
  g_hooks_installed = false;
}

}  // namespace

// Private implementation to hide Windows-specific details
class WindowManager::Impl {
 public:
  Impl(WindowManager* manager) : manager_(manager) {}
  ~Impl() {}

  // WINEVENT_OUTOFCONTEXT callbacks are delivered through the message queue of
  // the thread that installs the hook, so WindowManager must first be touched
  // from the UI thread; UnhookWinEvent has to run on that same thread.
  void StartEventListening() {
    g_foreground_changed_context = this;
    g_foreground_changed_fn = [](void* impl, HWND hwnd) {
      static_cast<Impl*>(impl)->OnForegroundChanged(hwnd);
    };

    if (!g_foreground_hook) {
      g_foreground_hook =
          SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                          ForegroundEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    }

    // Seed the tracked window so the first blur names the right window.
    HWND foreground = GetForegroundWindow();
    g_focused_hwnd = IsOwnProcessWindow(foreground) ? foreground : nullptr;

    g_window_changed_context = this;
    g_window_changed_fn = [](void* impl, DWORD event, HWND hwnd) {
      static_cast<Impl*>(impl)->OnWindowChanged(event, hwnd);
    };

    // Unlike the foreground hook this only concerns our own windows, so it is
    // scoped to this process and costs nothing while other applications work.
    // Showing and destroying are rare, unlike location changes (see above).
    if (!g_lifetime_hook) {
      // EVENT_OBJECT_DESTROY and EVENT_OBJECT_SHOW are adjacent
      g_lifetime_hook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW, nullptr,
                                        WindowChangedEventProc, GetCurrentProcessId(), 0,
                                        WINEVENT_OUTOFCONTEXT);
    }

    // Windows that exist already have a state to compare the first change with
    EnumWindows(
        [](HWND hwnd, LPARAM) -> BOOL {
          if (IsOwnProcessWindow(hwnd) && IsReportableWindow(hwnd)) {
            g_window_snapshots[hwnd] = TakeSnapshot(hwnd);
            WatchWindowGeometry(hwnd);
            // Already on screen, so not created under our eyes: no
            // WindowCreatedEvent for it, only the WindowClosedEvent.
            if (IsWindowVisible(hwnd)) {
              WindowId window_id = GetWindowIdFromHwnd(hwnd);
              if (window_id != IdAllocator::kInvalidId) {
                g_shown_windows[hwnd] = window_id;
              }
            }
          }
          return TRUE;
        },
        0);
  }

  void StopEventListening() {
    if (g_foreground_hook) {
      UnhookWinEvent(g_foreground_hook);
      g_foreground_hook = nullptr;
    }
    if (g_lifetime_hook) {
      UnhookWinEvent(g_lifetime_hook);
      g_lifetime_hook = nullptr;
    }
    g_focused_hwnd = nullptr;
    g_foreground_changed_fn = nullptr;
    g_foreground_changed_context = nullptr;
    g_window_changed_fn = nullptr;
    g_window_changed_context = nullptr;
    for (const auto& [hwnd, snapshot] : g_window_snapshots) {
      UnwatchWindowGeometry(hwnd);
    }
    g_window_snapshots.clear();
    g_shown_windows.clear();
  }

  static WindowSnapshot TakeSnapshot(HWND hwnd) {
    WindowSnapshot snapshot = {};
    snapshot.minimized = IsIconic(hwnd) != FALSE;
    snapshot.maximized = IsZoomed(hwnd) != FALSE;
    GetWindowRect(hwnd, &snapshot.rect);
    return snapshot;
  }

  // Compare a window with its last snapshot and emit what changed.
  void OnWindowChanged(DWORD event, HWND hwnd) {
    if (event == EVENT_OBJECT_DESTROY) {
      g_window_snapshots.erase(hwnd);
      auto shown = g_shown_windows.find(hwnd);
      if (shown != g_shown_windows.end()) {
        const WindowId window_id = shown->second;
        g_shown_windows.erase(shown);
        WindowClosedEvent closed_event(window_id);
        manager_->DispatchWindowEvent(closed_event);
      }
      return;
    }
    if (!IsReportableWindow(hwnd)) {
      return;
    }

    if (IsWindowVisible(hwnd) && g_shown_windows.find(hwnd) == g_shown_windows.end()) {
      WindowId window_id = GetWindowIdFromHwnd(hwnd);
      if (window_id != IdAllocator::kInvalidId) {
        g_shown_windows[hwnd] = window_id;
        WindowCreatedEvent created_event(window_id);
        manager_->DispatchWindowEvent(created_event);
      }
    }

    const WindowSnapshot current = TakeSnapshot(hwnd);
    auto it = g_window_snapshots.find(hwnd);
    if (it == g_window_snapshots.end()) {
      // First sight (a window being shown): nothing to compare with yet
      g_window_snapshots[hwnd] = current;
      WatchWindowGeometry(hwnd);
      return;
    }
    if (event != EVENT_OBJECT_LOCATIONCHANGE) {
      return;
    }

    const WindowSnapshot previous = it->second;
    if (current.minimized) {
      // A minimized window is parked off screen (-32000) and no longer reports
      // as maximized. Keep the rest of the snapshot, so that restoring it to
      // where it was is neither a move, a resize nor a second "maximized".
      it->second.minimized = true;
      if (!previous.minimized) {
        OnWindowEvent(hwnd, "minimized");
      }
      return;
    }
    it->second = current;

    if (previous.minimized) {
      OnWindowEvent(hwnd, "restored");
    }
    if (current.maximized != previous.maximized) {
      OnWindowEvent(hwnd, current.maximized ? "maximized" : "restored");
    }
    if (current.rect.left != previous.rect.left || current.rect.top != previous.rect.top) {
      OnWindowEvent(hwnd, "moved");
    }
    if (current.rect.right - current.rect.left != previous.rect.right - previous.rect.left ||
        current.rect.bottom - current.rect.top != previous.rect.bottom - previous.rect.top) {
      OnWindowEvent(hwnd, "resized");
    }
  }

  // Translate a foreground change into blurred/focused events. Only windows of
  // this process are reported: focus moving to another application blurs the
  // window we were tracking and focuses nothing.
  void OnForegroundChanged(HWND hwnd) {
    if (hwnd == g_focused_hwnd) {
      return;
    }

    HWND previous = g_focused_hwnd;
    g_focused_hwnd = IsOwnProcessWindow(hwnd) ? hwnd : nullptr;

    if (previous && IsWindow(previous)) {
      OnWindowEvent(previous, "blurred");
    }
    if (g_focused_hwnd) {
      OnWindowEvent(g_focused_hwnd, "focused");
    }
  }

  void OnWindowEvent(HWND hwnd, const std::string& event_type) {
    // Get window ID, first trying custom property, then creating Window if needed
    WindowId window_id = GetWindowIdFromHwnd(hwnd);
    if (window_id == IdAllocator::kInvalidId) {
      return;
    }

    if (event_type == "focused") {
      WindowFocusedEvent event(window_id);
      manager_->DispatchWindowEvent(event);
    } else if (event_type == "blurred") {
      WindowBlurredEvent event(window_id);
      manager_->DispatchWindowEvent(event);
    } else if (event_type == "minimized") {
      WindowMinimizedEvent event(window_id);
      manager_->DispatchWindowEvent(event);
    } else if (event_type == "restored") {
      WindowRestoredEvent event(window_id);
      manager_->DispatchWindowEvent(event);
    } else if (event_type == "maximized") {
      WindowMaximizedEvent event(window_id);
      manager_->DispatchWindowEvent(event);
    } else if (event_type == "resized" || event_type == "moved") {
      // Report what the getters return (logical pixels), not the raw rectangle
      auto window = WindowRegistry::GetInstance().Get(window_id);
      if (!window) {
        return;
      }
      if (event_type == "resized") {
        WindowResizedEvent event(window_id, window->GetSize());
        manager_->DispatchWindowEvent(event);
      } else {
        WindowMovedEvent event(window_id, window->GetPosition());
        manager_->DispatchWindowEvent(event);
      }
    } else if (event_type == "closing") {
      // Window closing event - no longer emitted
    }
  }

 private:
  WindowManager* manager_;
  // Optional pre-show/hide hooks
  // Held by shared_ptr so HandleWillShow/Hide can keep a hook alive while it
  // replaces or clears itself, without copying it.
  std::shared_ptr<WindowManager::WindowWillShowHook> will_show_hook_;
  std::shared_ptr<WindowManager::WindowWillHideHook> will_hide_hook_;

  friend class WindowManager;
};

WindowManager::WindowManager() : pimpl_(std::make_unique<Impl>(this)) {
  StartEventListening();
}

WindowManager::~WindowManager() {
  StopEventListening();
}

namespace {

struct WindowIdSearch {
  WindowId id;
  HWND found;
};

// Finds this process's top-level window carrying an ID, visible or not.
BOOL CALLBACK FindWindowWithId(HWND hwnd, LPARAM data) {
  auto* search = reinterpret_cast<WindowIdSearch*>(data);
  if (!IsOwnProcessWindow(hwnd)) {
    return TRUE;
  }
  HANDLE prop_handle = GetPropW(hwnd, kWindowIdProperty);
  if (prop_handle && static_cast<WindowId>(reinterpret_cast<uintptr_t>(prop_handle)) == search->id) {
    search->found = hwnd;
    return FALSE;
  }
  return TRUE;
}

}  // namespace

std::shared_ptr<Window> WindowManager::Get(WindowId id) {
  // First try to get from registry
  auto window = WindowRegistry::GetInstance().Get(id);
  if (window) {
    return window;
  }

  // Not registered yet: the window may still exist, hidden or untitled (which
  // GetAll() skips), so look for the ID on this process's windows directly.
  WindowIdSearch search = {id, nullptr};
  EnumWindows(FindWindowWithId, reinterpret_cast<LPARAM>(&search));
  if (search.found) {
    GetWindowIdFromHwnd(search.found);
  }
  return WindowRegistry::GetInstance().Get(id);
}

// Callback for EnumWindows to collect all top-level windows
static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
  auto* windows = reinterpret_cast<std::vector<HWND>*>(lParam);

  // Only include visible windows that are not minimized to taskbar
  // and have a title (filters out many background windows)
  if (IsWindowVisible(hwnd)) {
    int length = GetWindowTextLengthW(hwnd);
    if (length > 0) {
      // Check if it's a normal window (not tool window, etc.)
      LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
      if (!(exStyle & WS_EX_TOOLWINDOW)) {
        windows->push_back(hwnd);
      }
    }
  }

  return TRUE;  // Continue enumeration
}

std::vector<std::shared_ptr<Window>> WindowManager::GetAll() {
  std::vector<HWND> hwnds;

  // Enumerate all top-level windows
  EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&hwnds));

  std::vector<std::shared_ptr<Window>> windows;
  windows.reserve(hwnds.size());

  for (HWND hwnd : hwnds) {
    // Get window ID from HWND, creating Window object if needed
    WindowId window_id = GetWindowIdFromHwnd(hwnd);

    if (window_id != IdAllocator::kInvalidId) {
      // Try to get existing window from registry
      auto window = WindowRegistry::GetInstance().Get(window_id);
      if (window) {
        windows.push_back(window);
      }
    }
  }

  return windows;
}

std::shared_ptr<Window> WindowManager::GetCurrent() {
  HWND hwnd = GetActiveWindow();
  if (hwnd) {
    WindowId window_id = GetWindowIdFromHwnd(hwnd);
    if (window_id != IdAllocator::kInvalidId) {
      return Get(window_id);
    }
  }
  return nullptr;
}

namespace {

// Library positions are physical pixels divided by the scale factor of the
// monitor they fall on, so a logical point maps back to whichever monitor
// contains it once scaled by that monitor's own factor.
struct LogicalPointSearch {
  Point logical;
  POINT physical;
  bool found;
};

BOOL CALLBACK FindMonitorForLogicalPoint(HMONITOR monitor, HDC, LPRECT rect, LPARAM data) {
  auto* search = reinterpret_cast<LogicalPointSearch*>(data);
  double scale = GetScaleFactorForMonitor(monitor);
  if (scale <= 0.0) {
    scale = 1.0;
  }
  POINT candidate = {static_cast<LONG>(std::lround(search->logical.x * scale)),
                     static_cast<LONG>(std::lround(search->logical.y * scale))};
  if (PtInRect(rect, candidate)) {
    search->physical = candidate;
    search->found = true;
    return FALSE;
  }
  return TRUE;
}

POINT LogicalToPhysicalScreenPoint(Point point) {
  LogicalPointSearch search = {point, {0, 0}, false};
  EnumDisplayMonitors(nullptr, nullptr, FindMonitorForLogicalPoint,
                      reinterpret_cast<LPARAM>(&search));
  if (search.found) {
    return search.physical;
  }
  POINT origin = {0, 0};
  double scale = GetScaleFactorForMonitor(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
  if (scale <= 0.0) {
    scale = 1.0;
  }
  return {static_cast<LONG>(std::lround(point.x * scale)),
          static_cast<LONG>(std::lround(point.y * scale))};
}

// Whether `hwnd`, a top-level window, visibly covers `point`. Used below the
// window excluded from a hit test, where the system's own hit test cannot look.
bool CoversPoint(HWND hwnd, POINT point) {
  if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
    return false;
  }
  LONG ex_style = GetWindowLongW(hwnd, GWL_EXSTYLE);
  if (ex_style & WS_EX_LAYERED) {
    if (ex_style & WS_EX_TRANSPARENT) {
      return false;  // Click-through overlay.
    }
    BYTE alpha = 255;
    DWORD flags = 0;
    if (GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) &&
        alpha == 0) {
      return false;  // Fully transparent.
    }
  }
  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
      cloaked) {
    return false;  // On another virtual desktop, or a suspended UWP frame.
  }
  // The extended frame excludes the invisible resize borders that
  // GetWindowRect() includes on Windows 10 and later.
  RECT rect;
  if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
    if (!GetWindowRect(hwnd, &rect)) {
      return false;
    }
  }
  return PtInRect(&rect, point) != FALSE;
}

}  // namespace

std::shared_ptr<Window> WindowManager::GetWindowAtPoint(Point point, WindowId excluded_window_id) {
  HWND excluded = nullptr;
  if (excluded_window_id != 0) {
    if (auto window = Get(excluded_window_id)) {
      excluded = static_cast<HWND>(window->GetNativeObject());
    }
  }
  const POINT physical = LogicalToPhysicalScreenPoint(point);

  // Ask the system first: WindowFromPoint() is the window a click would reach,
  // so it looks through click-through overlays (layered windows with
  // transparent pixels, like game or capture overlays) that cover the screen.
  HWND found = WindowFromPoint(physical);
  if (found) {
    found = GetAncestor(found, GA_ROOT);
  }
  if (found && found == excluded) {
    // The excluded window, typically the one being dragged, is right under the
    // point. Walk down the stack beneath it instead.
    found = nullptr;
    for (HWND below = GetWindow(excluded, GW_HWNDNEXT); below;
         below = GetWindow(below, GW_HWNDNEXT)) {
      if (CoversPoint(below, physical)) {
        found = below;
        break;
      }
    }
  }
  // Another application's window on top means the point is covered.
  if (!found || !IsOwnProcessWindow(found)) {
    return nullptr;
  }
  WindowId window_id = GetWindowIdFromHwnd(found);
  if (window_id == IdAllocator::kInvalidId) {
    return nullptr;
  }
  return Get(window_id);
}

void WindowManager::SetWillShowHook(std::optional<WindowWillShowHook> hook) {
  pimpl_->will_show_hook_ =
      hook && *hook ? std::make_shared<WindowWillShowHook>(std::move(*hook)) : nullptr;

  bool has_any_hook = pimpl_->will_show_hook_ || pimpl_->will_hide_hook_;
  has_any_hook ? InstallHooks() : UninstallHooks();
}

void WindowManager::SetWillHideHook(std::optional<WindowWillHideHook> hook) {
  pimpl_->will_hide_hook_ =
      hook && *hook ? std::make_shared<WindowWillHideHook>(std::move(*hook)) : nullptr;

  bool has_any_hook = pimpl_->will_show_hook_ || pimpl_->will_hide_hook_;
  has_any_hook ? InstallHooks() : UninstallHooks();
}

bool WindowManager::HasWillShowHook() const {
  return pimpl_->will_show_hook_ != nullptr;
}

bool WindowManager::HasWillHideHook() const {
  return pimpl_->will_hide_hook_ != nullptr;
}

void WindowManager::HandleWillShow(WindowId id) {
  // The hook may replace or clear itself. Keep this invocation alive.
  auto hook = pimpl_->will_show_hook_;
  if (hook) {
    (*hook)(id);
  }
}

void WindowManager::HandleWillHide(WindowId id) {
  // The hook may replace or clear itself. Keep this invocation alive.
  auto hook = pimpl_->will_hide_hook_;
  if (hook) {
    (*hook)(id);
  }
}

bool WindowManager::CallOriginalShow(WindowId id) {
  auto window = Get(id);
  if (!window) {
    return false;
  }
  void* native = window->GetNativeObject();
  if (!native) {
    return false;
  }
  HWND hwnd = static_cast<HWND>(native);
  // On Windows, call the original ShowWindow through the function pointer
  if (g_original_show_window) {
    return g_original_show_window(hwnd, SW_SHOW) != FALSE;
  }
  return false;
}

bool WindowManager::CallOriginalHide(WindowId id) {
  auto window = Get(id);
  if (!window) {
    return false;
  }
  void* native = window->GetNativeObject();
  if (!native) {
    return false;
  }
  HWND hwnd = static_cast<HWND>(native);
  // On Windows, call the original ShowWindow through the function pointer
  if (g_original_show_window) {
    return g_original_show_window(hwnd, SW_HIDE) != FALSE;
  }
  return false;
}

void WindowManager::StartEventListening() {
  pimpl_->StartEventListening();
}

void WindowManager::StopEventListening() {
  pimpl_->StopEventListening();
}

void WindowManager::DispatchWindowEvent(const WindowEvent& event) {
  Emit(event);
}

}  // namespace nativeapi
