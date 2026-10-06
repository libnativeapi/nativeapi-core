#include "window_property_dispatch.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "window_manager.h"

namespace nativeapi::detail {
namespace {

struct Readings {
  std::string title;
  bool resizable = false;
  bool movable = false;
  bool minimizable = false;
  bool maximizable = false;
  bool full_screenable = false;
  bool closable = false;
  bool control_buttons_visible = false;
  bool always_on_top = false;
  bool always_on_bottom = false;
  TitleBarStyle title_bar_style = TitleBarStyle::Normal;
};

Readings Read(const Window& window) {
  Readings r;
  r.title = window.GetTitle();
  r.resizable = window.IsResizable();
  r.movable = window.IsMovable();
  r.minimizable = window.IsMinimizable();
  r.maximizable = window.IsMaximizable();
  r.full_screenable = window.IsFullScreenable();
  r.closable = window.IsClosable();
  r.control_buttons_visible = window.IsWindowControlButtonsVisible();
  r.always_on_top = window.IsAlwaysOnTop();
  r.always_on_bottom = window.IsAlwaysOnBottom();
  r.title_bar_style = window.GetTitleBarStyle();
  return r;
}

std::vector<WindowProperty> Changes(const Readings& a, const Readings& b) {
  std::vector<WindowProperty> changed;
  if (a.title != b.title) changed.push_back(WindowProperty::Title);
  if (a.resizable != b.resizable) changed.push_back(WindowProperty::Resizable);
  if (a.movable != b.movable) changed.push_back(WindowProperty::Movable);
  if (a.minimizable != b.minimizable) changed.push_back(WindowProperty::Minimizable);
  if (a.maximizable != b.maximizable) changed.push_back(WindowProperty::Maximizable);
  if (a.full_screenable != b.full_screenable) changed.push_back(WindowProperty::FullScreenable);
  if (a.closable != b.closable) changed.push_back(WindowProperty::Closable);
  if (a.control_buttons_visible != b.control_buttons_visible)
    changed.push_back(WindowProperty::WindowControlButtonsVisible);
  if (a.always_on_top != b.always_on_top) changed.push_back(WindowProperty::AlwaysOnTop);
  if (a.always_on_bottom != b.always_on_bottom) changed.push_back(WindowProperty::AlwaysOnBottom);
  if (a.title_bar_style != b.title_bar_style) changed.push_back(WindowProperty::TitleBarStyle);
  return changed;
}

std::mutex g_mutex;
std::unordered_map<WindowId, Readings>& Store() {
  static auto* store = new std::unordered_map<WindowId, Readings>();  // Outlives exit.
  return *store;
}
thread_local int g_scope_depth = 0;

}  // namespace

void WindowPropertyDispatch::Refresh(const Window& window) {
  if (g_scope_depth > 0) return;
  const WindowId id = window.GetId();
  if (id == IdAllocator::kInvalidId) return;
  Readings now = Read(window);  // Getters run outside the lock.
  std::vector<WindowProperty> changed;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto [entry, inserted] = Store().try_emplace(id, now);
    if (!inserted) {
      changed = Changes(entry->second, now);
      entry->second = std::move(now);
    }
  }
  if (changed.empty()) return;
  auto& manager = WindowManager::GetInstance();
  for (auto property : changed) {
    try {
      manager.DispatchWindowEvent(WindowPropertyChangedEvent(id, property));
    } catch (...) {
    }
  }
}

void WindowPropertyDispatch::Forget(WindowId id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  Store().erase(id);
}

WindowPropertyScope::WindowPropertyScope(const Window& window) : window_(window) {
  if (g_scope_depth++ > 0) return;
  const WindowId id = window.GetId();
  if (id == IdAllocator::kInvalidId) return;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (Store().count(id)) return;
  }
  Readings before = Read(window);
  std::lock_guard<std::mutex> lock(g_mutex);
  Store().try_emplace(id, std::move(before));
}

WindowPropertyScope::~WindowPropertyScope() {
  if (--g_scope_depth > 0) return;
  try {
    WindowPropertyDispatch::Refresh(window_);
  } catch (...) {
  }
}

}  // namespace nativeapi::detail
