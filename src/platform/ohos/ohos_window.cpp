#include "ohos_window.h"
#include "ohos_application.h"
#include "ohos_local_shortcuts.h"
#include "ohos_views.h"

#include <deviceinfo.h>
#include <hilog/log.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include "../../application.h"
#include "../../foundation/dispatcher.h"
#include "../../window_property_dispatch.h"
#include "../../window_registry.h"
#include "ohos_runtime.h"
#include "ohos_sdk_request.h"
#include "ohos_window_shadow.h"

namespace nativeapi::ohos {
namespace {
struct Windows {
  napi_env env = nullptr;
  napi_ref stage = nullptr, module = nullptr;
  std::vector<std::shared_ptr<WindowState>> items;
  std::function<void(const WindowEvent&)> sink;
  uint64_t order = 0;
  struct StackSnapshot {
    uint64_t completed_id = 0;
    std::vector<int32_t> ids;
  };
  uint64_t stack_request_id = 0;
  std::map<int32_t, int64_t> window_displays;
  std::map<int64_t, StackSnapshot> stacks;
  std::map<int64_t, std::shared_ptr<SdkRequest>> ui_stack_requests;
};
Windows& Host() {
  // Native cleanup may follow singleton teardown. No N-API calls in a C++ destructor.
  static auto* host = new Windows;
  return *host;
}
void Exception(napi_env env, const char* operation) {
  bool pending = false;
  napi_is_exception_pending(env, &pending);
  if (pending) {
    napi_value error;
    napi_get_and_clear_last_exception(env, &error);
  }
  OH_LOG_Print(LOG_APP, LOG_WARN, 0x0001, "nativeapi-window", "%{public}s failed", operation);
}
bool Call(napi_env env,
          napi_value object,
          const char* name,
          const std::vector<napi_value>& args,
          napi_value& result) {
  napi_value method;
  if (!object || napi_get_named_property(env, object, name, &method) != napi_ok ||
      napi_call_function(env, object, method, args.size(), args.data(), &result) != napi_ok) {
    Exception(env, name);
    return false;
  }
  return true;
}
struct PromiseCallback {
  Completion work;
  bool success;
};
napi_value PromiseDone(napi_env env, napi_callback_info info) {
  napi_value value = nullptr;
  size_t count = 1;
  void* data = nullptr;
  napi_get_cb_info(env, info, &count, &value, nullptr, &data);
  auto* callback = static_cast<PromiseCallback*>(data);
  try {
    if (callback && callback->work)
      callback->work(callback->success, value);
  } catch (...) {
    Exception(env, "window completion");
  }
  return nullptr;
}
napi_value Function(napi_env env, napi_callback callback, void* data, napi_finalize finalize) {
  napi_value value = nullptr;
  if (napi_create_function(env, "nativeapi.window", NAPI_AUTO_LENGTH, callback, data, &value) !=
      napi_ok)
    return nullptr;
  if (napi_add_finalizer(env, value, data, finalize, nullptr, nullptr) != napi_ok)
    return nullptr;
  return value;
}
bool Await(napi_env env, napi_value value, Completion completion) {
  bool promise = false;
  if (napi_is_promise(env, value, &promise) != napi_ok)
    return false;
  if (!promise) {
    if (completion)
      completion(true, value);
    return true;
  }
  std::vector<napi_value> args;
  for (bool success : {true, false}) {
    auto* callback = new PromiseCallback{completion, success};
    auto function = Function(env, PromiseDone, callback, [](napi_env, void* p, void*) {
      delete static_cast<PromiseCallback*>(p);
    });
    if (!function) {
      delete callback;
      return false;
    }
    args.push_back(function);
  }
  napi_value ignored;
  return Call(env, value, "then", args, ignored);
}
void Emit(const WindowEvent& event) {
  auto sink = Host().sink;  // A listener may replace the sink while handling this event.
  try {
    if (sink)
      sink(event);
  } catch (...) {
    // Never unwind a C++ listener exception through an ArkTS native callback.
  }
}
void RefreshWindowScale(const std::shared_ptr<WindowState>& state) {
  if (!state || !state->alive)
    return;
  const double scale = WindowScale(state);
  const double prior = state->observed_scale;
  state->observed_scale = scale;
  if (prior > 0 && std::abs(prior - scale) > 1e-6)
    Emit(WindowScaleFactorChangedEvent(state->id, scale));
}
void GeometryChanged(const std::shared_ptr<WindowState>& state) {
  if (!state->alive)
    return;
  RefreshWindowScale(state);
  if (!state->alive)
    return;
  const auto bounds = WindowBounds(state);
  const auto prior = state->observed_bounds;
  state->observed_bounds = bounds;
  if (prior.x != bounds.x || prior.y != bounds.y)
    Emit(WindowMovedEvent(state->id, {bounds.x, bounds.y}));
  if (!state->alive)
    return;
  if (prior.width != bounds.width || prior.height != bounds.height)
    Emit(WindowResizedEvent(state->id, {bounds.width, bounds.height}));
  RefreshWindowFullscreen(state);
  RefreshWindowCustomShadow(state);
}
struct EventCallback {
  std::weak_ptr<WindowState> state;
  std::string event;
};
struct NativeCloseReply {
  napi_env env = nullptr;
  napi_deferred deferred = nullptr;
  void Finish(bool prevent) {
    auto reply = std::exchange(deferred, nullptr);
    // Cleanup invalidates requests after detaching the environment. Never call
    // N-API during its teardown or resolve an old environment's reply in a new one.
    if (reply && env == Host().env)
      napi_resolve_deferred(env, reply, Boolean(env, prevent));
  }
};
napi_value NativeClose(napi_env env, const std::shared_ptr<WindowState>& state) {
  auto reply = std::make_shared<NativeCloseReply>();
  reply->env = env;
  napi_value promise = nullptr;
  if (napi_create_promise(env, &reply->deferred, &promise) != napi_ok)
    return nullptr;
  if (!state || !state->alive || state->destroying || state->close->IsPending()) {
    reply->Finish(true);  // One decision owns the native identity, including repeated clicks.
    return promise;
  }
  if (!state->close->HasObservers()) {
    reply->Finish(false);  // Preserve the host's normal close path without subscribers.
    return promise;
  }
  state->cancel_native_close = [reply] { reply->Finish(true); };
  auto complete = [weak = std::weak_ptr<WindowState>(state), reply](bool prevent) {
    if (auto current = weak.lock())
      current->cancel_native_close = {};
    reply->Finish(prevent);
  };
  // false continues this exact SDK close request. Do not destroy the window or
  // terminate its Ability ourselves: the host owns those operations and cleanup.
  if (!state->close->Request([complete] { complete(false); }, [complete] { complete(true); }))
    complete(true);
  return promise;
}
bool Watch(const std::shared_ptr<WindowState>& state, const char* event);
void WatchRenderedControls(const std::shared_ptr<WindowState>& state) {
  // Frame measurement requires loaded UIContent. The shown event retries the
  // initial attachment, which may precede stage.loadContent().
  for (const auto& entry : state->subscriptions)
    if (entry.event == "frameMetricsMeasured")
      return;
  napi_value value = nullptr;
  bool decorated = false;
  if (Call(state->env, WindowObject(state), "getWindowDecorVisible", {}, value) &&
      napi_get_value_bool(state->env, value, &decorated) == napi_ok &&
      Watch(state, "frameMetricsMeasured"))
    if (auto window = WindowRegistry::GetInstance().Get(state->id))
      detail::WindowPropertyDispatch::PrimeControlButtons(*window);
}
napi_value Observe(napi_env env, napi_callback_info info) {
  napi_value value = nullptr;
  size_t count = 1;
  void* data = nullptr;
  napi_get_cb_info(env, info, &count, &value, nullptr, &data);
  auto* callback = static_cast<EventCallback*>(data);
  auto state = callback ? callback->state.lock() : nullptr;
  if (callback && callback->event == "windowWillClose")
    return NativeClose(env, state);
  if (!count)
    return nullptr;
  if (!state || !state->alive)
    return nullptr;
  if (callback->event == "occlusionStateChanged" || callback->event == "windowVisibilityChange") {
    bool visible = false;
    if (callback->event == "occlusionStateChanged") {
      int32_t status = -1;
      if (napi_get_value_int32(env, value, &status) != napi_ok || status < 0 || status > 2)
        return nullptr;
      visible = status != 2;
    } else if (napi_get_value_bool(env, value, &visible) != napi_ok)
      return nullptr;
    state->observed_occlusion =
        visible ? WindowOcclusionState::Visible : WindowOcclusionState::Occluded;
    if (state->primary && visible)
      ObserveApplicationWindowVisibility(env, true);
    if (auto window = WindowRegistry::GetInstance().Get(state->id))
      detail::WindowPropertyDispatch::RefreshOcclusion(*window);
  } else if (callback->event == "windowEvent") {
    int32_t type = 0;
    napi_get_value_int32(env, value, &type);
    if (type == 7) {
      ForgetWindow(state);
      return nullptr;
    }
    if (type == 1 || type == 4) {
      const bool visible = type == 1;
      if (visible)
        WatchRenderedControls(state);
      if (state->observed_visible != visible) {
        state->observed_visible = visible;
        if (visible)
          state->order = ++Host().order;
      }
      RefreshWindowCustomShadow(state);
    } else if (type == 2 || type == 3) {
      const bool focused = type == 2;
      ResetLocalKeys(state);
      if (state->observed_focused != focused) {
        state->observed_focused = focused;
        if (focused) {
          state->order = ++Host().order;
          Emit(WindowFocusedEvent(state->id));
        } else
          Emit(WindowBlurredEvent(state->id));
      }
    }
  } else if (callback->event == "windowRectChange") {
    ObserveWindowCustomShadowFrame(state, value);
    GeometryChanged(state);
  } else if (callback->event == "windowSizeChange") {
    ObserveWindowCustomShadowSize(state, value);
    GeometryChanged(state);
  } else if (callback->event == "windowDensityChange") {
    RefreshWindowScale(state);
  } else if (callback->event == "windowTitleButtonRectChange") {
    WatchRenderedControls(state);
    if (auto window = WindowRegistry::GetInstance().Get(state->id))
      detail::WindowPropertyDispatch::RefreshControlButtons(*window);
  } else if (callback->event == "frameMetricsMeasured") {
    // Empty/hidden button rows need not produce a usable rectangle notification.
    // Read the actual area after layout, rather than predicting the requested value.
    if (auto window = WindowRegistry::GetInstance().Get(state->id))
      detail::WindowPropertyDispatch::RefreshControlButtons(*window);
  } else if (callback->event == "windowStatusChange") {
    int32_t status = 0;
    napi_get_value_int32(env, value, &status);
    const auto prior = state->observed_status;
    state->observed_status = status;
    if (prior != status) {
      if (status == 3)
        Emit(WindowMinimizedEvent(state->id));
      else if (status == 2)
        Emit(WindowMaximizedEvent(state->id));
      else if (prior == 2 || prior == 3)
        Emit(WindowRestoredEvent(state->id));
    }
    RefreshWindowFullscreen(state);
  }
  return nullptr;
}
bool Watch(const std::shared_ptr<WindowState>& state, const char* event) {
  for (const auto& entry : state->subscriptions)
    if (entry.event == event)
      return true;
  auto* callback = new EventCallback{state, event};
  auto function = Function(state->env, Observe, callback,
                           [](napi_env, void* p, void*) { delete static_cast<EventCallback*>(p); });
  if (!function) {
    delete callback;
    return false;
  }
  napi_value ignored;
  if (!Call(state->env, WindowObject(state), "on", {String(state->env, event), function}, ignored))
    return false;
  napi_ref reference;
  if (napi_create_reference(state->env, function, 1, &reference) != napi_ok) {
    Call(state->env, WindowObject(state), "off", {String(state->env, event), function}, ignored);
    return false;
  }
  state->subscriptions.push_back({event, reference});
  return true;
}
void Unwatch(const std::shared_ptr<WindowState>& state, bool all) {
  auto& subscriptions = state->subscriptions;
  for (auto it = subscriptions.begin(); it != subscriptions.end();) {
    // Keep an installed close interceptor until native destruction. Removing
    // observers must not let a repeated system click bypass an outstanding vote.
    // With no close observers and no pending request it transparently permits SDK closing.
    if (!all &&
        (it->event == "windowEvent" || it->event == "windowWillClose" ||
         it->event == "windowVisibilityChange" || it->event == "occlusionStateChanged" ||
         it->event == "windowTitleButtonRectChange" || it->event == "frameMetricsMeasured" ||
         ((it->event == "windowRectChange" || it->event == "windowSizeChange") && state->custom_shadow))) {
      ++it;
      continue;
    }
    napi_value callback, ignored;
    napi_get_reference_value(state->env, it->callback, &callback);
    // Destroyed SDK windows cannot accept off(), and a closing environment
    // cannot execute JavaScript. Both dispose their own native subscriptions;
    // release our roots without re-entering either invalid owner.
    if (state->alive && state->env == Host().env)
      Call(state->env, WindowObject(state), "off", {String(state->env, it->event), callback},
           ignored);
    napi_delete_reference(state->env, it->callback);
    it = subscriptions.erase(it);
  }
}
void Cleanup(void*) {
  const auto env = Host().env;
  Host().env = nullptr;
  Host().window_displays.clear();
  Host().stacks.clear();
  Host().ui_stack_requests.clear();
  auto items = Host().items;
  for (auto& state : items)
    ForgetWindow(state, false);
  if (Host().stage)
    napi_delete_reference(env, Host().stage);
  if (Host().module)
    napi_delete_reference(env, Host().module);
  Host().stage = Host().module = nullptr;
  // Preserve the guarded manager sink across a later UI environment attachment.
}
bool InitializeHost(napi_env env) {
  if (!IsInitialized() || !IsMainThread() || !env)
    return false;
  if (Host().env)
    return Host().env == env;
  if (napi_add_env_cleanup_hook(env, Cleanup, nullptr) != napi_ok)
    return false;
  Host().env = env;
  napi_value module;
  if (napi_load_module(env, "@ohos.window", &module) == napi_ok)
    napi_create_reference(env, module, 1, &Host().module);
  else
    Exception(env, "load window module");
  return true;
}
}  // namespace

napi_value Number(napi_env env, double value) {
  napi_value out;
  napi_create_double(env, value, &out);
  return out;
}
napi_value Boolean(napi_env env, bool value) {
  napi_value out;
  napi_get_boolean(env, value, &out);
  return out;
}
napi_value String(napi_env env, const std::string& value) {
  napi_value out;
  napi_create_string_utf8(env, value.data(), value.size(), &out);
  return out;
}
napi_value WindowObject(const std::shared_ptr<WindowState>& state) {
  napi_value object = nullptr;
  if (state && state->object)
    napi_get_reference_value(state->env, state->object, &object);
  return object;
}
napi_value WindowStageObject(const std::shared_ptr<WindowState>& state) {
  napi_value object = nullptr;
  if (!state || !state->alive || !state->primary || state->env != Host().env || !Host().stage)
    return nullptr;
  napi_value main = nullptr, properties = nullptr, id = nullptr;
  int32_t system_id = 0;
  if (napi_get_reference_value(state->env, Host().stage, &object) != napi_ok ||
      !Call(state->env, object, "getMainWindowSync", {}, main) ||
      !Call(state->env, main, "getWindowProperties", {}, properties) ||
      !js::Property(state->env, properties, "id", id) ||
      napi_get_value_int32(state->env, id, &system_id) != napi_ok || system_id != state->system_id)
    return nullptr;
  return object;
}
bool UseWindow(const std::shared_ptr<WindowState>& state, std::function<void()> work) {
  if (!state || !state->alive)
    return false;
  bool used = false;
  RunSync([&] {
    if (!state->alive || state->env != Host().env)
      return;
    napi_handle_scope scope;
    if (napi_open_handle_scope(state->env, &scope) != napi_ok)
      return;
    try {
      work();
      used = true;
    } catch (...) {
      Exception(state->env, "window operation");
    }
    napi_close_handle_scope(state->env, scope);
  });
  return used;
}
std::vector<std::shared_ptr<WindowState>> WindowStates() {
  std::vector<std::shared_ptr<WindowState>> result;
  RunSync([&] {
    result = Host().items;
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a->order > b->order; });
  });
  return result;
}
std::vector<std::shared_ptr<WindowState>> HitTestWindowStates() {
  const bool ui = IsMainThread();
  std::vector<std::pair<std::shared_ptr<SdkRequest>, bool>> requests;
  WithEnvironment([&](napi_env env) {
    if (Host().env != env || !Host().module || Host().items.empty()) return;
    napi_value module = nullptr;
    if (napi_get_reference_value(env, Host().module, &module) != napi_ok) return;
    std::set<int32_t> active_windows;
    std::set<int64_t> displays;
    for (const auto& state : Host().items) {
      if (!state->alive) continue;
      active_windows.insert(state->system_id);
      napi_value properties = nullptr, display = nullptr;
      int64_t id = -1;
      if (js::Call(env, WindowObject(state), "getWindowProperties", {}, properties) &&
          js::Property(env, properties, "displayId", display) &&
          napi_get_value_int64(env, display, &id) == napi_ok && id >= 0)
        Host().window_displays[state->system_id] = id;
      // Retain the known display across transient property-query failure.
      const auto known = Host().window_displays.find(state->system_id);
      if (known != Host().window_displays.end()) displays.insert(known->second);
    }
    for (auto it = Host().window_displays.begin(); it != Host().window_displays.end();)
      if (!active_windows.count(it->first)) it = Host().window_displays.erase(it);
      else ++it;
    for (auto it = Host().stacks.begin(); it != Host().stacks.end();)
      if (!displays.count(it->first)) it = Host().stacks.erase(it);
      else ++it;
    std::vector<std::shared_ptr<SdkRequest>> cancelled;
    for (auto it = Host().ui_stack_requests.begin(); it != Host().ui_stack_requests.end();)
      if (!displays.count(it->first)) {
        cancelled.push_back(it->second);
        it = Host().ui_stack_requests.erase(it);
      } else ++it;
    for (const auto& request : cancelled) request->Finish(false);
    for (const auto display_id : displays) {
      // Workers always start after their invocation. Reusing an older pending UI
      // query could otherwise return a stack from before an external SDK raise.
      const auto pending = Host().ui_stack_requests.find(display_id);
      if (ui && pending != Host().ui_stack_requests.end()) {
        requests.emplace_back(pending->second, true);
        continue;
      }
      auto request = std::make_shared<SdkRequest>();
      const auto generation = EnvironmentGeneration();
      const auto sequence = ++Host().stack_request_id;
      request->read_result = [env, generation, sequence, display_id](napi_env current, napi_value value) {
        bool array = false; uint32_t count = 0;
        if (current != env || Host().env != env || EnvironmentGeneration() != generation ||
            napi_is_array(env, value, &array) != napi_ok || !array ||
            napi_get_array_length(env, value, &count) != napi_ok) return false;
        if (std::none_of(Host().window_displays.begin(), Host().window_displays.end(),
              [display_id](const auto& entry) { return entry.second == display_id; })) return false;
        std::vector<int32_t> stack;
        for (uint32_t i = 0; i < count; ++i) {
          napi_value object = nullptr, properties = nullptr, id_value = nullptr;
          int32_t id = 0;
          if (napi_get_element(env, value, i, &object) != napi_ok ||
              !js::Call(env, object, "getWindowProperties", {}, properties) ||
              !js::Property(env, properties, "id", id_value) ||
              napi_get_value_int32(env, id_value, &id) != napi_ok || id <= 0 ||
              std::find(stack.begin(), stack.end(), id) != stack.end()) return false;
          stack.push_back(id);
        }
        auto& completed = Host().stacks[display_id];
        if (sequence >= completed.completed_id) {
          completed.ids = std::move(stack);
          completed.completed_id = sequence;
        }
        return true;
      };
      if (ui) {
        request->completed = [weak = std::weak_ptr<SdkRequest>(request), display_id](bool) {
          const auto it = Host().ui_stack_requests.find(display_id);
          if (it != Host().ui_stack_requests.end() && it->second == weak.lock())
            Host().ui_stack_requests.erase(it);
        };
      }
      napi_value display_arg = nullptr, all_arg = nullptr;
      if (napi_create_int64(env, display_id, &display_arg) != napi_ok ||
          napi_create_int32(env, 0, &all_arg) != napi_ok) continue;
      const std::vector<napi_value> args{display_arg, all_arg};
      if (ui) Host().ui_stack_requests[display_id] = request;
      const bool submitted = request->Start(env, "getWindowsByCoordinate", module, args);
      requests.emplace_back(request, submitted);
    }
  });
  for (const auto& entry : requests) entry.first->Result(entry.second);
  auto states = WindowStates();
  if (!RunSync([&] {
    struct Group {
      uint64_t order = 0;
      int64_t display = -1;
      std::vector<std::shared_ptr<WindowState>> states;
    };
    std::map<int64_t, Group> groups;
    for (const auto& state : states) {
      const auto known = Host().window_displays.find(state->system_id);
      const auto display = known == Host().window_displays.end() ? -1 : known->second;
      auto& group = groups[display];
      group.display = display;
      group.order = std::max(group.order, state->order);
      group.states.push_back(state);
    }
    std::vector<Group> ordered;
    for (auto& entry : groups) {
      auto& group = entry.second;
      const auto snapshot = Host().stacks.find(group.display);
      if (snapshot != Host().stacks.end()) {
        const auto& stack = snapshot->second.ids;
        group.states.erase(std::remove_if(group.states.begin(), group.states.end(), [&](const auto& state) {
          return std::find(stack.begin(), stack.end(), state->system_id) == stack.end();
        }), group.states.end());
        std::stable_sort(group.states.begin(), group.states.end(), [&](const auto& a, const auto& b) {
          return std::find(stack.begin(), stack.end(), a->system_id) <
                 std::find(stack.begin(), stack.end(), b->system_id);
        });
      }
      ordered.push_back(std::move(group));
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
      return a.order > b.order;
    });
    states.clear();
    for (auto& group : ordered) states.insert(states.end(), group.states.begin(), group.states.end());
  })) states.clear();
  return states;
}
std::shared_ptr<WindowState> FindWindowState(void* token) {
  for (auto& state : WindowStates())
    if (state.get() == token)
      return state;
  return nullptr;
}
bool ReadWindow(const std::shared_ptr<WindowState>& state,
                WindowManager_WindowProperties& properties) {
  bool ok = false;
  UseWindow(state,
            [&] { ok = OH_WindowManager_GetWindowProperties(state->system_id, &properties) == 0; });
  return ok;
}
double WindowScale(const std::shared_ptr<WindowState>& state) {
  double result = 1;
  UseWindow(state, [&] {
    napi_value density, value;
    if (Call(state->env, WindowObject(state), "getWindowDensityInfo", {}, density) &&
        napi_get_named_property(state->env, density, "systemDensity", &value) == napi_ok)
      napi_get_value_double(state->env, value, &result);
  });
  return std::isfinite(result) && result > 0 ? result : 1;
}
Rectangle WindowBounds(const std::shared_ptr<WindowState>& state, bool content) {
  WindowManager_WindowProperties properties{};
  if (!ReadWindow(state, properties))
    return {};
  const auto scale = WindowScale(state);
  const auto& rect = content ? properties.drawableRect : properties.windowRect;
  // drawableRect coordinates are relative to the window, not the screen.
  return {(rect.posX + (content ? properties.windowRect.posX : 0)) / scale,
          (rect.posY + (content ? properties.windowRect.posY : 0)) / scale, rect.width / scale,
          rect.height / scale};
}
bool CallWindow(const std::shared_ptr<WindowState>& state,
                const char* method,
                std::vector<napi_value> args,
                Completion completion) {
  bool ok = false;
  UseWindow(state, [&] {
    napi_value value;
    if (!Call(state->env, WindowObject(state), method, args, value)) {
      if (completion)
        completion(false, nullptr);
      return;
    }
    ok = Await(state->env, value,
               [weak = std::weak_ptr<WindowState>(state), completion,
                operation = std::string(method)](bool success, napi_value result) {
                 auto current = weak.lock();
                 if (!current || current->env != Host().env)
                   return;
                 if (!current->alive) {
                   if (completion)
                     completion(false, nullptr);
                   return;
                 }
                 if (!success)
                   Exception(current->env, operation.c_str());
                 if (completion)
                   completion(success, result);
               });
  });
  return ok;
}
bool WindowBoolean(const std::shared_ptr<WindowState>& state, const char* method) {
  bool result = false;
  UseWindow(state, [&] {
    napi_value value;
    if (Call(state->env, WindowObject(state), method, {}, value))
      napi_get_value_bool(state->env, value, &result);
  });
  return result;
}
int WindowStatus(const std::shared_ptr<WindowState>& state) {
  int32_t result = 0;
  UseWindow(state, [&] {
    napi_value value;
    if (Call(state->env, WindowObject(state), "getWindowStatus", {}, value))
      napi_get_value_int32(state->env, value, &result);
  });
  return result;
}
bool IsDesktopMainWindow(const std::shared_ptr<WindowState>& state) {
  if (!state || !state->primary)
    return false;
  const char* type = OH_GetDeviceType();
  return type && std::string(type) == "2in1";
}
bool IsFreeWindowMode(const std::shared_ptr<WindowState>& state) {
  bool supported = false;
  UseWindow(state, [&] {
    napi_value query = nullptr;
    napi_valuetype type = napi_undefined;
    if (!js::Property(state->env, WindowObject(state), "isInFreeWindowMode", query) ||
        napi_typeof(state->env, query, &type) != napi_ok)
      return;
    if (type == napi_function) {
      supported = WindowBoolean(state, "isInFreeWindowMode");
    } else if (type == napi_undefined) {
      const char* device_type = OH_GetDeviceType();
      supported = device_type && std::string(device_type) == "2in1";
    }
  });
  return supported;
}
WindowOcclusionState WindowOcclusion(const std::shared_ptr<WindowState>& state) {
  auto result = WindowOcclusionState::Unknown;
  UseWindow(state, [&] {
    if (ApplicationHidesWindow(state)) {
      result = WindowOcclusionState::Occluded;
      return;
    }
    napi_value value = nullptr;
    bool shown = false;
    if (!Call(state->env, WindowObject(state), "isWindowShowing", {}, value) ||
        napi_get_value_bool(state->env, value, &shown) != napi_ok)
      return;
    result = shown ? state->observed_occlusion : WindowOcclusionState::Occluded;
  });
  return result;
}
bool WindowFullscreen(const std::shared_ptr<WindowState>& state) {
  if (IsDesktopMainWindow(state) || IsFreeWindowMode(state))
    return WindowStatus(state) == 1;  // Freeform children also have a native fullscreen mode.
  WindowManager_WindowProperties properties{};
  return ReadWindow(state, properties) && properties.isFullScreen;
}
void RefreshWindowProperties(const std::shared_ptr<WindowState>& state) {
  if (auto window = WindowRegistry::GetInstance().Get(state->id))
    detail::WindowPropertyDispatch::Refresh(*window);
}
bool ObserveWindowShadowGeometry(const std::shared_ptr<WindowState>& state) {
  bool watched = false;
  UseWindow(state, [&] { watched = Watch(state, "windowRectChange") && Watch(state, "windowSizeChange"); });
  return watched;
}
void ReleaseWindowShadowGeometry(const std::shared_ptr<WindowState>& state) {
  if (!Host().sink) UseWindow(state, [&] { Unwatch(state, false); });
}
void ObserveWindowControlButtons(const std::shared_ptr<WindowState>& state) {
  UseWindow(state, [&] { WatchRenderedControls(state); });
}
void RefreshWindowFullscreen(const std::shared_ptr<WindowState>& state) {
  if (!state || !state->alive)
    return;
  const bool fullscreen = WindowFullscreen(state);
  if (state->observed_full_screen == fullscreen)
    return;
  state->observed_full_screen = fullscreen;
  if (fullscreen)
    Emit(WindowEnteredFullScreenEvent(state->id));
  else
    Emit(WindowExitedFullScreenEvent(state->id));
}
void SetWindowEventSink(std::function<void(const WindowEvent&)> sink) {
  RunSync([&] {
    Host().sink = std::move(sink);
    const auto items = Host().items;
    for (const auto& state : items)
      UseWindow(state, [&] {
        WatchRenderedControls(state);
        state->observed_scale = WindowScale(state);
        state->observed_bounds = WindowBounds(state);
        state->observed_status = WindowStatus(state);
        state->observed_full_screen = WindowFullscreen(state);
        if (Host().sink) {
          Watch(state, "windowWillClose");
          Watch(state, "windowRectChange");
          Watch(state, "windowStatusChange");
          Watch(state, "windowDensityChange");
        } else
          Unwatch(state, false);
      });
  });
}
void ForgetWindow(const std::shared_ptr<WindowState>& state, bool notify) {
  if (!state || !state->alive.exchange(false))
    return;
  ForgetWindowCustomShadow(state, notify);
  ResetLocalKeys(state, true);
  if (auto cancel = std::move(state->cancel_native_close))
    cancel();
  if (auto cancel = std::move(state->cancel_parent))
    cancel();
  if (auto request = std::move(state->title_action_request))
    request->Finish(false, false);
  if (auto request = std::move(state->layout_request))
    request->Finish(false, false);
  if (state->cancel_corner) {
    state->cancel_corner();
    state->cancel_corner = {};
  }
  if (state->cancel_shape) {
    state->cancel_shape();
    state->cancel_shape = {};
  }
  if (state->cancel_aspect) {
    state->cancel_aspect();
    state->cancel_aspect = {};
  }
  if (auto cancel = std::move(state->cancel_modes)) cancel();
  DetachViewContent(state);
  state->content_view.reset();
  state->close->Invalidate();
  // Close observations reach retained wrappers before removing the lookup entry.
  if (notify)
    Emit(WindowClosedEvent(state->id));
  Unwatch(state, true);
  napi_delete_reference(state->env, state->object);
  state->object = nullptr;
  WindowRegistry::GetInstance().Remove(state->id);
  auto& items = Host().items;
  items.erase(std::remove(items.begin(), items.end(), state), items.end());
}
bool DestroyWindow(const std::shared_ptr<WindowState>& state) {
  bool submitted = false;
  UseWindow(state, [&] {
    if (state->destroying) {
      submitted = true;
      return;
    }
    state->destroying = true;
    submitted = CallWindow(state, "destroyWindow", {}, [state](bool ok, napi_value) {
      state->destroying = false;
      if (ok)
        ForgetWindow(state);
    });
    if (!submitted)
      state->destroying = false;
  });
  return submitted;
}
std::shared_ptr<Window> AttachWindow(napi_env env,
                                     napi_value object,
                                     WindowId parent,
                                     bool primary) {
  if (!InitializeHost(env))
    return nullptr;
  napi_value properties, id_value;
  int32_t system_id;
  if (!Call(env, object, "getWindowProperties", {}, properties) ||
      napi_get_named_property(env, properties, "id", &id_value) != napi_ok ||
      napi_get_value_int32(env, id_value, &system_id) != napi_ok)
    return nullptr;
  for (auto& state : Host().items)
    if (state->system_id == system_id)
      return WindowRegistry::GetInstance().Get(state->id);
  auto state = std::make_shared<WindowState>();
  state->env = env;
  state->system_id = system_id;
  state->id = IdAllocator::Allocate<Window>();
  if (state->id == IdAllocator::kInvalidId)
    return nullptr;
  state->parent = parent;
  state->primary = primary;
  state->control_policy_known = IsDesktopMainWindow(state);
  state->order = ++Host().order;
  if (napi_create_reference(env, object, 1, &state->object) != napi_ok)
    return nullptr;
  auto weak = std::weak_ptr<WindowState>(state);
  state->close = detail::WindowCloseState::Create(
      state->id,
      [](auto work) {
        if (IsMainThread()) {
          work();
          return true;
        }
        return Post(std::move(work));
      },
      [weak] {
        auto s = weak.lock();
        return s && s->alive;
      });
  Host().items.push_back(state);
  auto window = std::make_shared<Window>(state.get());
  WindowRegistry::GetInstance().Add(state->id, window);
  state->observed_scale = WindowScale(state);
  state->observed_bounds = WindowBounds(state);
  state->observed_visible = WindowBoolean(state, "isWindowShowing");
  state->observed_focused = WindowBoolean(state, "isFocused");
  state->observed_status = WindowStatus(state);
  state->observed_full_screen = WindowFullscreen(state);
  // Keep actual visibility observations even without event listeners: shown
  // windows have no synchronous public occlusion getter. Never infer Visible
  // merely from isWindowShowing or geometry.
  detail::WindowPropertyDispatch::RefreshOcclusion(*window);
  if (!Watch(state, "occlusionStateChanged") || state->primary)
    Watch(state, "windowVisibilityChange");
  Watch(state, "windowEvent");  // Required even without observers: invalidate borrowed identities
                                // on destruction.
  Watch(state, "windowTitleButtonRectChange");  // Keep live control-property snapshots current.
  WatchRenderedControls(state);
  if (Host().sink) {
    Watch(state, "windowWillClose");
    Watch(state, "windowRectChange");
    Watch(state, "windowStatusChange");
    Watch(state, "windowDensityChange");
  }
  detail::WindowPropertyDispatch::Refresh(*window);
  Emit(WindowCreatedEvent(state->id));
  return window;
}
bool AttachWindowStage(napi_env env, napi_value stage) {
  if (!InitializeHost(env))
    return false;
  napi_value main;
  if (!Call(env, stage, "getMainWindowSync", {}, main) ||
      !AttachWindow(env, main, IdAllocator::kInvalidId, true))
    return false;
  napi_ref reference;
  if (napi_create_reference(env, stage, 1, &reference) != napi_ok)
    return false;
  if (Host().stage)
    napi_delete_reference(env, Host().stage);
  Host().stage = reference;
  return true;
}
bool CreateSubWindow(const std::string& name,
                     const std::string& page,
                     std::function<void(std::shared_ptr<Window>)> completion) {
  if (name.empty() || page.empty() || name.find('\0') != std::string::npos ||
      page.find('\0') != std::string::npos || !completion)
    return false;
  auto completed = std::make_shared<bool>(false);
  auto finish = [completion = std::move(completion), completed](std::shared_ptr<Window> window) {
    if (std::exchange(*completed, true))
      return;
    completion(std::move(window));
  };
  bool submitted = false;
  RunSync([&] {
    auto env = Host().env;
    if (!env || !Host().stage)
      return;
    napi_handle_scope scope;
    if (napi_open_handle_scope(env, &scope) != napi_ok)
      return;
    napi_value stage, promise;
    napi_get_reference_value(env, Host().stage, &stage);
    if (Call(env, stage, "createSubWindow", {String(env, name)}, promise)) {
      submitted = Await(env, promise, [env, page, finish](bool ok, napi_value child) {
        if (Host().env != env)
          return;  // Environment teardown cancels completion.
        if (!ok) {
          finish(nullptr);
          return;
        }
        WindowId parent = IdAllocator::kInvalidId;
        for (auto& state : Host().items)
          if (state->primary) {
            parent = state->id;
            break;
          }
        auto window = AttachWindow(env, child, parent);
        if (!window) {
          napi_value destroyed;
          if (Call(env, child, "destroyWindow", {}, destroyed))
            Await(env, destroyed, {});
          finish(nullptr);
          return;
        }
        auto state = FindWindowState(window->GetNativeObject());
        if (!state || state->destroying) {
          finish(nullptr);
          return;
        }
        const bool loading = CallWindow(state, "loadContent", {String(env, page)},
                                        [state, window, finish](bool ready, napi_value) {
                                          if (ready && state->alive && !state->destroying)
                                            finish(window);
                                          else {
                                            DestroyWindow(state);
                                            finish(nullptr);
                                          }
                                        });
        if (!loading) {
          DestroyWindow(state);
          finish(nullptr);
        }
      });
    }
    napi_close_handle_scope(env, scope);
  });
  return submitted;
}
void FocusWindow(const std::shared_ptr<WindowState>& state) {
  UseWindow(state, [&] {
    if (!Host().module)
      return;
    for (auto& source : Host().items) {
      if (source == state || !WindowBoolean(source, "isFocused"))
        continue;
      state->previous_focus = source->id;
      napi_value module, value;
      napi_get_reference_value(state->env, Host().module, &module);
      if (Call(state->env, module, "shiftAppWindowFocus",
               {Number(state->env, source->system_id), Number(state->env, state->system_id)},
               value))
        Await(state->env, value, {});
      break;
    }
  });
}
void ShowWindow(const std::shared_ptr<WindowState>& state, bool activate) {
  UseWindow(state, [&] {
    if (activate && ApplicationHidesWindow(state) && !Application::GetInstance().Show())
      return;
    for (auto& source : Host().items)
      if (source != state && WindowBoolean(source, "isFocused")) {
        state->previous_focus = source->id;
        break;
      }
    napi_value options;
    napi_create_object(state->env, &options);
    napi_set_named_property(state->env, options, "focusOnShow", Boolean(state->env, activate));
    CallWindow(state, "showWindow", {options}, [state, activate](bool ok, napi_value) {
      if (!ok)
        return;
      state->order = ++Host().order;
      if (activate)
        FocusWindow(state);
    });
  });
}
void BlurWindow(const std::shared_ptr<WindowState>& state) {
  UseWindow(state, [&] {
    if (!WindowBoolean(state, "isFocused"))
      return;
    std::shared_ptr<WindowState> target;
    for (auto& candidate : Host().items) {
      if (candidate == state || !WindowBoolean(candidate, "isWindowShowing"))
        continue;
      if (candidate->id == state->previous_focus) {
        target = candidate;
        break;
      }
      if (candidate->primary)
        target = candidate;
    }
    if (target)
      FocusWindow(target);
  });
}
}  // namespace nativeapi::ohos
