#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "view.h"
#include "window.h"

namespace nativeapi {

/// Token for the protected View(NativeControl) constructor: a native control
/// a subclass created and the view now owns.
struct View::NativeControl {
  void* native;
};

/// Lets the platform files name the protected types without being members.
struct ViewInternal {
  using Impl = View::Impl;
  using NativeControl = View::NativeControl;
};

/**
 * Shared state of every View (view.cpp). The platform file defines Platform,
 * the constructor / destructor pair and the `Native*` seam below; everything
 * else — the subview tree, layout, ids — is written once.
 *
 * Frames are logical points, origin at the parent's top-left, y down. The
 * platform converts to its own coordinate system inside the seam.
 */
class View::Impl {
 public:
  /**
   * @param owner The view this belongs to.
   * @param native The native view to wrap; nullptr creates a new container.
   * @param owned Whether the native view is destroyed with this Impl. Always
   *        true for a view this library created, false for a wrapped one.
   */
  Impl(View* owner, void* native, bool owned);
  ~Impl();

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  // ---- shared (view.cpp) ----

  /// Lays out this view's subviews per `layout`, then recurses.
  void Relayout();
  /// Lays the whole tree out again, from the root down. Called after any
  /// change that can move or resize something.
  void RelayoutTree();
  /// The top of this view's tree (itself when detached).
  View* Root() const;
  /// Called by the platform when the native frame of a root view changed.
  void OnNativeResized();
  /// Marks `view` as a window's root: it wraps the window's content view,
  /// follows its size and answers GetWindow(). Called by Window::GetContentView.
  static void InitializeRoot(const std::shared_ptr<View>& view, std::weak_ptr<Window> window);
  /// Called by a control after a change that alters its intrinsic size.
  void InvalidateIntrinsicSize();
  /// Called by a platform that replaced the native view: keeps `native` and
  /// the tree's z-order bookkeeping consistent.
  void NativeReplaced(void* replacement) { native = replacement; }
  /// Emits `event` from the owner. For the platform callbacks.
  template <typename E>
  void Emit(const E& event) {
    owner->EventEmitter<ViewEvent>::Emit(event);
  }

  View* owner;
  /// The native view. A platform that swaps the control for another one
  /// (a text field turning secure) must point this at the replacement.
  void* native = nullptr;
  bool owned = false;
  bool is_root = false;
  /// Set by the platform in the constructor; views of one tree share it.
  ViewBackend backend = ViewBackend::Native;
  ViewId id = IdAllocator::kInvalidId;
  std::weak_ptr<View> parent;
  std::weak_ptr<Window> window;  // root views only
  std::vector<std::shared_ptr<View>> subviews;

  ViewLayout layout = ViewLayout::Absolute;
  double spacing = 0.0;
  EdgeInsets padding{0.0, 0.0, 0.0, 0.0};
  double flex = 0.0;
  ViewAlignment alignment = ViewAlignment::Stretch;
  Size preferred_size{0.0, 0.0};
  /// The frame last requested through SetFrame(); what an Absolute parent uses.
  Rectangle frame{0.0, 0.0, 0.0, 0.0};
  bool has_frame = false;
  /// The frame an Absolute parent gives this view: `frame` once SetFrame() was
  /// called, otherwise the intrinsic size at the origin.
  Rectangle AbsoluteFrame() const;
  bool visible = true;
  Color background_color{0, 0, 0, 0};
  std::optional<std::string> tooltip;

  // ---- platform seam ----

  /// Creates the native container for a plain View.
  static void* CreateNativeContainer();
  /// The platform must never destroy a subview's native object when this one
  /// goes away: a subview the caller still holds stays usable (view.cpp resets
  /// its parent pointer; the platform detaches or parks the native child).
  void SetNativeFrame(Rectangle frame);
  Rectangle GetNativeFrame() const;
  /// The size the control wants; zero for a container.
  Size GetNativeIntrinsicSize() const;
  /// Reparents `child`'s native view under this one at z-index `index`.
  /// Called after `subviews` already holds the child at `index`, so the
  /// siblings around it can be looked up there.
  void AddNativeSubview(Impl& child, size_t index);
  void RemoveNativeSubview(Impl& child);
  /// No-ops on a root view: hiding or disabling the window is Window's job.
  void SetNativeVisible(bool is_visible);
  void SetNativeEnabled(bool is_enabled);
  bool IsNativeEnabled() const;
  void SetNativeBackgroundColor(Color color);
  void SetNativeTooltip(const std::optional<std::string>& tooltip);
  void NativeFocus();
  void NativeBlur();
  bool IsNativeFocused() const;
  /// Hooks up / tears down the native callbacks that produce ViewEvents.
  void StartNativeListening();
  void StopNativeListening();
  /// Root views only: start / stop watching the native frame for changes.
  void ObserveNativeResize(bool observe);

  struct Platform;
  std::unique_ptr<Platform> platform;
};

}  // namespace nativeapi
