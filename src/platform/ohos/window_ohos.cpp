#ifdef __OHOS__
#include <hilog/log.h>
#endif
#include <cmath>
#include <array>
#include <cstdio>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "../../display_manager.h"
#include "../../foundation/id_allocator.h"
#include "../../window.h"
#include "../../window_manager.h"
#include "../../window_property_dispatch.h"
#include "ohos_application.h"
#include "ohos_napi.h"
#include "ohos_runtime.h"
#include "ohos_sdk_request.h"
#include "ohos_window.h"
#include "ohos_window_mask.h"
#include "ohos_window_modes.h"
#include "ohos_window_shadow.h"

namespace nativeapi {

// The ArkTS host owns native windows; wrappers share a borrowed, invalidatable token.
class Window::Impl {
 public:
  explicit Impl(void* token) : state_(ohos::FindWindowState(token)) {}
  std::shared_ptr<ohos::WindowState> state_;
};
Window::Window() : Window(nullptr) {}
Window::Window(void* token) : pimpl_(std::make_unique<Impl>(token)) {}
Window::~Window() {
  ShutdownEmitter();
}
WindowNumber Window::GetNativeNumber() const {
  return 0;
}

WindowId Window::GetId() const {
  return pimpl_->state_ ? pimpl_->state_->id : IdAllocator::kInvalidId;
}

namespace {
using State = std::shared_ptr<ohos::WindowState>;
void CancelAspectRequests(const State& state) {
  while (!state->aspect_requests.empty()) {
    auto request = state->aspect_requests.front().second;
    state->aspect_requests.pop_front();
    request->Finish(false, false);
  }
}
void ApplyNextAspectRatio(const State& state) try {
  if (state->aspect_requests.empty()) {
    state->cancel_aspect = {};
    return;
  }
  if (!state->alive || state->destroying) {
    auto cancel = state->cancel_aspect;
    if (cancel)
      cancel();
    return;
  }
  const double ratio = state->aspect_requests.front().first;
  auto request = state->aspect_requests.front().second;
  request->completed = [weak = std::weak_ptr<ohos::WindowState>(state),
                        operation = std::weak_ptr<ohos::SdkRequest>(request), ratio](bool ok) {
    auto current = weak.lock();
    if (!current || !current->alive || current->aspect_requests.empty() ||
        current->aspect_requests.front().second != operation.lock())
      return;
    if (ok)
      current->aspect_ratio = ratio;
    current->aspect_requests.pop_front();
    ApplyNextAspectRatio(current);
  };
  if (ratio > 0)
    request->Start(state->env, "setContentAspectRatio", ohos::WindowObject(state),
                   {ohos::Number(state->env, ratio), ohos::Boolean(state->env, false),
                    ohos::Boolean(state->env, false)});
  else
    request->Start(state->env, "resetAspectRatio", ohos::WindowObject(state));
} catch (...) {
  CancelAspectRequests(state);
}
bool Property(const State& state, bool WindowManager_WindowProperties::* member) {
  WindowManager_WindowProperties properties{};
  return ohos::ReadWindow(state, properties) && properties.*member;
}
void Policy(const State& state, const char* method, bool value, bool ohos::WindowState::* member) {
  ohos::UseWindow(state, [&] {
    ohos::CallWindow(state, method, {ohos::Boolean(state->env, value)},
                     [state, member, value](bool ok, napi_value) {
                       if (ok) {
                         state.get()->*member = value;
                         ohos::RefreshWindowProperties(state);
                       }
                     });
  });
}
bool Cached(const State& state, bool ohos::WindowState::* member) {
  bool result = false;
  ohos::UseWindow(state, [&] { result = state.get()->*member; });
  return result;
}
bool ControlAreaVisible(const State& state) {
  napi_value rect = nullptr, width = nullptr, height = nullptr;
  double wide = 0, high = 0;
  const bool visible = ohos::js::Call(state->env, ohos::WindowObject(state), "getTitleButtonRect", {}, rect) &&
      ohos::js::Property(state->env, rect, "width", width) &&
      ohos::js::Property(state->env, rect, "height", height) &&
      napi_get_value_double(state->env, width, &wide) == napi_ok &&
      napi_get_value_double(state->env, height, &high) == napi_ok &&
      std::isfinite(wide) && std::isfinite(high) && wide > 0 && high > 0;
  return visible;
}
void ApplyControlButtons(const State& state, std::array<bool,3> policy, bool visible) {
  ohos::CallWindow(state, "setWindowTitleButtonVisible",
      {ohos::Boolean(state->env, visible && policy[1]),
       ohos::Boolean(state->env, visible && policy[0]),
       ohos::Boolean(state->env, visible && policy[2])},
      [state, policy, visible](bool ok, napi_value) {
        if (!ok) return;
        state->minimizable = policy[0];
        state->maximizable = policy[1];
        state->closable = policy[2];
        state->control_buttons = visible;
        state->control_policy_known = true;
        ohos::RefreshWindowProperties(state);
      });
}
void ControlPolicy(Window& window, const State& state, size_t index, bool value) {
  ohos::UseWindow(state, [&] {
    if (state->destroying) return;
    ohos::ObserveWindowControlButtons(state);
    detail::WindowPropertyScope scope(window);
    std::array<bool,3> policy{state->minimizable, state->maximizable, state->closable};
    policy[index] = value;
    ApplyControlButtons(state, policy, state->control_buttons);
  });
}
bool ControlPolicy(const State& state, bool ohos::WindowState::* member) {
  bool result = false;
  ohos::UseWindow(state, [&] {
    // There is no individual SDK getter. Retain stable default/applied policies,
    // independent of when the compositor happens to publish the button layout.
    result = state->control_policy_known && state.get()->*member;
  });
  return result;
}
bool WindowSessionSupported() {
  bool supported = false;
  ohos::WithEnvironment([&](napi_env env) {
    napi_value global = nullptr, result = nullptr;
    if (napi_get_global(env, &global) == napi_ok &&
        ohos::js::Call(env, global, "canIUse",
                       {ohos::String(env, "SystemCapability.Window.SessionManager")}, result))
      napi_get_value_bool(env, result, &supported);
  });
  return supported;
}
bool Pixel(double value, double scale, bool dimension = false) {
  return std::isfinite(value) && std::isfinite(value * scale) &&
         value * scale >= (dimension ? 1.0 : std::numeric_limits<int32_t>::min()) &&
         value * scale <= std::numeric_limits<int32_t>::max();
}
Size Limits(const State& state, bool maximum) {
  Size result{};
  ohos::UseWindow(state, [&] {
    ohos::CallWindow(state, "getWindowLimits", {}, [&](bool ok, napi_value limits) {
      if (!ok)
        return;
      napi_value w, h;
      napi_get_named_property(state->env, limits, maximum ? "maxWidth" : "minWidth", &w);
      napi_get_named_property(state->env, limits, maximum ? "maxHeight" : "minHeight", &h);
      napi_get_value_double(state->env, w, &result.width);
      napi_get_value_double(state->env, h, &result.height);
      const auto scale = ohos::WindowScale(state);
      result.width /= scale;
      result.height /= scale;
    });
  });
  return result;
}
void SetLimits(const State& state, Size size, bool maximum) {
  ohos::UseWindow(state, [&] {
    const auto scale = ohos::WindowScale(state);
    if (!Pixel(size.width, scale, true) || !Pixel(size.height, scale, true))
      return;
    napi_value limits;
    napi_create_object(state->env, &limits);
    napi_set_named_property(state->env, limits, maximum ? "maxWidth" : "minWidth",
                            ohos::Number(state->env, std::round(size.width * scale)));
    napi_set_named_property(state->env, limits, maximum ? "maxHeight" : "minHeight",
                            ohos::Number(state->env, std::round(size.height * scale)));
    ohos::CallWindow(state, "setWindowLimits", {limits});
  });
}
}  // namespace

void Window::Focus() {
  if (!IsFocusable())
    return;
  ohos::ShowWindow(pimpl_->state_, !Cached(pimpl_->state_, &ohos::WindowState::non_activating));
}

void Window::Blur() {
  ohos::BlurWindow(pimpl_->state_);
}

bool Window::IsFocused() const {
  return ohos::WindowBoolean(pimpl_->state_, "isFocused");
}

void Window::Show() {
  ohos::ShowWindow(pimpl_->state_, !Cached(pimpl_->state_, &ohos::WindowState::non_activating));
}

void Window::ShowInactive() {
  ohos::ShowWindow(pimpl_->state_, false);
}

void Window::Hide() {
  ohos::CallWindow(pimpl_->state_, "minimize");
}

bool Window::IsVisible() const {
  return !ohos::ApplicationHidesWindow(pimpl_->state_) &&
         ohos::WindowBoolean(pimpl_->state_, "isWindowShowing");
}

WindowOcclusionState Window::GetOcclusionState() const {
  return ohos::WindowOcclusion(pimpl_->state_);
}

bool Window::IsOcclusionStateSupported() {
  return WindowSessionSupported();
}

void Window::Maximize() {
  // The SDK default enters full screen. Maximize keeps the desktop bars visible.
  auto state = pimpl_->state_;
  ohos::UseWindow(state,
                  [&] { ohos::CallWindow(state, "maximize", {ohos::Number(state->env, 1)}); });
}

void Window::Unmaximize() {
  ohos::CallWindow(pimpl_->state_, "recover");
}

bool Window::IsMaximized() const {
  return ohos::WindowStatus(pimpl_->state_) == 2;
}

void Window::Minimize() {
  ohos::CallWindow(pimpl_->state_, "minimize");
}

void Window::Restore() {
  if (ohos::WindowStatus(pimpl_->state_) == 2)
    Unmaximize();
  else if (ohos::IsDesktopMainWindow(pimpl_->state_))
    ohos::CallWindow(pimpl_->state_, "restore");
  else
    Show();
}

bool Window::IsMinimized() const {
  return ohos::WindowStatus(pimpl_->state_) == 3;
}

void Window::SetFullScreen(bool is_full_screen) {
  auto state = pimpl_->state_;
  ohos::UseWindow(state, [&] {
    auto refresh = [state](bool ok, napi_value) {
      if (ok)
        ohos::RefreshWindowFullscreen(state);
    };
    if (ohos::IsDesktopMainWindow(state) || ohos::IsFreeWindowMode(state)) {
      const int status = ohos::WindowStatus(state);
      if (is_full_screen && status != 1) {
        state->fullscreen_restore_maximized = status == 2;
        ohos::CallWindow(state, "maximize", {ohos::Number(state->env, 2)}, refresh);
      } else if (!is_full_screen && status == 1) {
        if (state->fullscreen_restore_maximized)
          ohos::CallWindow(state, "maximize", {ohos::Number(state->env, 1)}, refresh);
        else
          ohos::CallWindow(state, "recover", {}, refresh);
      }
      return;
    }
    ohos::CallWindow(state, "setWindowLayoutFullScreen",
                     {ohos::Boolean(state->env, is_full_screen)}, refresh);
    napi_value bars;
    napi_create_array(state->env, &bars);
    if (!is_full_screen) {
      napi_set_element(state->env, bars, 0, ohos::String(state->env, "status"));
      napi_set_element(state->env, bars, 1, ohos::String(state->env, "navigation"));
    }
    ohos::CallWindow(state, "setWindowSystemBarEnable", {bars}, refresh);
  });
}

bool Window::IsFullScreen() const {
  return ohos::WindowFullscreen(pimpl_->state_);
}

void Window::SetBounds(Rectangle bounds) {
  SetPosition({bounds.x, bounds.y});
  SetSize({bounds.width, bounds.height}, false);
}

Rectangle Window::GetBounds() const {
  return ohos::WindowBounds(pimpl_->state_);
}

void Window::SetSize(Size size, bool animate) {
  auto state = pimpl_->state_;
  ohos::UseWindow(state, [&] {
    const auto scale = ohos::WindowScale(state);
    if (!Pixel(size.width, scale, true) || !Pixel(size.height, scale, true))
      return;
    ohos::CallWindow(state, "resize",
                     {ohos::Number(state->env, std::round(size.width * scale)),
                      ohos::Number(state->env, std::round(size.height * scale))});
  });
}

Size Window::GetSize() const {
  const auto r = GetBounds();
  return {r.width, r.height};
}

void Window::SetContentSize(Size size) {
  const auto frame = GetSize(), content = GetContentSize();
  SetSize({size.width + frame.width - content.width, size.height + frame.height - content.height},
          false);
}

Size Window::GetContentSize() const {
  const auto r = GetContentBounds();
  return {r.width, r.height};
}

void Window::SetContentBounds(Rectangle bounds) {
  const auto frame = GetBounds(), content = GetContentBounds();
  SetPosition({bounds.x - (content.x - frame.x), bounds.y - (content.y - frame.y)});
  SetContentSize({bounds.width, bounds.height});
}

Rectangle Window::GetContentBounds() const {
  return ohos::WindowBounds(pimpl_->state_, true);
}

void Window::SetMinimumSize(Size size) {
  SetLimits(pimpl_->state_, size, false);
}

Size Window::GetMinimumSize() const {
  return Limits(pimpl_->state_, false);
}

void Window::SetMaximumSize(Size size) {
  SetLimits(pimpl_->state_, size, true);
}

Size Window::GetMaximumSize() const {
  return Limits(pimpl_->state_, true);
}

void Window::SetResizable(bool is_resizable) {
  Policy(pimpl_->state_, "setResizeByDragEnabled", is_resizable, &ohos::WindowState::resizable);
}

bool Window::IsResizable() const {
  return Cached(pimpl_->state_, &ohos::WindowState::resizable);
}

void Window::SetMovable(bool is_movable) {
  Policy(pimpl_->state_, "setWindowTitleMoveEnabled", is_movable, &ohos::WindowState::movable);
}

bool Window::IsMovable() const {
  return Cached(pimpl_->state_, &ohos::WindowState::movable);
}

void Window::SetMinimizable(bool is_minimizable) {
  ControlPolicy(*this, pimpl_->state_, 0, is_minimizable);
}

bool Window::IsMinimizable() const {
  return ControlPolicy(pimpl_->state_, &ohos::WindowState::minimizable);
}

void Window::SetMaximizable(bool is_maximizable) {
  ControlPolicy(*this, pimpl_->state_, 1, is_maximizable);
}

bool Window::IsMaximizable() const {
  return ControlPolicy(pimpl_->state_, &ohos::WindowState::maximizable);
}

void Window::SetFullScreenable(bool is_full_screenable) {
  ohos::SetWindowFullScreenable(pimpl_->state_, is_full_screenable);
}

bool Window::IsFullScreenable() const {
  return ohos::WindowFullScreenable(pimpl_->state_);
}

void Window::SetClosable(bool is_closable) {
  ControlPolicy(*this, pimpl_->state_, 2, is_closable);
}

bool Window::IsClosable() const {
  return ControlPolicy(pimpl_->state_, &ohos::WindowState::closable);
}

void Window::SetWindowControlButtonsVisible(bool is_visible) {
  ohos::UseWindow(pimpl_->state_, [&] {
    ohos::ObserveWindowControlButtons(pimpl_->state_);
    detail::WindowPropertyScope property_scope(*this);
    if (!pimpl_->state_->destroying)
      ApplyControlButtons(pimpl_->state_,
          {pimpl_->state_->minimizable, pimpl_->state_->maximizable, pimpl_->state_->closable},
          is_visible);
  });
}

bool Window::IsWindowControlButtonsVisible() const {
  bool visible = false;
  auto state = pimpl_->state_;
  ohos::UseWindow(state, [&] {
    visible = ControlAreaVisible(state);
  });
  return visible;
}

void Window::SetAlwaysOnTop(bool is_always_on_top) {
  Policy(pimpl_->state_, "setWindowTopmost", is_always_on_top, &ohos::WindowState::always_on_top);
}

bool Window::IsAlwaysOnTop() const {
  return Cached(pimpl_->state_, &ohos::WindowState::always_on_top);
}

void Window::SetAlwaysOnBottom(bool is_always_on_bottom) {
  // not supported on OpenHarmony
}

bool Window::IsAlwaysOnBottom() const {
  return false;
}

namespace {
State ParentState(const State& state) {
  if (!state || !state->alive || state->primary)
    return nullptr;
  napi_value object = ohos::WindowObject(state), method = nullptr;
  napi_valuetype type = napi_undefined;
  if (!ohos::js::Property(state->env, object, "getParentWindow", method) ||
      napi_typeof(state->env, method, &type) != napi_ok || type != napi_function) {
    // Before API 19 the SDK has no reparent operation or live parent getter.
    for (const auto& candidate : ohos::WindowStates())
      if (candidate->id == state->parent && candidate->env == state->env && candidate->alive)
        return candidate;
    return nullptr;
  }
  napi_value parent = nullptr, properties = nullptr, value = nullptr;
  int32_t system_id = 0;
  if (!ohos::js::Call(state->env, object, "getParentWindow", {}, parent) ||
      !ohos::js::Call(state->env, parent, "getWindowProperties", {}, properties) ||
      !ohos::js::Property(state->env, properties, "id", value) ||
      napi_get_value_int32(state->env, value, &system_id) != napi_ok)
    return nullptr;
  for (const auto& candidate : ohos::WindowStates())
    if (candidate->env == state->env && candidate->alive && candidate->system_id == system_id)
      return candidate;
  return nullptr;
}
}  // namespace

bool Window::SetParentWindow(std::shared_ptr<Window> parent) try {
  const auto state = pimpl_->state_;
  const auto next = parent ? parent->pimpl_->state_ : nullptr;
  if (!state || !next)
    return false;  // The SDK cannot detach an application subwindow from its parent.
  auto request = std::make_shared<ohos::SdkRequest>();
  bool submitted = false;
  ohos::UseWindow(state, [&] {
    if (state->primary || state == next || state->parent_pending || state->destroying ||
        !next->alive || next->destroying || next->env != state->env)
      return;
    // Inspect live SDK ancestry: the embedding host can change it independently.
    auto cursor = next;
    size_t remaining = ohos::WindowStates().size();
    while (cursor && remaining--) {
      if (cursor == state || cursor->parent_pending)
        return;
      cursor = ParentState(cursor);
    }
    if (cursor)
      return;
    napi_value id = nullptr;
    if (napi_create_int32(state->env, next->system_id, &id) != napi_ok)
      return;
    const std::vector<napi_value> args{id};
    request->completed = [weak = std::weak_ptr<ohos::WindowState>(state),
                          next_weak = std::weak_ptr<ohos::WindowState>(next)](bool ok) {
      if (auto current = weak.lock(); current && current->alive) {
        current->parent_pending = false;
        current->cancel_parent = {};
        if (auto target = next_weak.lock(); ok && target && target->alive)
          current->parent = target->id;
      }
    };
    auto cancel = [weak = std::weak_ptr<ohos::SdkRequest>(request)] {
      if (auto pending = weak.lock())
        pending->Finish(false, false);
    };
    state->cancel_parent = std::move(cancel);
    state->parent_pending = true;
    try {
      submitted = request->Start(state->env, "setParentWindow", ohos::WindowObject(state), args);
    } catch (...) {
      request->Finish(false);
    }
  });
  return request->Result(submitted) && state->alive && next->alive;
} catch (...) {
  return false;
}

std::shared_ptr<Window> Window::GetParentWindow() const try {
  std::shared_ptr<Window> result;
  ohos::UseWindow(pimpl_->state_, [&] {
    if (const auto parent = ParentState(pimpl_->state_))
      result = WindowManager::GetInstance().Get(parent->id);
  });
  return result;
} catch (...) {
  return nullptr;
}

void Window::SetAspectRatio(double aspect_ratio) {
  if (!std::isfinite(aspect_ratio))
    return;
  try {
    auto state = pimpl_->state_;
    auto request = std::make_shared<ohos::SdkRequest>();
    bool submitted = false;
    ohos::UseWindow(state, [&] {
      if (state->destroying)
        return;
      const bool idle = state->aspect_requests.empty();
      state->cancel_aspect = [weak = std::weak_ptr<ohos::WindowState>(state)] {
        if (auto current = weak.lock())
          CancelAspectRequests(current);
      };
      state->aspect_requests.emplace_back(aspect_ratio > 0 ? aspect_ratio : 0, request);
      submitted = true;
      if (idle)
        ApplyNextAspectRatio(state);
    });
    request->Result(submitted);
  } catch (...) {
  }
}

double Window::GetAspectRatio() const {
  double ratio = 0;
  ohos::UseWindow(pimpl_->state_, [&] { ratio = pimpl_->state_->aspect_ratio; });
  return ratio;
}

void Window::SetNonActivating(bool is_non_activating) {
  ohos::UseWindow(pimpl_->state_, [&] { pimpl_->state_->non_activating = is_non_activating; });
}

bool Window::IsNonActivating() const {
  return Cached(pimpl_->state_, &ohos::WindowState::non_activating);
}

void Window::SetPosition(Point point) {
  auto state = pimpl_->state_;
  ohos::UseWindow(state, [&] {
    const auto scale = ohos::WindowScale(state);
    if (!Pixel(point.x, scale) || !Pixel(point.y, scale))
      return;
    ohos::CallWindow(state, "moveWindowTo",
                     {ohos::Number(state->env, std::round(point.x * scale)),
                      ohos::Number(state->env, std::round(point.y * scale))});
  });
}

Point Window::GetPosition() const {
  const auto r = GetBounds();
  return {r.x, r.y};
}

void Window::Center() {
  auto display = DisplayManager::GetInstance().GetPrimary();
  if (!display)
    return;
  const auto area = display->GetWorkArea();
  const auto size = GetSize();
  SetPosition({area.x + (area.width - size.width) / 2, area.y + (area.height - size.height) / 2});
}

void Window::SetTitle(std::string title) {
  auto state = pimpl_->state_;
  if (title.find('\0') != std::string::npos)
    return;
  ohos::UseWindow(state, [&] {
    ohos::CallWindow(state, "setWindowTitle", {ohos::String(state->env, title)},
                     [state, title](bool ok, napi_value) {
                       if (ok) {
                         state->title = title;
                         ohos::RefreshWindowProperties(state);
                       }
                     });
  });
}

std::string Window::GetTitle() const {
  std::string title;
  ohos::UseWindow(pimpl_->state_, [&] { title = pimpl_->state_->title; });
  return title;
}

bool Window::SetFrameless(bool /*is_frameless*/) { return false; }

bool Window::IsFrameless() const {
  return false;
}

void Window::SetTitleBarStyle(TitleBarStyle style) {
  ohos::UseWindow(pimpl_->state_, [&] {
    ohos::ObserveWindowControlButtons(pimpl_->state_);
    detail::WindowPropertyScope property_scope(*this);
    ohos::CallWindow(pimpl_->state_, "setWindowDecorVisible",
                     {ohos::Boolean(pimpl_->state_->env, style == TitleBarStyle::Normal)},
                     [state = pimpl_->state_, style](bool ok, napi_value) {
                       if (ok) {
                         const bool visible = style == TitleBarStyle::Normal;
                         state->shadow_captionless_without_query = !visible;
                         ApplyControlButtons(state,
                             {state->minimizable, state->maximizable, state->closable}, visible);
                         ohos::RefreshWindowCustomShadow(state);
                       }
                     });
  });
}

TitleBarStyle Window::GetTitleBarStyle() const {
  if (!GetNativeObject())
    return TitleBarStyle::Normal;
  return ohos::WindowBoolean(pimpl_->state_, "getWindowDecorVisible") ? TitleBarStyle::Normal
                                                                      : TitleBarStyle::Hidden;
}

bool Window::SetContentUnderTitleBar(bool is_content_under_title_bar) try {
  const auto state = pimpl_->state_;
  auto request = std::make_shared<ohos::SdkRequest>();
  bool submitted = false;
  ohos::UseWindow(state, [&] {
    if (state->destroying || state->layout_request) return;
    request->completed = [weak = std::weak_ptr<ohos::WindowState>(state),
                          operation = std::weak_ptr<ohos::SdkRequest>(request)](bool ok) {
      if (auto current = weak.lock();
          current && current->layout_request == operation.lock()) {
        current->layout_request.reset();
        if (ok && current->alive) ohos::RefreshWindowFullscreen(current);
      }
    };
    request->read_result = [weak = std::weak_ptr<ohos::WindowState>(state),
                            is_content_under_title_bar](napi_env, napi_value) {
      WindowManager_WindowProperties properties{};
      const auto current = weak.lock();
      return current && current->alive && ohos::ReadWindow(current, properties) &&
             properties.isLayoutFullScreen == is_content_under_title_bar;
    };
    state->layout_request = request;
    submitted = request->Start(state->env, "setWindowLayoutFullScreen",
                               ohos::WindowObject(state),
                               {ohos::Boolean(state->env, is_content_under_title_bar)});
  });
  return request->Result(submitted) && state && state->alive;
} catch (...) { return false; }

bool Window::IsContentUnderTitleBar() const {
  return Property(pimpl_->state_, &WindowManager_WindowProperties::isLayoutFullScreen);
}

bool Window::SetContentProtection(bool is_content_protected) {
  bool ok = false;
  ohos::UseWindow(pimpl_->state_, [&] {
    ok =
        OH_WindowManager_SetWindowPrivacyMode(pimpl_->state_->system_id, is_content_protected) == 0;
  });
  return ok;
}

bool Window::IsContentProtected() const {
  return Property(pimpl_->state_, &WindowManager_WindowProperties::isPrivacyMode);
}

bool Window::IsContentProtectionSupported() {
  return ohos::IsInitialized();
}

bool Window::SetCornerPreference(WindowCornerPreference preference) try {
  switch (preference) {
    case WindowCornerPreference::Default:
    case WindowCornerPreference::DoNotRound:
    case WindowCornerPreference::Round:
    case WindowCornerPreference::RoundSmall:
      break;
    default:
      return false;
  }
  auto state = pimpl_->state_;
  auto request = std::make_shared<ohos::SdkRequest>();
  bool submitted = false;
  ohos::UseWindow(state, [&] {
    if (state->primary || state->corner_pending || state->destroying)
      return;
    if (state->original_corner_radius < 0) {
      napi_value value = nullptr;
      double radius = -1;
      if (!ohos::js::Call(state->env, ohos::WindowObject(state), "getWindowCornerRadius", {},
                          value) ||
          napi_get_value_double(state->env, value, &radius) != napi_ok || !std::isfinite(radius) ||
          radius < 0)
        return;
      state->original_corner_radius = radius;
    }
    const double radius = preference == WindowCornerPreference::Default
                              ? state->original_corner_radius
                          : preference == WindowCornerPreference::DoNotRound ? 0
                          : preference == WindowCornerPreference::RoundSmall ? 4
                                                                             : 8;
    auto cancel = [weak = std::weak_ptr<ohos::SdkRequest>(request)] {
      if (auto current = weak.lock())
        current->Finish(false, false);
    };
    request->completed = [weak = std::weak_ptr<ohos::WindowState>(state), preference](bool ok) {
      auto current = weak.lock();
      if (!current || !current->alive)
        return;
      current->cancel_corner = {};
      current->corner_pending = false;
      if (ok) {
        current->corner_preference = preference;
        ohos::RefreshWindowCustomShadow(current);
      }
    };
    state->cancel_corner = std::move(cancel);
    state->corner_pending = true;
    try {
      submitted = request->Start(state->env, "setWindowCornerRadius", ohos::WindowObject(state),
                                 {ohos::Number(state->env, radius)});
    } catch (...) {
      request->Finish(false);
    }
  });
  return request->Result(submitted) && state && state->alive;
} catch (...) {
  return false;
}

WindowCornerPreference Window::GetCornerPreference() const {
  auto result = WindowCornerPreference::Default;
  ohos::UseWindow(pimpl_->state_, [&] { result = pimpl_->state_->corner_preference; });
  return result;
}

bool Window::IsCornerPreferenceSupported() {
  return WindowSessionSupported();
}

bool Window::IsContentUnderTitleBarSupported() {
  return ohos::IsInitialized();
}

void Window::SetHasShadow(bool has_shadow) try {
  auto state = pimpl_->state_;
  if (ohos::ToggleWindowCustomShadow(state, has_shadow)) return;
  ohos::UseWindow(state, [&] {
    if (state->primary) {
      Policy(state, "setWindowShadowEnabled", has_shadow, &ohos::WindowState::shadow);
      return;
    }
    // There is no public radius getter/reset. Enabling chooses the library's
    // normal 18-logical-pixel blur; disabling sends the documented zero radius.
    ohos::CallWindow(state, "setWindowShadowRadius",
                     {ohos::Number(state->env, has_shadow ? 18 * ohos::WindowScale(state) : 0)},
                     [state, has_shadow](bool ok, napi_value) {
                       if (ok)
                         state->shadow = has_shadow;
                     });
  });
} catch (...) {
}

bool Window::HasShadow() const {
  return Cached(pimpl_->state_, &ohos::WindowState::shadow);
}

void Window::SetOpacity(float opacity) {
  // SetOpacity not supported on OpenHarmony
}

float Window::GetOpacity() const {
  return 1.0f;
}

bool Window::SetVisualEffect(VisualEffect effect) {
  return effect == VisualEffect::None;
}

VisualEffect Window::GetVisualEffect() const {
  return VisualEffect::None;
}

bool Window::IsVisualEffectSupported(VisualEffect effect) {
  return effect == VisualEffect::None;
}

void Window::SetBackgroundColor(const Color& color) {
  auto state = pimpl_->state_;
  ohos::UseWindow(state, [&] {
    char hex[10];
    std::snprintf(hex, sizeof(hex), "#%08X", color.ToARGB());
    if (OH_WindowManager_SetWindowBackgroundColor(state->system_id, hex) == 0)
      state->background = color;
  });
}

Color Window::GetBackgroundColor() const {
  Color result{};
  ohos::UseWindow(pimpl_->state_, [&] { result = pimpl_->state_->background; });
  return result;
}

void Window::SetVisibleOnAllWorkspaces(bool is_visible_on_all_workspaces) {
  // SetVisibleOnAllWorkspaces not supported on OpenHarmony
}

bool Window::IsVisibleOnAllWorkspaces() const {
  return false;
}

void Window::SetVisibleInTaskbar(bool is_visible_in_taskbar) {
  // SetVisibleInTaskbar not supported on OpenHarmony
}

bool Window::IsVisibleInTaskbar() const {
  return false;
}

bool Window::SetIgnoreMouseEvents(bool is_ignore_mouse_events, bool forward) {
  if (forward)
    return false;
  bool ok = false;
  ohos::UseWindow(pimpl_->state_, [&] {
    ok = OH_WindowManager_SetWindowTouchable(pimpl_->state_->system_id, !is_ignore_mouse_events) ==
         0;
  });
  return ok;
}

bool Window::IsIgnoreMouseEvents() const {
  WindowManager_WindowProperties p{};
  return ohos::ReadWindow(pimpl_->state_, p) && !p.touchable;
}
bool Window::IsMouseMoveForwardingEnabled() const {
  return false;
}
bool Window::IsMouseMoveForwardingSupported() {
  return false;
}

void Window::SetFocusable(bool is_focusable) {
  ohos::UseWindow(pimpl_->state_, [&] {
    OH_WindowManager_SetWindowFocusable(pimpl_->state_->system_id, is_focusable);
  });
}

bool Window::IsFocusable() const {
  return Property(pimpl_->state_, &WindowManager_WindowProperties::focusable);
}

bool Window::ShowSystemMenu(Point /*position*/) {
  return false;
}

bool Window::IsSystemMenuSupported() {
  return false;
}

bool Window::SetMaximizeButtonBounds(Rectangle) {
  return false;
}

Rectangle Window::GetMaximizeButtonBounds() const {
  return {0, 0, 0, 0};
}

bool Window::IsMaximizeButtonBoundsSupported() {
  return false;
}

bool Window::PerformTitleBarDoubleClick() try {
  const auto state = pimpl_->state_;
  auto request = std::make_shared<ohos::SdkRequest>();
  bool submitted = false;
  ohos::UseWindow(state, [&] {
    if (state->destroying || state->title_action_request ||
        (state->control_policy_known && !state->maximizable))
      return;
    // A phone child can report FLOATING and fulfill maximize without changing
    // anything. Query the live workspace capability rather than treating its
    // window mode or a fulfilled void promise as evidence of device support.
    if (!ohos::IsFreeWindowMode(state))
      return;
    const int status = ohos::WindowStatus(state);
    const bool recover = status == 2 || status == 5;
    if (!recover && status != 4)
      return;
    std::vector<napi_value> args;
    if (status == 4)
      args.push_back(ohos::Number(state->env, 1));
    request->completed = [weak = std::weak_ptr<ohos::WindowState>(state),
                          operation = std::weak_ptr<ohos::SdkRequest>(request)](bool) {
      if (auto current = weak.lock();
          current && current->title_action_request == operation.lock())
        current->title_action_request.reset();
    };
    state->title_action_request = request;
    try {
      submitted = request->Start(state->env, recover ? "recover" : "maximize",
                                 ohos::WindowObject(state), args);
    } catch (...) {
      request->Finish(false);
    }
  });
  // UI callers submit without blocking the Ability; workers observe the actual
  // SDK completion. Unknown/fullscreen/minimized modes never request a change.
  return request->Result(submitted) && state && state->alive;
} catch (...) {
  return false;
}

void Window::StartDragging() {
  ohos::UseWindow(pimpl_->state_, [&] {
    if (!pimpl_->state_->destroying)
      ohos::CallWindow(pimpl_->state_, "startMoving");
  });
}

void Window::StartResizing(ResizeEdge edge) {
  // StartResizing not supported on OpenHarmony
}

void* Window::GetNativeObjectInternal() const {
  return pimpl_->state_ && pimpl_->state_->alive ? pimpl_->state_.get() : nullptr;
}

bool Window::SetShape(std::shared_ptr<WindowShape> shape) try {
  // Snapshot the value before dispatching; subsequent builder edits cannot change this request.
  auto polygon = shape ? std::make_shared<WindowShape>(*shape) : nullptr;
  if (polygon && polygon->GetPointCount() < 3)
    return false;
  auto state = pimpl_->state_;
  auto request = std::make_shared<ohos::SdkRequest>();
  bool submitted = false;
  ohos::UseWindow(state, [&] {
    if (state->primary || state->destroying || state->shape_pending ||
        (polygon && GetTitleBarStyle() != TitleBarStyle::Hidden))
      return;
    napi_value object = ohos::WindowObject(state), method = nullptr;
    napi_valuetype type = napi_undefined;
    if (!ohos::js::Property(state->env, object, "clearWindowMask", method) ||
        napi_typeof(state->env, method, &type) != napi_ok || type != napi_function)
      return;  // API 24 adds the real inverse operation; older runtimes cannot clear a mask.
    std::string name = "clearWindowMask";
    std::vector<napi_value> arguments;
    napi_value mask_value = nullptr;
    request->env = state->env;
    if (polygon) {
      // Window properties can lag a resize promise. Use the matching SDK view
      // of the current size; a concurrent resize rejection preserves the old mask state.
      napi_value properties = nullptr, rect = nullptr, w = nullptr, h = nullptr;
      double wide = 0, high = 0;
      if (!ohos::js::Call(state->env, object, "getWindowProperties", {}, properties) ||
          !ohos::js::Property(state->env, properties, "windowRect", rect) ||
          !ohos::js::Property(state->env, rect, "width", w) ||
          !ohos::js::Property(state->env, rect, "height", h) ||
          napi_get_value_double(state->env, w, &wide) != napi_ok ||
          napi_get_value_double(state->env, h, &high) != napi_ok || !std::isfinite(wide) ||
          !std::isfinite(high) || wide < 1 || high < 1 || wide > 16384 || high > 16384 ||
          std::floor(wide) != wide || std::floor(high) != high)
        return;
      const auto width = static_cast<uint32_t>(wide), height = static_cast<uint32_t>(high);
      std::vector<uint8_t> mask;
      if (!ohos::RasterizeWindowMask(*polygon, width, height, ohos::WindowScale(state), mask))
        return;
      napi_value value = nullptr;
      if (ohos::js::Property(state->env, object, "setWindowMaskWithAlpha", method) &&
          napi_typeof(state->env, method, &type) == napi_ok && type == napi_function) {
        void* bytes = nullptr;
        napi_value buffer = nullptr;
        if (napi_create_arraybuffer(state->env, mask.size(), &bytes, &buffer) != napi_ok ||
            !bytes ||
            napi_create_typedarray(state->env, napi_uint8_array, mask.size(), buffer, 0, &value) !=
                napi_ok)
          return;
        std::copy(mask.begin(), mask.end(), static_cast<uint8_t*>(bytes));
        name = "setWindowMaskWithAlpha";
        arguments = {value, ohos::Number(state->env, width), ohos::Number(state->env, height)};
      } else {
        // The legacy nested JS array costs much more memory than one byte per pixel.
        if (mask.size() > 1024 * 1024 ||
            napi_create_array_with_length(state->env, height, &value) != napi_ok)
          return;
        for (uint32_t y = 0; y < height; ++y) {
          napi_value row = nullptr;
          if (napi_create_array_with_length(state->env, width, &row) != napi_ok)
            return;
          for (uint32_t x = 0; x < width; ++x)
            if (napi_set_element(state->env, row, x,
                                 ohos::Number(state->env, mask[size_t(y) * width + x] ? 1 : 0)) !=
                napi_ok)
              return;
          if (napi_set_element(state->env, value, y, row) != napi_ok)
            return;
        }
        name = "setWindowMask";
        arguments = {value};
      }
      mask_value = value;
    }
    auto cancel = [weak = std::weak_ptr<ohos::SdkRequest>(request)] {
      if (auto current = weak.lock())
        current->Finish(false, false);
    };
    request->completed = [weak = std::weak_ptr<ohos::WindowState>(state), polygon](
                             bool ok) {
      if (auto current = weak.lock(); current && current->alive) {
        current->shape_pending = false;
        current->cancel_shape = {};
        if (ok) {
          current->shaped = bool(polygon);
          current->applied_shape = polygon;
          ohos::RefreshWindowCustomShadow(current);
        }
      }
    };
    if (mask_value && !request->Keep(mask_value))
      return;
    state->cancel_shape = std::move(cancel);
    state->shape_pending = true;
    submitted = request->Start(state->env, name, object, arguments);
  });
  return request->Result(submitted) && state && state->alive;
} catch (...) {
  return false;
}
bool Window::IsShaped() const {
  return Cached(pimpl_->state_, &ohos::WindowState::shaped);
}
bool Window::IsShapeSupported() {
  bool supported = false;
  if (WindowSessionSupported())
    ohos::WithEnvironment([&](napi_env env) {
      napi_value info = nullptr, value = nullptr;
      int32_t api = 0;
      supported = ohos::js::Module(env, "@ohos.deviceInfo", info) &&
                  ohos::js::Property(env, info, "sdkApiVersion", value) &&
                  napi_get_value_int32(env, value, &api) == napi_ok && api >= 24;
    });
  return supported;
}

bool Window::SetInputShape(std::shared_ptr<WindowShape> shape) {
  return false;
}
bool Window::IsInputShaped() const {
  return false;
}
bool Window::IsInputShapeSupported() {
  return false;
}

}  // namespace nativeapi

namespace nativeapi {
bool Window::SetTitleBarColors(const Color&, const Color&) {
  return false;
}
bool Window::ResetTitleBarColors() {
  return false;
}
}  // namespace nativeapi

namespace nativeapi {
bool Window::SetCustomShadow(std::shared_ptr<WindowShadow> shadow) {
  return ohos::SetWindowCustomShadow(pimpl_->state_, std::move(shadow));
}
std::shared_ptr<WindowShadow> Window::GetCustomShadow() const {
  return ohos::WindowCustomShadow(pimpl_->state_);
}

// Optional desktop window capabilities.
bool Window::SetMaximizedVertically(bool) { return false; }
bool Window::IsMaximizedVertically() const { return false; }
bool Window::IsVerticalMaximizeSupported() { return false; }
bool Window::Dock(WindowDockSide, double) { return false; }
bool Window::Undock() { return false; }
WindowDockSide Window::GetDockSide() const { return WindowDockSide::None; }
bool Window::IsDockingSupported() { return false; }
bool Window::AnimateBounds(Rectangle) { return false; }
bool Window::IsBoundsAnimationSupported() { return false; }
bool Window::SetVisibleOnFullScreen(bool) { return false; }
bool Window::IsVisibleOnFullScreen() const { return false; }
bool Window::IsVisibleOnFullScreenSupported() { return false; }
bool Window::GrabKeyboard() { return false; }
bool Window::UngrabKeyboard() { return false; }
bool Window::IsKeyboardGrabbed() const { return false; }
bool Window::IsKeyboardGrabSupported() { return false; }

}  // namespace nativeapi
