#include <arkui/drawable_descriptor.h>
#include <arkui/native_node_napi.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include "../../foundation/dispatcher_common.h"
#include "../../image.h"
#include "../../view_impl.h"
#include "../../window_manager.h"
#include "ohos_arkui.h"
#include "ohos_napi.h"
#include "ohos_runtime.h"
#include "ohos_views.h"

namespace nativeapi {

// Shared layout batching; this backend schedules painting itself (or is a stub).
void View::Impl::BeginNativeUpdates() {}
void View::Impl::EndNativeUpdates(bool) {}
namespace {
using Impl = ViewInternal::Impl;
using ohos::NodeApi;
using ohos::NodeFloat;
using ohos::NodeInt;
using ohos::NodeText;
using ohos::NodeTextValue;
bool Main() {
  return dispatcher_internal::IsMainThreadByCapturedId() && ohos::IsInitialized();
}
ArkUI_NodeHandle Node(const Impl& impl) {
  return static_cast<ArkUI_NodeHandle>(impl.native);
}
std::map<uintptr_t, Impl*> callbacks;
std::set<napi_env> environments;
uintptr_t next_token = 0;
int teardown_depth = 0;
void Cleanup(void* data);
ArkUI_NodeHandle Create(ArkUI_NodeType type) {
  ArkUI_NodeHandle node = nullptr;
  if (Main())
    ohos::WithUiContext(nullptr, [&] {
      if (NodeApi())
        node = NodeApi()->createNode(type);
    });
  return node;
}
int Int(ArkUI_NodeHandle node, ArkUI_NodeAttributeType attr, int fallback = 0) {
  if (!Main() || !node || !NodeApi())
    return fallback;
  const auto* item = NodeApi()->getAttribute(node, attr);
  return item && item->value && item->size ? item->value[0].i32 : fallback;
}
float Float(ArkUI_NodeHandle node, ArkUI_NodeAttributeType attr) {
  if (!Main() || !node || !NodeApi())
    return 0;
  const auto* item = NodeApi()->getAttribute(node, attr);
  return item && item->value && item->size ? item->value[0].f32 : 0;
}
void ColorAttribute(ArkUI_NodeHandle node, ArkUI_NodeAttributeType attr, Color color) {
  if (!Main() || !node || !NodeApi())
    return;
  if (!color.a) {
    NodeApi()->resetAttribute(node, attr);
    return;
  }
  ArkUI_NumberValue value{};
  value.u32 = color.ToARGB();
  ArkUI_AttributeItem item{&value, 1, nullptr, nullptr};
  NodeApi()->setAttribute(node, attr, &item);
}
}  // namespace
struct View::Impl::Platform {
  napi_env env = nullptr;
  uintptr_t token = 0;
  ArkUI_NodeType type = ARKUI_NODE_STACK;
  bool listening = false, resize = false, editable = true, secure = false, multiline = false;
  std::string last_text;
  std::optional<std::string> placeholder;
  ArkUI_NodeAttributeType TextAttribute() const {
    return multiline ? NODE_TEXT_AREA_TEXT : NODE_TEXT_INPUT_TEXT;
  }
  ArkUI_NodeAttributeType PlaceholderAttribute() const {
    return multiline ? NODE_TEXT_AREA_PLACEHOLDER : NODE_TEXT_INPUT_PLACEHOLDER;
  }
  Rectangle applied_frame{};
  Color text_color = Color::Transparent;
  double font_size = 0;
  TextAlignment text_alignment = TextAlignment::Start;
  std::shared_ptr<Image> image;
  ArkUI_DrawableDescriptor* drawable = nullptr;
  ArkUI_NodeContentHandle content = nullptr;
  napi_ref content_ref = nullptr;
  napi_ref tooltip_host = nullptr;  // Mounted root only.
  bool tooltip_hovered = false;
  ArkUI_NodeHandle receiver_node = nullptr;
  std::weak_ptr<ohos::WindowState> tooltip_window;
  static bool TooltipEligible(Impl& impl) {
    if (!Main() || !impl.native || !impl.platform->tooltip_hovered || !impl.tooltip || impl.tooltip->empty())
      return false;
    auto window = impl.owner->GetWindow();
    auto state = window ? ohos::FindWindowState(window->GetNativeObject()) : nullptr;
    if (!state || !state->alive || state->destroying || !window->IsVisible() || !state->content_view)
      return false;
    auto& root = ViewInternal::Of(*state->content_view);
    if (!root.platform->content || !root.platform->tooltip_host)
      return false;
    for (auto view = impl.owner; view;) {
      if (!view->IsVisible() || !view->IsEnabled()) return false;
      auto parent = view->GetParent();
      view = parent.get();
    }
    return true;
  }
  void HideTooltip(bool reset_hover = false) try {
    if (reset_hover) tooltip_hovered = false;
    auto state = tooltip_window.lock();
    tooltip_window.reset();
    if (!state || !state->alive || !state->content_view || state->env != env) return;
    auto& root = ViewInternal::Of(*state->content_view);
    if (root.platform->tooltip_host) {
      napi_value host = nullptr, ignored = nullptr;
      if (napi_get_reference_value(env, root.platform->tooltip_host, &host) == napi_ok)
        ohos::js::Call(env, host, "hide", {ohos::Number(env, token)}, ignored);
    }
  } catch (...) {
    tooltip_hovered = false;
    tooltip_window.reset();
  }
  static void HideTooltips(Impl& impl, bool notify = true) {
    if (notify) impl.platform->HideTooltip(true);
    else { impl.platform->tooltip_hovered = false; impl.platform->tooltip_window.reset(); }
    for (const auto& child : impl.subviews) HideTooltips(ViewInternal::Of(*child), notify);
  }
  void ShowTooltip(Impl& impl) {
    HideTooltip();
    if (!TooltipEligible(impl)) return;
    auto window = impl.owner->GetWindow();
    auto state = ohos::FindWindowState(window->GetNativeObject());
    auto& root = ViewInternal::Of(*state->content_view);
    const auto unique_id = ohos::NodeUniqueId(Node(impl));
    napi_value host = nullptr, validate = nullptr, ignored = nullptr;
    if (unique_id < 0 ||
        napi_get_reference_value(env, root.platform->tooltip_host, &host) != napi_ok ||
        napi_create_function(env, "nativeapi.tooltip.valid", NAPI_AUTO_LENGTH,
          [](napi_env env, napi_callback_info info) -> napi_value {
            void* data = nullptr;
            napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data);
            const auto found = callbacks.find(reinterpret_cast<uintptr_t>(data));
            bool valid = false;
            try { valid = found != callbacks.end() && TooltipEligible(*found->second); } catch (...) {}
            napi_value result = nullptr; napi_get_boolean(env, valid, &result); return result;
          }, reinterpret_cast<void*>(token), &validate) != napi_ok)
      return;
    tooltip_window = state;
    ohos::js::Call(env, host, "show", {ohos::Number(env, token), ohos::Number(env, unique_id),
      ohos::String(env, *impl.tooltip), validate}, ignored);
  }
  static void Event(ArkUI_NodeEvent* event) {
    if (teardown_depth)
      return;
    // Per-node receivers also see events registered by other modules on this
    // node. User-data tokens and target IDs are only local to their registrant.
    const auto type = OH_ArkUI_NodeEvent_GetEventType(event);
    if (type != NODE_ON_FOCUS && type != NODE_ON_BLUR && type != NODE_ON_CLICK &&
        type != NODE_ON_SIZE_CHANGE && type != NODE_ON_HOVER && type != NODE_TEXT_AREA_ON_CHANGE &&
        type != NODE_TEXT_INPUT_ON_CHANGE && type != NODE_TEXT_AREA_ON_SUBMIT &&
        type != NODE_TEXT_INPUT_ON_SUBMIT && type != NODE_TEXT_AREA_ON_WILL_INSERT &&
        type != NODE_TEXT_INPUT_ON_WILL_INSERT && type != NODE_TEXT_AREA_ON_WILL_DELETE &&
        type != NODE_TEXT_INPUT_ON_WILL_DELETE)
      return;
    auto found = callbacks.find(reinterpret_cast<uintptr_t>(OH_ArkUI_NodeEvent_GetUserData(event)));
    if (found == callbacks.end())
      return;
    auto* impl = found->second;
    // A queued event from a replaced TextInput must not mutate the new node's
    // hover or text state even though the View and its callback token survive.
    if (OH_ArkUI_NodeEvent_GetNodeHandle(event) != Node(*impl))
      return;
    // Listeners may drop the last external reference during synchronous emission.
    auto keep = impl->owner->weak_from_this().lock();
    const int target = OH_ArkUI_NodeEvent_GetTargetId(event);
    if (type == NODE_ON_HOVER && target == 9) {
      const auto* data = OH_ArkUI_NodeEvent_GetNodeComponentEvent(event);
      impl->platform->tooltip_hovered = data && data->data[0].i32 != 0;
      try {
        if (impl->platform->tooltip_hovered) impl->platform->ShowTooltip(*impl);
        else impl->platform->HideTooltip();
      } catch (...) { impl->platform->tooltip_hovered = false; }
      return;
    }
    if (target == 7 || target == 8) {
      ArkUI_NumberValue value{};
      value.i32 = impl->platform->editable;
      OH_ArkUI_NodeEvent_SetReturnNumberValue(event, &value, 1);
      return;
    }
    if (target == 4) {
      auto& p = *impl->platform;
      auto text = NodeTextValue(Node(*impl), p.TextAttribute());
      if (text == p.last_text)
        return;
      p.last_text = text;
      if (p.listening)
        impl->Emit(TextFieldChangedEvent(impl->id, text));
      return;
    }
    if (target == 6) {
      if (impl->platform->resize)
        impl->OnNativeResized();
      return;
    }
    if (!impl->platform->listening)
      return;
    switch (target) {
      case 1:
        impl->Emit(ViewFocusedEvent(impl->id));
        break;
      case 2:
        impl->Emit(ViewBlurredEvent(impl->id));
        break;
      case 3:
        impl->Emit(ButtonClickedEvent(impl->id));
        break;
      case 5:
        if (!impl->platform->multiline)
          impl->Emit(TextFieldSubmittedEvent(impl->id));
        break;
    }
  }
  void HookTooltip(Impl& impl) {
    if (!impl.native || receiver_node == Node(impl)) return;
    receiver_node = Node(impl);
    NodeApi()->addNodeEventReceiver(Node(impl), Event);
    NodeApi()->registerNodeEvent(Node(impl), NODE_ON_HOVER, 9, reinterpret_cast<void*>(token));
  }
  void Hook(Impl& impl) {
    auto node = Node(impl);
    if (!node)
      return;
    HookTooltip(impl);
    NodeApi()->registerNodeEvent(node, NODE_ON_FOCUS, 1, reinterpret_cast<void*>(token));
    NodeApi()->registerNodeEvent(node, NODE_ON_BLUR, 2, reinterpret_cast<void*>(token));
    if (type == ARKUI_NODE_BUTTON)
      NodeApi()->registerNodeEvent(node, NODE_ON_CLICK, 3, reinterpret_cast<void*>(token));
  }
  void HookText(Impl& impl) {
    auto node = Node(impl);
    if (!node)
      return;
    const auto data = reinterpret_cast<void*>(token);
    NodeApi()->registerNodeEvent(
        node, multiline ? NODE_TEXT_AREA_ON_CHANGE : NODE_TEXT_INPUT_ON_CHANGE, 4, data);
    NodeApi()->registerNodeEvent(
        node, multiline ? NODE_TEXT_AREA_ON_SUBMIT : NODE_TEXT_INPUT_ON_SUBMIT, 5, data);
    NodeApi()->registerNodeEvent(
        node, multiline ? NODE_TEXT_AREA_ON_WILL_INSERT : NODE_TEXT_INPUT_ON_WILL_INSERT, 7, data);
    NodeApi()->registerNodeEvent(
        node, multiline ? NODE_TEXT_AREA_ON_WILL_DELETE : NODE_TEXT_INPUT_ON_WILL_DELETE, 8, data);
  }
  void DisposeTooltipHost(bool notify = true) {
    if (tooltip_host && env) {
      napi_value host = nullptr, ignored = nullptr;
      if (notify && napi_get_reference_value(env, tooltip_host, &host) == napi_ok)
        ohos::js::Call(env, host, "dispose", {}, ignored);
      napi_delete_reference(env, tooltip_host);
      tooltip_host = nullptr;
    }
  }
  void Unmount(Impl& impl, bool notify = true) {
    HideTooltips(impl, notify);
    DisposeTooltipHost(notify);
    if (content && impl.native)
      OH_ArkUI_NodeContent_RemoveNode(content, Node(impl));
    content = nullptr;
    if (content_ref && env)
      napi_delete_reference(env, content_ref);
    content_ref = nullptr;
  }
  void Dispose(Impl& impl) {
    HideTooltip(true);
    ++teardown_depth;
    Unmount(impl);
    if (impl.native && NodeApi()) {
      NodeApi()->removeNodeEventReceiver(Node(impl), Event);
      for (auto& child : impl.subviews) {
        auto& c = ViewInternal::Of(*child);
        if (c.native)
          NodeApi()->removeChild(Node(impl), Node(c));
      }
      if (impl.owned)
        NodeApi()->disposeNode(Node(impl));
    }
    impl.native = nullptr;
    if (drawable)
      OH_ArkUI_DrawableDescriptor_Dispose(drawable);
    drawable = nullptr;
    image.reset();
    env = nullptr;
    --teardown_depth;
  }
};
namespace {
void Cleanup(void* data) {
  auto env = static_cast<napi_env>(data);
  // Unmount all slots before disposing nodes, independently of tree order.
  for (auto& entry : callbacks)
    if (entry.second->platform->env == env)
      entry.second->platform->Unmount(*entry.second, false);
  for (auto& entry : callbacks)
    if (entry.second->platform->env == env)
      entry.second->platform->Dispose(*entry.second);
  environments.erase(env);
}
void Control(Impl& impl, ArkUI_NodeType type) {
  impl.platform->type = type;
  impl.platform->Hook(impl);
}
}  // namespace
View::Impl::Impl(View* owner, void* value, bool owned)
    : owner(owner),
      native(value ? value : CreateNativeContainer()),
      owned(owned),
      id(IdAllocator::Allocate<View>()),
      platform(std::make_unique<Platform>()) {
  if (!Main() || !native) {
    native = nullptr;
    return;
  }
  ohos::WithEnvironment([&](napi_env env) {
    if (!environments.count(env)) {
      if (napi_add_env_cleanup_hook(env, Cleanup, env) != napi_ok)
        return;
      environments.insert(env);
    }
    platform->env = env;
  });
  if (!platform->env) {
    if (owned)
      NodeApi()->disposeNode(Node(*this));
    native = nullptr;
    return;
  }
  platform->token = ++next_token;
  callbacks.emplace(platform->token, this);
  platform->type = static_cast<ArkUI_NodeType>(OH_ArkUI_NodeUtils_GetNodeType(Node(*this)));
  if (!value)
    NodeInt(Node(*this), NODE_STACK_ALIGN_CONTENT, ARKUI_ALIGNMENT_TOP_START);
  NodeText(Node(*this), NODE_ID, "nativeapi-view-" + std::to_string(id));
}
View::Impl::~Impl() {
  callbacks.erase(platform->token);
  if (Main())
    platform->Dispose(*this);
}
void* View::Impl::CreateNativeContainer() {
  return Create(ARKUI_NODE_STACK);
}
void View::Impl::SetNativeFrame(Rectangle r) {
  if (!Main() || !native || !std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) ||
      !std::isfinite(r.height) || r.width < 0 || r.height < 0)
    return;
  ArkUI_NumberValue values[2]{};
  values[0].f32 = r.x;
  values[1].f32 = r.y;
  ArkUI_AttributeItem item{values, 2, nullptr, nullptr};
  if (NodeApi()->setAttribute(Node(*this), NODE_POSITION, &item) == 0) {
    platform->applied_frame.x = r.x;
    platform->applied_frame.y = r.y;
  }
  NodeFloat(Node(*this), NODE_WIDTH, r.width);
  NodeFloat(Node(*this), NODE_HEIGHT, r.height);
}
Rectangle View::Impl::GetNativeFrame() const {
  if (!Main() || !native)
    return {};
  Rectangle result{platform->applied_frame.x, platform->applied_frame.y,
                   Float(Node(*this), NODE_WIDTH), Float(Node(*this), NODE_HEIGHT)};
  if (is_root) {
    ArkUI_IntSize size{};
    if (OH_ArkUI_NodeUtils_GetLayoutSize(Node(*this), &size) == 0) {
      double scale = 1;
      if (auto w = window.lock())
        scale = ohos::WindowScale(ohos::FindWindowState(w->GetNativeObject()));
      result.width = size.width / scale;
      result.height = size.height / scale;
    }
  } else if (!parent.expired()) {
    ArkUI_IntOffset offset{};
    if (OH_ArkUI_NodeUtils_GetLayoutPosition(Node(*this), &offset) == 0) {
      double scale = 1;
      if (auto w = owner->GetWindow())
        scale = ohos::WindowScale(ohos::FindWindowState(w->GetNativeObject()));
      result.x = offset.x / scale;
      result.y = offset.y / scale;
    }
  }
  return result;
}
Size View::Impl::GetNativeIntrinsicSize() const {
  if (!Main() || !native)
    return {};
  if (platform->type == ARKUI_NODE_IMAGE)
    return platform->image ? platform->image->GetSize() : Size{};
  if (platform->type == ARKUI_NODE_STACK)
    return {};
  Size result{};
  auto host = owner->GetWindow();
  if (!host)
    host = WindowManager::GetInstance().GetCurrent();
  auto state = host ? ohos::FindWindowState(host->GetNativeObject()) : nullptr;
  ohos::UseWindow(state, [&] {
    napi_value context, measure, options, measured, value;
    const auto env = state->env;
    if (!ohos::js::Call(env, ohos::WindowObject(state), "getUIContext", {}, context) ||
        !ohos::js::Call(env, context, "getMeasureUtils", {}, measure))
      return;
    napi_create_object(env, &options);
    napi_set_named_property(
        env, options, "textContent",
        ohos::String(
            env, NodeTextValue(Node(*this), platform->type == ARKUI_NODE_BUTTON ? NODE_BUTTON_LABEL
                                            : platform->type == ARKUI_NODE_TEXT
                                                ? NODE_TEXT_CONTENT
                                                : platform->TextAttribute())));
    napi_set_named_property(env, options, "fontSize",
                            ohos::Number(env, platform->font_size > 0 ? platform->font_size : 16));
    if (!ohos::js::Call(env, measure, "measureTextSize", {options}, measured))
      return;
    if (ohos::js::Property(env, measured, "width", value))
      napi_get_value_double(env, value, &result.width);
    if (ohos::js::Property(env, measured, "height", value))
      napi_get_value_double(env, value, &result.height);
    const auto scale = ohos::WindowScale(state);
    result.width /= scale;
    result.height /= scale;
  });
  if (platform->type == ARKUI_NODE_BUTTON) {
    result.width += 32;
    result.height = std::max(40., result.height + 16);
  }
  if (platform->type == ARKUI_NODE_TEXT_INPUT || platform->type == ARKUI_NODE_TEXT_AREA) {
    result.width = std::max(120., result.width + 32);
    result.height = std::max(platform->multiline ? 80. : 40., result.height + 16);
  }
  return result;
}
void View::Impl::AddNativeSubview(Impl& child, size_t index) {
  if (Main() && native && child.native)
    NodeApi()->insertChildAt(Node(*this), Node(child), index);
}
void View::Impl::RemoveNativeSubview(Impl& child) {
  if (Main()) Platform::HideTooltips(child);
  if (Main() && native && child.native)
    NodeApi()->removeChild(Node(*this), Node(child));
}
void View::Impl::SetNativeVisible(bool value) {
  if (Main() && !value) Platform::HideTooltips(*this);
  if (Main() && native && !is_root)
    NodeInt(Node(*this), NODE_VISIBILITY, value ? ARKUI_VISIBILITY_VISIBLE : ARKUI_VISIBILITY_NONE);
}
void View::Impl::SetNativeEnabled(bool value) {
  if (Main() && !value) Platform::HideTooltips(*this);
  if (Main() && native && !is_root)
    NodeInt(Node(*this), NODE_ENABLED, value);
}
bool View::Impl::IsNativeEnabled() const {
  return Int(Node(*this), NODE_ENABLED) != 0;
}
void View::Impl::SetNativeBackgroundColor(Color color) {
  ColorAttribute(Node(*this), NODE_BACKGROUND_COLOR, color);
}
void View::Impl::SetNativeTooltip(const std::optional<std::string>&) try {
  if (!Main() || !native) return;
  platform->HookTooltip(*this);
  if (platform->tooltip_hovered) platform->ShowTooltip(*this);
  else platform->HideTooltip();
} catch (...) {
  platform->tooltip_hovered = false;
}
void View::Impl::NativeFocus() {
  if (Main() && native && !is_root)
    NodeInt(Node(*this), NODE_FOCUS_STATUS, 1);
}
void View::Impl::NativeBlur() {
  if (Main() && native && !is_root)
    NodeInt(Node(*this), NODE_FOCUS_STATUS, 0);
}
bool View::Impl::IsNativeFocused() const {
  return Int(Node(*this), NODE_FOCUS_STATUS) != 0;
}
void View::Impl::StartNativeListening() {
  platform->listening = true;
}
void View::Impl::StopNativeListening() {
  platform->listening = false;
}
void View::Impl::ObserveNativeResize(bool observe) {
  platform->resize = observe;
  if (!Main() || !native)
    return;
  if (observe) {
    platform->Hook(*this);
    NodeApi()->registerNodeEvent(Node(*this), NODE_ON_SIZE_CHANGE, 6,
                                 reinterpret_cast<void*>(platform->token));
  } else
    NodeApi()->unregisterNodeEvent(Node(*this), NODE_ON_SIZE_CHANGE);
}
bool View::IsSupported() {
  return Main() && NodeApi();
}
bool View::IsBackendSupported(ViewBackend backend) {
  return backend == ViewBackend::Native && IsSupported();
}
bool View::SetDefaultBackend(ViewBackend backend) {
  return IsBackendSupported(backend);
}
ViewBackend View::GetDefaultBackend() {
  return ViewBackend::Native;
}
std::shared_ptr<View> Window::GetContentView() const {
  auto state = ohos::FindWindowState(GetNativeObject());
  return Main() && state && state->alive ? state->content_view : nullptr;
}
namespace ohos {
void DetachViewContent(const std::shared_ptr<WindowState>& state) {
  if (!state || !state->content_view)
    return;
  auto& impl = ViewInternal::Of(*state->content_view);
  impl.platform->Unmount(impl);
}
bool AttachViewContent(napi_env env, napi_value window, napi_value content, napi_value tooltip_host) {
  if (!Main())
    return false;
  auto wrapped = AttachWindow(env, window);
  auto state = wrapped ? FindWindowState(wrapped->GetNativeObject()) : nullptr;
  ArkUI_NodeContentHandle handle = nullptr;
  if (!state || state->env != env ||
      OH_ArkUI_GetNodeContentFromNapiValue(env, content, &handle) != 0 || !handle)
    return false;
  auto root = state->content_view;
  if (!root)
    root = std::make_shared<View>();
  auto& impl = ViewInternal::Of(*root);
  if (!impl.native)
    return false;
  if (impl.platform->content == handle) {
    if (!tooltip_host) return true;
    napi_value previous = nullptr;
    bool same = false;
    if (impl.platform->tooltip_host &&
        napi_get_reference_value(env, impl.platform->tooltip_host, &previous) == napi_ok &&
        napi_strict_equals(env, previous, tooltip_host, &same) == napi_ok && same) return true;
    napi_ref replacement = nullptr;
    if (napi_create_reference(env, tooltip_host, 1, &replacement) != napi_ok) return false;
    Impl::Platform::HideTooltips(impl);
    impl.platform->DisposeTooltipHost();
    impl.platform->tooltip_host = replacement;
    return true;
  }
  napi_ref reference = nullptr, tooltip_reference = nullptr;
  if (napi_create_reference(env, content, 1, &reference) != napi_ok)
    return false;
  if (tooltip_host && napi_create_reference(env, tooltip_host, 1, &tooltip_reference) != napi_ok) {
    napi_delete_reference(env, reference);
    return false;
  }
  impl.platform->Unmount(impl);
  if (OH_ArkUI_NodeContent_AddNode(handle, Node(impl)) != 0) {
    napi_delete_reference(env, reference);
    if (tooltip_reference) napi_delete_reference(env, tooltip_reference);
    return false;
  }
  impl.platform->tooltip_host = tooltip_reference;
  impl.platform->content = handle;
  impl.platform->content_ref = reference;
  NodeFloat(Node(impl), NODE_WIDTH_PERCENT, 1);
  NodeFloat(Node(impl), NODE_HEIGHT_PERCENT, 1);
  Impl::InitializeRoot(root, wrapped);
  state->content_view = root;
  return true;
}
}  // namespace ohos
Label::Label(const std::string& text) : View(NativeControl{Create(ARKUI_NODE_TEXT)}) {
  Control(*pimpl_, ARKUI_NODE_TEXT);
  SetText(text);
}
void Label::SetText(const std::string& text) {
  if (Main() && text.find('\0') == text.npos && NodeText(Node(*pimpl_), NODE_TEXT_CONTENT, text))
    pimpl_->InvalidateIntrinsicSize();
}
std::string Label::GetText() const {
  return Main() ? NodeTextValue(Node(*pimpl_), NODE_TEXT_CONTENT) : "";
}
void Label::SetTextColor(Color color) {
  ColorAttribute(Node(*pimpl_), NODE_FONT_COLOR, color);
  pimpl_->platform->text_color = color;
}
Color Label::GetTextColor() const {
  return pimpl_->platform->text_color;
}
void Label::SetFontSize(double size) {
  if (!Main() || !pimpl_->native || !std::isfinite(size) || size < 0)
    return;
  if (size)
    NodeFloat(Node(*pimpl_), NODE_FONT_SIZE, size);
  else
    NodeApi()->resetAttribute(Node(*pimpl_), NODE_FONT_SIZE);
  pimpl_->platform->font_size = size;
  pimpl_->InvalidateIntrinsicSize();
}
double Label::GetFontSize() const {
  return pimpl_->platform->font_size;
}
void Label::SetTextAlignment(TextAlignment value) {
  if (Main() && pimpl_->native && NodeInt(Node(*pimpl_), NODE_TEXT_ALIGN, static_cast<int>(value)))
    pimpl_->platform->text_alignment = value;
}
TextAlignment Label::GetTextAlignment() const {
  return pimpl_->platform->text_alignment;
}
Button::Button(const std::string& text) : View(NativeControl{Create(ARKUI_NODE_BUTTON)}) {
  Control(*pimpl_, ARKUI_NODE_BUTTON);
  SetText(text);
}
void Button::SetText(const std::string& text) {
  if (Main() && text.find('\0') == text.npos && NodeText(Node(*pimpl_), NODE_BUTTON_LABEL, text))
    pimpl_->InvalidateIntrinsicSize();
}
std::string Button::GetText() const {
  return Main() ? NodeTextValue(Node(*pimpl_), NODE_BUTTON_LABEL) : "";
}
TextField::TextField(const std::string& text) : View(NativeControl{Create(ARKUI_NODE_TEXT_INPUT)}) {
  Control(*pimpl_, ARKUI_NODE_TEXT_INPUT);
  pimpl_->platform->HookText(*pimpl_);
  SetText(text);
}
void TextField::SetText(const std::string& text) {
  if (!Main() || !pimpl_->native || text.find('\0') != text.npos)
    return;
  auto& p = *pimpl_->platform;
  auto previous = p.last_text;
  p.last_text = text;
  if (!NodeText(Node(*pimpl_), p.TextAttribute(), text))
    p.last_text = previous;
  pimpl_->InvalidateIntrinsicSize();
}
std::string TextField::GetText() const {
  return Main() ? NodeTextValue(Node(*pimpl_), pimpl_->platform->TextAttribute()) : "";
}
void TextField::SetTextColor(Color color) {
  ColorAttribute(Node(*pimpl_), NODE_FONT_COLOR, color);
  pimpl_->platform->text_color = color;
}
Color TextField::GetTextColor() const {
  return pimpl_->platform->text_color;
}
void TextField::SetFontSize(double size) {
  if (!Main() || !pimpl_->native || !std::isfinite(size) || size < 0)
    return;
  if (size)
    NodeFloat(Node(*pimpl_), NODE_FONT_SIZE, size);
  else
    NodeApi()->resetAttribute(Node(*pimpl_), NODE_FONT_SIZE);
  pimpl_->platform->font_size = size;
  pimpl_->InvalidateIntrinsicSize();
}
double TextField::GetFontSize() const {
  return pimpl_->platform->font_size;
}
void TextField::SetTextAlignment(TextAlignment value) {
  if (Main() && pimpl_->native && NodeInt(Node(*pimpl_), NODE_TEXT_ALIGN, static_cast<int>(value)))
    pimpl_->platform->text_alignment = value;
}
TextAlignment TextField::GetTextAlignment() const {
  return pimpl_->platform->text_alignment;
}
void TextField::SetPlaceholder(const std::optional<std::string>& text) {
  if (!Main() || !pimpl_->native || (text && text->find('\0') != text->npos))
    return;
  if (NodeText(Node(*pimpl_), pimpl_->platform->PlaceholderAttribute(), text.value_or("")))
    pimpl_->platform->placeholder = text;
}
std::optional<std::string> TextField::GetPlaceholder() const {
  return pimpl_->platform->placeholder;
}
void TextField::SetEditable(bool value) {
  if (Main())
    pimpl_->platform->editable = value;
}
bool TextField::IsEditable() const {
  return pimpl_->platform->editable;
}
void TextField::SetSecure(bool value) {
  if (!Main() || !pimpl_->native || pimpl_->platform->multiline)
    return;
  if (NodeInt(Node(*pimpl_), NODE_TEXT_INPUT_TYPE,
              value ? ARKUI_TEXTINPUT_TYPE_PASSWORD : ARKUI_TEXTINPUT_TYPE_NORMAL)) {
    pimpl_->platform->secure = value;
    NodeInt(Node(*pimpl_), NODE_TEXT_INPUT_SHOW_PASSWORD_ICON, 0);
  }
}
bool TextField::IsSecure() const {
  return pimpl_->platform->secure;
}
void TextField::SetMultiline(bool value) {
  auto& p = *pimpl_->platform;
  if (!Main() || !pimpl_->native || value == p.multiline || (value && p.secure))
    return;
  auto replacement = Create(value ? ARKUI_NODE_TEXT_AREA : ARKUI_NODE_TEXT_INPUT);
  if (!replacement)
    return;
  const auto text = GetText();
  const auto bounds = GetFrame();
  const bool enabled = IsEnabled();
  p.HideTooltip(true);
  auto old = Node(*pimpl_);
  auto parent = GetParent();
  NodeApi()->removeNodeEventReceiver(old, Impl::Platform::Event);
  if (parent)
    NodeApi()->removeChild(static_cast<ArkUI_NodeHandle>(parent->GetNativeObject()), old);
  pimpl_->NativeReplaced(replacement);
  p.multiline = value;
  Control(*pimpl_, value ? ARKUI_NODE_TEXT_AREA : ARKUI_NODE_TEXT_INPUT);
  p.HookText(*pimpl_);
  NodeText(replacement, NODE_ID, "nativeapi-view-" + std::to_string(GetId()));
  if (parent) {
    auto siblings = parent->GetSubviews();
    auto it = std::find_if(siblings.begin(), siblings.end(),
                           [this](const auto& v) { return v.get() == this; });
    NodeApi()->insertChildAt(static_cast<ArkUI_NodeHandle>(parent->GetNativeObject()), replacement,
                             std::distance(siblings.begin(), it));
  }
  SetText(text);
  SetPlaceholder(p.placeholder);
  SetTextColor(p.text_color);
  SetFontSize(p.font_size);
  SetTextAlignment(p.text_alignment);
  pimpl_->SetNativeFrame(bounds);
  SetEnabled(enabled);
  pimpl_->SetNativeVisible(IsVisible());
  SetBackgroundColor(GetBackgroundColor());
  NodeApi()->disposeNode(old);
  pimpl_->InvalidateIntrinsicSize();
}
bool TextField::IsMultiline() const {
  return pimpl_->platform->multiline;
}
ImageView::ImageView() : View(NativeControl{Create(ARKUI_NODE_IMAGE)}) {
  Control(*pimpl_, ARKUI_NODE_IMAGE);
  if (pimpl_->native)
    NodeInt(Node(*pimpl_), NODE_IMAGE_OBJECT_FIT, ARKUI_OBJECT_FIT_CONTAIN);
}
void ImageView::SetImage(std::shared_ptr<Image> image) {
  if (!Main() || !pimpl_->native)
    return;
  auto& p = *pimpl_->platform;
  auto* descriptor = image ? OH_ArkUI_DrawableDescriptor_CreateFromPixelMap(
                                 static_cast<OH_PixelmapNativeHandle>(image->GetNativeObject()))
                           : nullptr;
  if (image && !descriptor)
    return;
  int code = 0;
  if (descriptor) {
    ArkUI_AttributeItem item{nullptr, 0, nullptr, descriptor};
    code = NodeApi()->setAttribute(Node(*pimpl_), NODE_IMAGE_SRC, &item);
  } else
    code = NodeApi()->resetAttribute(Node(*pimpl_), NODE_IMAGE_SRC);
  if (code) {
    if (descriptor)
      OH_ArkUI_DrawableDescriptor_Dispose(descriptor);
    return;
  }
  if (p.drawable)
    OH_ArkUI_DrawableDescriptor_Dispose(p.drawable);
  p.drawable = descriptor;
  p.image = std::move(image);
  pimpl_->InvalidateIntrinsicSize();
}
std::shared_ptr<Image> ImageView::GetImage() const {
  return pimpl_->platform->image;
}
}  // namespace nativeapi
