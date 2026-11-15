#pragma once

// Platform-internal ArkTS host seam. Never included by the generated bindings.
#include <napi/native_api.h>
#include <window_manager/oh_window.h>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "../../window.h"
#include "../../window_close_dispatch.h"

namespace nativeapi::ohos {
struct SdkRequest;
struct CustomWindowShadow;
// Call on the initialized ArkTS UI thread from onWindowStageCreate. The host
// retains ownership of its stage/main window. Repeated attachment is idempotent.
bool AttachWindowStage(napi_env env, napi_value stage);
std::shared_ptr<Window> AttachWindow(napi_env env,
                                     napi_value window,
                                     WindowId parent = IdAllocator::kInvalidId,
                                     bool primary = false);
// Creation/content loading are asynchronous on HarmonyOS. Completion runs on UI;
// nullptr means failure. Returns false if submission was impossible. Environment
// teardown cancels outstanding callbacks. The new child is initially hidden.
bool CreateSubWindow(const std::string& name,
                     const std::string& content_page,
                     std::function<void(std::shared_ptr<Window>)> completion);

// Implementation shared by the Window and WindowManager platform files. These
// tokens are borrowed opaque identities, not OHNativeWindow render surfaces.
struct WindowState {
  napi_env env = nullptr;
  napi_ref object = nullptr;
  int32_t system_id = 0;
  WindowId id = IdAllocator::kInvalidId;
  WindowId parent = IdAllocator::kInvalidId;
  WindowId previous_focus = IdAllocator::kInvalidId;
  bool primary = false;
  bool destroying = false;  // UI-thread state while the SDK's destroy promise is pending.
  std::atomic<bool> alive{true};
  uint64_t order = 0;
  std::shared_ptr<detail::WindowCloseState> close;
  std::function<void()> cancel_native_close;
  struct Subscription {
    std::string event;
    napi_ref callback;
  };
  std::vector<Subscription> subscriptions;
  Rectangle observed_bounds{};
  double observed_scale = 0;
  bool observed_visible = false, observed_focused = false;
  bool observed_full_screen = false;
  int observed_status = 0;
  WindowOcclusionState observed_occlusion = WindowOcclusionState::Unknown;
  std::shared_ptr<View> content_view;
  std::string title;
  Color background = Color::White;
  std::shared_ptr<CustomWindowShadow> custom_shadow;
  Rectangle shadow_event_frame{};
  uint64_t shadow_geometry_epoch = 0;
  std::optional<bool> shadow_captionless_without_query;
  std::shared_ptr<WindowShape> applied_shape;
  bool resizable = false, movable = false, non_activating = false;
  bool minimizable = true, maximizable = true, closable = true;
  bool control_policy_known = false;
  std::shared_ptr<SdkRequest> title_action_request;
  std::shared_ptr<SdkRequest> layout_request;
  bool control_buttons = true, shadow = false, always_on_top = false;
  bool fullscreen_restore_maximized = false;
  WindowCornerPreference corner_preference = WindowCornerPreference::Default;
  double original_corner_radius = -1;
  bool parent_pending = false;
  std::function<void()> cancel_parent;
  bool corner_pending = false;
  std::function<void()> cancel_corner;
  bool shaped = false, shape_pending = false;
  std::function<void()> cancel_shape;
  double aspect_ratio = 0;
  std::deque<std::pair<double, std::shared_ptr<SdkRequest>>> aspect_requests;
  std::function<void()> cancel_aspect;
  std::optional<uint32_t> supported_window_modes;
  std::deque<std::pair<bool, std::shared_ptr<SdkRequest>>> mode_requests;
  std::function<void()> cancel_modes;
};
std::shared_ptr<WindowState> FindWindowState(void* token);
std::vector<std::shared_ptr<WindowState>> WindowStates();
// Workers refresh the public SDK stack; UI callers submit without blocking and
// use the last completed stack (observed show/focus order before the first one).
std::vector<std::shared_ptr<WindowState>> HitTestWindowStates();
bool UseWindow(const std::shared_ptr<WindowState>& state, std::function<void()> work);
bool ReadWindow(const std::shared_ptr<WindowState>& state,
                WindowManager_WindowProperties& properties);
double WindowScale(const std::shared_ptr<WindowState>& state);
Rectangle WindowBounds(const std::shared_ptr<WindowState>& state, bool content = false);
napi_value WindowObject(const std::shared_ptr<WindowState>& state);
// Borrowed Stage object for its attached primary window, within the UI scope.
napi_value WindowStageObject(const std::shared_ptr<WindowState>& state);
napi_value Number(napi_env env, double value);
napi_value Boolean(napi_env env, bool value);
napi_value String(napi_env env, const std::string& value);
using Completion = std::function<void(bool, napi_value)>;
bool CallWindow(const std::shared_ptr<WindowState>& state,
                const char* method,
                std::vector<napi_value> args = {},
                Completion completion = {});
bool WindowBoolean(const std::shared_ptr<WindowState>& state, const char* method);
int WindowStatus(const std::shared_ptr<WindowState>& state);
bool IsDesktopMainWindow(const std::shared_ptr<WindowState>& state);
// Public live workspace query; only SDKs without it use the older 2in1 fallback.
bool IsFreeWindowMode(const std::shared_ptr<WindowState>& state);
bool WindowFullscreen(const std::shared_ptr<WindowState>& state);
WindowOcclusionState WindowOcclusion(const std::shared_ptr<WindowState>& state);
void ShowWindow(const std::shared_ptr<WindowState>& state, bool activate);
void FocusWindow(const std::shared_ptr<WindowState>& state);
void BlurWindow(const std::shared_ptr<WindowState>& state);
void ForgetWindow(const std::shared_ptr<WindowState>& state, bool notify = true);
bool DestroyWindow(const std::shared_ptr<WindowState>& state);
void SetWindowEventSink(std::function<void(const WindowEvent&)> sink);
void RefreshWindowProperties(const std::shared_ptr<WindowState>& state);
bool ObserveWindowShadowGeometry(const std::shared_ptr<WindowState>& state);
void ReleaseWindowShadowGeometry(const std::shared_ptr<WindowState>& state);
void ObserveWindowControlButtons(const std::shared_ptr<WindowState>& state);
void RefreshWindowFullscreen(const std::shared_ptr<WindowState>& state);
}  // namespace nativeapi::ohos
