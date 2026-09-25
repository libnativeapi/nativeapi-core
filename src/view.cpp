#include "view.h"

#include <algorithm>

#include "view_impl.h"
#include "view_layout.h"

namespace nativeapi {

// ---------------------------------------------------------------------------
// Impl: the shared half
// ---------------------------------------------------------------------------

View* View::Impl::Root() const {
  View* view = owner;
  while (auto parent = view->pimpl_->parent.lock()) {
    view = parent.get();
  }
  return view;
}

void View::Impl::RelayoutTree() {
  Root()->pimpl_->Relayout();
}

void View::Impl::InvalidateIntrinsicSize() {
  RelayoutTree();
}

void View::Impl::OnNativeResized() {
  Relayout();
}

Rectangle View::Impl::AbsoluteFrame() const {
  if (has_frame) {
    return frame;
  }
  const Size intrinsic = GetNativeIntrinsicSize();
  return Rectangle{0.0, 0.0, intrinsic.width, intrinsic.height};
}

void View::Impl::Relayout() {
  if (layout == ViewLayout::Absolute) {
    // Re-applied on every pass: on platforms whose native origin is bottom-left
    // a child's position depends on the parent's height.
    for (const auto& subview : subviews) {
      subview->pimpl_->SetNativeFrame(subview->pimpl_->AbsoluteFrame());
    }
  } else if (!subviews.empty()) {
    std::vector<LayoutChild> children;
    children.reserve(subviews.size());
    for (const auto& subview : subviews) {
      auto& child = *subview->pimpl_;
      LayoutChild entry;
      entry.visible = child.visible;
      entry.preferred = child.preferred_size;
      entry.intrinsic = child.visible ? child.GetNativeIntrinsicSize() : Size{0.0, 0.0};
      entry.flex = child.flex;
      entry.alignment = child.alignment;
      children.push_back(entry);
    }
    const Rectangle own = GetNativeFrame();
    const auto frames = ComputeStackLayout(layout, Size{own.width, own.height}, padding, spacing,
                                           children);
    for (size_t i = 0; i < subviews.size(); ++i) {
      if (children[i].visible) {
        subviews[i]->pimpl_->SetNativeFrame(frames[i]);
      }
    }
  }
  for (const auto& subview : subviews) {
    subview->pimpl_->Relayout();
  }
}

void View::Impl::InitializeRoot(const std::shared_ptr<View>& view, std::weak_ptr<Window> window) {
  auto& impl = *view->pimpl_;
  impl.is_root = true;
  impl.window = std::move(window);
  impl.ObserveNativeResize(true);
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------

View::View() : pimpl_(std::make_unique<Impl>(this, nullptr, true)) {}

View::View(void* native_view) : pimpl_(std::make_unique<Impl>(this, native_view, false)) {}

View::View(NativeControl control) : pimpl_(std::make_unique<Impl>(this, control.native, true)) {}

View::~View() {
  // Subviews must let go of their parent pointer before the native tree is
  // torn down, and a root stops watching the content view it never owned.
  for (const auto& subview : pimpl_->subviews) {
    subview->pimpl_->parent.reset();
  }
  if (pimpl_->is_root) {
    pimpl_->ObserveNativeResize(false);
  }
}

ViewId View::GetId() const {
  return pimpl_->id;
}

// === Tree ===

void View::AddSubview(std::shared_ptr<View> subview) {
  InsertSubview(pimpl_->subviews.size(), std::move(subview));
}

void View::InsertSubview(size_t index, std::shared_ptr<View> view) {
  if (!view || view.get() == this || !pimpl_->native || !view->pimpl_->native) {
    return;
  }
  // No cycles: a view cannot be added under one of its own descendants.
  for (View* ancestor = this; ancestor != nullptr;) {
    if (ancestor == view.get()) {
      return;
    }
    auto parent = ancestor->pimpl_->parent.lock();
    ancestor = parent.get();
  }
  if (auto old_parent = view->pimpl_->parent.lock()) {
    old_parent->RemoveSubview(view);
  }
  index = std::min(index, pimpl_->subviews.size());
  view->pimpl_->parent = weak_from_this();
  pimpl_->subviews.insert(pimpl_->subviews.begin() + static_cast<std::ptrdiff_t>(index), view);
  pimpl_->AddNativeSubview(*view->pimpl_, index);
  pimpl_->RelayoutTree();
}

bool View::RemoveSubview(std::shared_ptr<View> subview) {
  if (!subview) {
    return false;
  }
  auto& subviews = pimpl_->subviews;
  auto it = std::find(subviews.begin(), subviews.end(), subview);
  if (it == subviews.end()) {
    return false;
  }
  return RemoveSubviewAt(static_cast<size_t>(it - subviews.begin()));
}

bool View::RemoveSubviewAt(size_t index) {
  auto& subviews = pimpl_->subviews;
  if (index >= subviews.size()) {
    return false;
  }
  // Keep the subview alive past the erase: the caller may hold no reference.
  std::shared_ptr<View> view = subviews[index];
  subviews.erase(subviews.begin() + static_cast<std::ptrdiff_t>(index));
  view->pimpl_->parent.reset();
  pimpl_->RemoveNativeSubview(*view->pimpl_);
  pimpl_->RelayoutTree();
  return true;
}

void View::ClearSubviews() {
  while (!pimpl_->subviews.empty()) {
    RemoveSubviewAt(pimpl_->subviews.size() - 1);
  }
}

size_t View::GetSubviewCount() const {
  return pimpl_->subviews.size();
}

std::shared_ptr<View> View::GetSubviewAt(size_t index) const {
  if (index >= pimpl_->subviews.size()) {
    return nullptr;
  }
  return pimpl_->subviews[index];
}

std::vector<std::shared_ptr<View>> View::GetSubviews() const {
  return pimpl_->subviews;
}

std::shared_ptr<View> View::GetParent() const {
  return pimpl_->parent.lock();
}

std::shared_ptr<Window> View::GetWindow() const {
  return pimpl_->Root()->pimpl_->window.lock();
}

// === Geometry & layout ===

void View::SetFrame(Rectangle frame) {
  if (pimpl_->is_root) {
    return;
  }
  pimpl_->frame = frame;
  pimpl_->has_frame = true;
  auto parent = pimpl_->parent.lock();
  if (!parent || parent->pimpl_->layout == ViewLayout::Absolute) {
    pimpl_->SetNativeFrame(frame);
  }
  pimpl_->RelayoutTree();
}

Rectangle View::GetFrame() const {
  return pimpl_->GetNativeFrame();
}

void View::SetPreferredSize(Size size) {
  pimpl_->preferred_size = size;
  pimpl_->RelayoutTree();
}

Size View::GetPreferredSize() const {
  return pimpl_->preferred_size;
}

Size View::GetIntrinsicSize() const {
  return pimpl_->GetNativeIntrinsicSize();
}

void View::SetFlex(double flex) {
  pimpl_->flex = flex < 0.0 ? 0.0 : flex;
  pimpl_->RelayoutTree();
}

double View::GetFlex() const {
  return pimpl_->flex;
}

void View::SetAlignment(ViewAlignment alignment) {
  pimpl_->alignment = alignment;
  pimpl_->RelayoutTree();
}

ViewAlignment View::GetAlignment() const {
  return pimpl_->alignment;
}

void View::SetLayout(ViewLayout layout) {
  pimpl_->layout = layout;
  pimpl_->RelayoutTree();
}

ViewLayout View::GetLayout() const {
  return pimpl_->layout;
}

void View::SetSpacing(double spacing) {
  pimpl_->spacing = spacing < 0.0 ? 0.0 : spacing;
  pimpl_->RelayoutTree();
}

double View::GetSpacing() const {
  return pimpl_->spacing;
}

void View::SetPadding(EdgeInsets padding) {
  pimpl_->padding = padding;
  pimpl_->RelayoutTree();
}

EdgeInsets View::GetPadding() const {
  return pimpl_->padding;
}

// === Appearance & state ===

void View::SetVisible(bool is_visible) {
  pimpl_->visible = is_visible;
  pimpl_->SetNativeVisible(is_visible);
  pimpl_->RelayoutTree();
}

bool View::IsVisible() const {
  return pimpl_->visible;
}

void View::SetEnabled(bool is_enabled) {
  pimpl_->SetNativeEnabled(is_enabled);
}

bool View::IsEnabled() const {
  return pimpl_->IsNativeEnabled();
}

void View::SetBackgroundColor(Color color) {
  pimpl_->background_color = color;
  pimpl_->SetNativeBackgroundColor(color);
}

Color View::GetBackgroundColor() const {
  return pimpl_->background_color;
}

void View::SetTooltip(const std::optional<std::string>& tooltip) {
  pimpl_->tooltip = tooltip;
  pimpl_->SetNativeTooltip(tooltip);
}

std::optional<std::string> View::GetTooltip() const {
  return pimpl_->tooltip;
}

void View::Focus() {
  pimpl_->NativeFocus();
}

void View::Blur() {
  pimpl_->NativeBlur();
}

bool View::IsFocused() const {
  return pimpl_->IsNativeFocused();
}

void View::StartEventListening() {
  pimpl_->StartNativeListening();
}

void View::StopEventListening() {
  pimpl_->StopNativeListening();
}

void* View::GetNativeObjectInternal() const {
  return pimpl_->native;
}

// ---------------------------------------------------------------------------
// Window: the root view cache
// ---------------------------------------------------------------------------

std::shared_ptr<View> Window::ContentViewFor(void* native_content_view) const {
  if (!native_content_view || !View::IsSupported()) {
    return nullptr;
  }
  if (content_view_ && content_view_->GetNativeObject() == native_content_view) {
    return content_view_;
  }
  auto view = std::make_shared<View>(native_content_view);
  View::Impl::InitializeRoot(view, const_cast<Window*>(this)->weak_from_this());
  content_view_ = view;
  return view;
}

// The controls' constructors and methods are platform code; only the
// destructors are shared.

Label::~Label() = default;
Button::~Button() = default;
TextField::~TextField() = default;
ImageView::~ImageView() = default;

}  // namespace nativeapi
