#include <deviceinfo.h>
#include <hilog/log.h>
#include <multimedia/image_framework/image/pixelmap_native.h>
#include <limits>
#include "../../application.h"
#include "../../application_quit_dispatch.h"
#include "../../foundation/dispatcher.h"
#include "../../image.h"
#include "../../window_manager.h"
#include "ohos_application.h"
#include "ohos_napi.h"
#include "ohos_runtime.h"
#include "ohos_sdk_request.h"
#include "ohos_window.h"

namespace nativeapi {
namespace {
struct AppState {
  napi_env env = nullptr;
  napi_ref context = nullptr;
  bool alive = false, foreground = false, primary_override = false;
  bool terminating = false, sdk_hidden = false;
  int visibility_pending = -1;
  std::function<void(bool)> visibility_notify;
  uint64_t generation = 0;
  std::shared_ptr<EventRequest> quit;
  std::function<void(ohos::AbilityLifecycle)> notify;
  std::function<void()> cleanup;
};
std::weak_ptr<AppState> active;

void Cleanup(void* data) {
  std::unique_ptr<std::weak_ptr<AppState>> weak(static_cast<std::weak_ptr<AppState>*>(data));
  auto state = weak->lock();
  if (!state)
    return;
  if (state->context)
    napi_delete_reference(state->env, state->context);
  state->context = nullptr;
  state->env = nullptr;
  state->alive = state->foreground = state->terminating = state->sdk_hidden = false;
  state->visibility_pending = -1;
  ++state->generation;
  if (state->cleanup)
    state->cleanup();
  state->quit.reset();
}

bool Owner(napi_env env, const std::shared_ptr<AppState>& state, napi_value& context) {
  return state->alive && state->env == env && state->context &&
         napi_get_reference_value(env, state->context, &context) == napi_ok && context;
}

bool Visible(const std::shared_ptr<AppState>& state) {
  return state->alive && state->foreground && !state->sdk_hidden;
}
void Hidden(const std::shared_ptr<AppState>& state, bool hidden) {
  const bool before = Visible(state);
  state->sdk_hidden = hidden;
  const bool after = Visible(state);
  if (before != after && state->visibility_notify) {
    try {
      state->visibility_notify(after);
    } catch (...) {
    }
  }
}
using ohos::SdkRequest;
bool Visibility(const std::shared_ptr<AppState>& state, bool show) {
  auto request = std::make_shared<SdkRequest>();
  bool submitted = false;
  ohos::WithEnvironment([&](napi_env env) {
    napi_value context = nullptr, global = nullptr, supported = nullptr;
    bool cap = false;
    if (!Owner(env, state, context) || state->terminating || state->visibility_pending != -1 ||
        napi_get_global(env, &global) != napi_ok ||
        !ohos::js::Call(env, global, "canIUse",
                        {ohos::String(env, "SystemCapability.PCService.StatusBarManager")},
                        supported) ||
        napi_get_value_bool(env, supported, &cap) != napi_ok || !cap)
      return;
    const auto generation = state->generation;
    state->visibility_pending = show ? 1 : 0;
    request->completed = [weak = std::weak_ptr<AppState>(state), generation, show](bool ok) {
      auto current = weak.lock();
      if (!current || !current->alive || current->generation != generation)
        return;
      current->visibility_pending = -1;
      if (ok)
        Hidden(current, !show);
    };
    submitted = request->Start(env, show ? "showAbility" : "hideAbility", context);
  });
  return request->Result(submitted);
}
bool BadgeNumber(const std::string& label, int32_t& value) {
  value = 0;
  if (label == "99+") {
    value = 100;
    return true;
  }
  for (char c : label) {
    if (c < '0' || c > '9' || value > (std::numeric_limits<int32_t>::max() - (c - '0')) / 10)
      return false;
    value = value * 10 + (c - '0');
  }
  return true;
}

struct Termination {
  std::weak_ptr<AppState> state;
  uint64_t generation;
  static napi_value Complete(napi_env env, napi_callback_info info) {
    size_t count = 1;
    napi_value error = nullptr;
    void* data = nullptr;
    napi_get_cb_info(env, info, &count, &error, nullptr, &data);
    const auto& token = *static_cast<Termination*>(data);
    auto state = token.state.lock();
    if (!state || state->generation != token.generation)
      return nullptr;
    napi_valuetype type = napi_undefined;
    if (error)
      napi_typeof(env, error, &type);
    napi_value null_value = nullptr;
    bool null_error = false;
    napi_get_null(env, &null_value);
    if (error)
      napi_strict_equals(env, error, null_value, &null_error);
    int32_t code = -1;
    napi_value number = nullptr;
    if (type == napi_object && !null_error && ohos::js::Property(env, error, "code", number))
      napi_get_value_int32(env, number, &code);
    else if (type == napi_number)
      napi_get_value_int32(env, error, &code);
    const bool ok = !count || type == napi_undefined || null_error || code == 0;
    if (!ok) {
      state->terminating = false;
      detail::ApplicationQuitDispatch::ReleaseFromHost(state->quit);
      state->quit.reset();
    }
    // Success is not an exit event: wait for the real UIAbility.onDestroy.
    return nullptr;
  }
};
}  // namespace

class Application::Impl {
 public:
  std::shared_ptr<AppState> state = std::make_shared<AppState>();
};

Application::Application()
    : pimpl_(std::make_unique<Impl>()), initialized_(false), running_(false), exit_code_(0) {
  active = pimpl_->state;
  pimpl_->state->notify = CreateGuardedCallback<ohos::AbilityLifecycle>(
      std::function<void(ohos::AbilityLifecycle)>([this](ohos::AbilityLifecycle event) {
        switch (event) {
          case ohos::AbilityLifecycle::Create:
            initialized_ = running_ = true;
            exit_code_ = 0;
            primary_window_.reset();
            InvalidateQuitRequest();
            Emit<ApplicationStartedEvent>();
            break;
          case ohos::AbilityLifecycle::Foreground:
            Emit<ApplicationActivatedEvent>();
            break;
          case ohos::AbilityLifecycle::Background:
            Emit<ApplicationDeactivatedEvent>();
            break;
          case ohos::AbilityLifecycle::Destroy:
            running_ = false;
            primary_window_.reset();
            pimpl_->state->primary_override = true;
            if (!pimpl_->state->terminating) {
              exit_code_ = 0;
              detail::ApplicationQuitDispatch::NotifyRequired();
            } else
              InvalidateQuitRequest();
            Emit<ApplicationExitingEvent>(exit_code_);
            break;
        }
      }));
  pimpl_->state->visibility_notify =
      CreateGuardedCallback<bool>(std::function<void(bool)>([this](bool visible) {
        if (visible)
          Emit<ApplicationActivatedEvent>();
        else
          Emit<ApplicationDeactivatedEvent>();
      }));
  pimpl_->state->cleanup = CreateGuardedCallback<>(std::function<void()>([this] {
    initialized_ = running_ = false;
    InvalidateQuitRequest();
  }));
}
Application::~Application() {
  ShutdownEmitter();
  InvalidateQuitRequest();
}

int Application::Run() {
  // The embedding Ability owns the UI loop. Never claim that a nested loop ran.
  return -1;
}
int Application::Run(std::shared_ptr<Window>) {
  return -1;
}

void Application::Quit(int exit_code) try {
  auto state = pimpl_->state;
  ohos::RunSync([this, state, exit_code] {
    if (state->alive)
      RequestQuit(exit_code);
  });
} catch (...) {
  // A failed allocation or UI submission must not terminate from the caller.
}
void Application::PerformQuit(int) {
  auto state = pimpl_->state;
  ohos::WithEnvironment([&](napi_env env) {
    napi_value context = nullptr, callback = nullptr, ignored = nullptr;
    if (!Owner(env, state, context) || state->terminating)
      return;
    auto token = std::make_unique<Termination>(Termination{state, state->generation});
    if (napi_create_function(env, "nativeapi.terminate", NAPI_AUTO_LENGTH, Termination::Complete,
                             token.get(), &callback) != napi_ok ||
        napi_add_finalizer(
            env, callback, token.get(),
            [](napi_env, void* data, void*) { delete static_cast<Termination*>(data); }, nullptr,
            nullptr) != napi_ok)
      return;
    token.release();
    state->quit = detail::ApplicationQuitDispatch::CurrentRequest();
    detail::ApplicationQuitDispatch::HoldForHost(state->quit);
    state->terminating = true;
    if (!ohos::js::Call(env, context, "terminateSelf", {callback}, ignored)) {
      state->terminating = false;
      detail::ApplicationQuitDispatch::ReleaseFromHost(state->quit);
      state->quit.reset();
    }
  });
}

bool Application::IsRunning() const {
  bool result = false;
  ohos::WithEnvironment([&](napi_env) { result = pimpl_->state->alive; });
  return result;
}
bool Application::IsVisible() const {
  bool result = false;
  ohos::WithEnvironment([&](napi_env) { result = Visible(pimpl_->state); });
  return result;
}
bool Application::IsSingleInstance() const {
  bool result = false;
  ohos::WithContext([&](napi_env env, napi_value context) {
    napi_value info = nullptr, mode = nullptr, type = nullptr;
    if (!ohos::js::Property(env, context, "applicationInfo", info) ||
        !ohos::js::Property(env, info, "multiAppMode", mode) ||
        !ohos::js::Property(env, mode, "multiAppModeType", type))
      return;
    int32_t value = -1;
    // Public MultiAppModeType.UNSPECIFIED is 0: no multi-instance/clone mode.
    // A singleton Ability alone does not prove that the application is single-instance.
    result = napi_get_value_int32(env, type, &value) == napi_ok && value == 0;
  });
  return result;
}

bool Application::Show() {
  return Visibility(pimpl_->state, true);
}
bool Application::Hide() {
  return Visibility(pimpl_->state, false);
}
bool Application::SetIcon(const std::string& path) {
  if (!ohos::IsInitialized() || path.empty() || path.find('\0') != path.npos)
    return false;
  auto request = std::make_shared<SdkRequest>();
  bool submitted = false;
  ohos::WithContext([&](napi_env env, napi_value context) {
    // Public setMissionWindowIcon is desktop-only and validates a square image.
    const char* device = OH_GetDeviceType();
    if (!device || std::string(device) != "2in1")
      return;
    auto image = Image::FromFile(path);
    if (!image || image->GetSize().width != image->GetSize().height ||
        image->GetSize().width * image->GetSize().height > 128 * 1024 * 1024 / 4)
      return;
    auto* source = static_cast<OH_PixelmapNative*>(image->GetNativeObject());
    OH_PixelmapNative* copy = nullptr;
    napi_value pixels = nullptr;
    // OH_PixelmapNative_Clone() is API 22 and the pinned SDK may predate it. A 1:1
    // scaled pixelmap is a copy of the source (PixelMap::Create + scale(1, 1)), so
    // the conversion below still gets an independent image to hand to the host.
    if (!source ||
        OH_PixelmapNative_CreateScaledPixelMap(source, &copy, 1.0f, 1.0f) != IMAGE_SUCCESS ||
        !copy)
      return;
    auto converted = OH_PixelmapNative_ConvertPixelmapNativeToNapi(env, copy, &pixels);
    OH_PixelmapNative_Destroy(&copy);
    if (converted != IMAGE_SUCCESS) {
      ohos::js::ClearException(env);
      return;
    }
    request->env = env;
    if (!request->Keep(pixels))
      return;
    submitted = request->Start(env, "setMissionWindowIcon", context, {pixels});
  });
  return request->Result(submitted);
}
bool Application::SetDockIconVisible(bool) {
  return false;
}
bool Application::SetProgressBar(double) {
  return false;
}
bool Application::SetBadgeLabel(const std::string& label) {
  int32_t number = 0;
  if (!BadgeNumber(label, number))
    return false;
  auto request = std::make_shared<SdkRequest>();
  bool submitted = false;
  ohos::WithEnvironment([&](napi_env env) {
    napi_value module = nullptr;
    if (ohos::js::Module(env, "@ohos.notificationManager", module))
      submitted = request->Start(env, "setBadgeNumber", module, {ohos::Number(env, number)});
  });
  return request->Result(submitted);
}
bool Application::SetMenuBar(std::shared_ptr<Menu>) {
  return false;
}

bool Application::SetBrightness(Brightness brightness) {
  const char* key = nullptr;
  switch (brightness) {
    case Brightness::System:
      key = "COLOR_MODE_NOT_SET";
      break;
    case Brightness::Light:
      key = "COLOR_MODE_LIGHT";
      break;
    case Brightness::Dark:
      key = "COLOR_MODE_DARK";
      break;
    default:
      return false;
  }
  bool result = false;
  ohos::WithContext([&](napi_env env, napi_value context) {
    napi_value app_context = nullptr, module = nullptr, colors = nullptr;
    napi_value mode = nullptr, ignored = nullptr;
    if (ohos::js::Call(env, context, "getApplicationContext", {}, app_context) &&
        ohos::js::Module(env, "@ohos.app.ability.ConfigurationConstant", module) &&
        ohos::js::Property(env, module, "ColorMode", colors) &&
        ohos::js::Property(env, colors, key, mode))
      result = ohos::js::Call(env, app_context, "setColorMode", {mode}, ignored);
  });
  return result;
}
std::shared_ptr<Window> Application::GetPrimaryWindow() const {
  std::shared_ptr<Window> result;
  ohos::WithEnvironment([&](napi_env) {
    if (pimpl_->state->primary_override) {
      if (primary_window_ && primary_window_->GetNativeObject())
        result = primary_window_;
      return;
    }
    for (auto& state : ohos::WindowStates())
      if (state->primary && state->alive) {
        result = WindowManager::GetInstance().Get(state->id);
        break;
      }
  });
  return result;
}
void Application::SetPrimaryWindow(std::shared_ptr<Window> window) {
  ohos::RunSync([this, window = std::move(window)] {
    if (window && !window->GetNativeObject())
      return;
    primary_window_ = window;
    pimpl_->state->primary_override = true;
  });
}
std::vector<std::shared_ptr<Window>> Application::GetAllWindows() const {
  std::vector<std::shared_ptr<Window>> result;
  ohos::WithEnvironment([&](napi_env) { result = WindowManager::GetInstance().GetAll(); });
  return result;
}
void Application::StartEventListening() {}
void Application::StopEventListening() {}

namespace ohos {
bool ApplicationHidesWindow(const std::shared_ptr<WindowState>& window) {
  bool hidden = false;
  RunSync([&] {
    auto state = active.lock();
    if (!window || !window->alive || !state || !state->alive || !state->sdk_hidden ||
        state->env != window->env)
      return;
    auto current = window;
    const auto all = WindowStates();
    for (size_t remaining = all.size() + 1; current && remaining; --remaining) {
      if (current->primary) {
        hidden = true;
        break;
      }
      std::shared_ptr<WindowState> parent;
      for (auto& candidate : all)
        if (candidate->id == current->parent) {
          parent = candidate;
          break;
        }
      current = std::move(parent);
    }
  });
  return hidden;
}
void ObserveApplicationWindowVisibility(napi_env env, bool visible) {
  auto state = active.lock();
  if (!state || !state->alive || state->env != env)
    return;
  if (visible && state->sdk_hidden)
    Hidden(state, false);
}
bool HandleApplicationLifecycle(napi_env env, napi_value context, AbilityLifecycle event) {
  if (!env || !context || !IsInitialized() || !IsMainThread())
    return false;
  Application::GetInstance();
  auto state = active.lock();
  if (!state || (state->env && state->env != env))
    return false;
  napi_value owner = nullptr;
  bool same = false;
  if (state->context && napi_get_reference_value(env, state->context, &owner) == napi_ok)
    napi_strict_equals(env, owner, context, &same);
  if (event == AbilityLifecycle::Create) {
    if (state->alive)
      return same;
    bool attached = false;
    WithContext([&](napi_env attached_env, napi_value attached_context) {
      if (attached_env == env)
        napi_strict_equals(env, attached_context, context, &attached);
    });
    if (!attached)
      return false;
    if (!state->env) {
      auto weak = std::make_unique<std::weak_ptr<AppState>>(state);
      if (napi_add_env_cleanup_hook(env, Cleanup, weak.get()) != napi_ok)
        return false;
      weak.release();
      state->env = env;
    }
    if (napi_create_reference(env, context, 1, &state->context) != napi_ok)
      return false;
    ++state->generation;
    state->alive = true;
    state->foreground = state->terminating = state->primary_override = state->sdk_hidden = false;
    state->visibility_pending = -1;
    try {
      state->notify(event);
    } catch (...) {
    }
    return true;
  }
  if (!same || !state->alive)
    return false;
  const bool visible_before = Visible(state);
  switch (event) {
    case AbilityLifecycle::Foreground:
      if (state->foreground)
        return true;
      state->foreground = true;
      break;
    case AbilityLifecycle::Background:
      if (!state->foreground)
        return true;
      state->foreground = false;
      break;
    case AbilityLifecycle::Destroy:
      state->alive = state->foreground = false;
      ++state->generation;
      break;
    default:
      return false;
  }
  if (event == AbilityLifecycle::Destroy) {
    const auto reference = state->context;
    const auto generation = state->generation;
    state->context = nullptr;
    DetachContext(env, context);
    try {
      state->notify(event);
    } catch (...) {
    }
    napi_delete_reference(env, reference);
    if (state->generation == generation) {
      state->terminating = false;
      state->quit.reset();
    }
  } else if (visible_before != Visible(state)) {
    try {
      state->notify(event);
    } catch (...) {
    }
  }
  return true;
}
}  // namespace ohos
}  // namespace nativeapi
