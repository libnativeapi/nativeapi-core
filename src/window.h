#pragma once
#include <memory>
#include <string>
#include "foundation/color.h"
#include "foundation/event.h"
#include "foundation/event_emitter.h"
#include "foundation/geometry.h"
#include "foundation/id_allocator.h"
#include "foundation/native_object_provider.h"

namespace nativeapi {

class EventRequest;
namespace detail { struct WindowEventSubscription; struct WindowPropertyDispatch; }

class View;
class WindowShape;
class WindowShadow;

/**
 * @typedef WindowId
 * @brief Unique identifier for a window instance.
 *
 * This type is used to uniquely identify window instances across the system.
 * Each window gets assigned a unique ID when created.
 */
typedef IdAllocator::IdType WindowId;

/**
 * Base class for all window-related events
 *
 * This class provides common functionality for window events,
 * including access to the window ID that triggered the event.
 *
 * WindowManager reports observed changes for every process window. Window
 * subscriptions receive those observations for their native identity, and
 * Window itself produces cancellable close requests before its native action.
 */
class WindowEvent : public Event {
 public:
  /**
   * Constructor for WindowEvent
   * @param window_id The window ID associated with this event
   */
  explicit WindowEvent(WindowId window_id) : window_id_(window_id) {}

  /**
   * Virtual destructor
   */
  virtual ~WindowEvent() = default;

  /**
   * Get the window ID associated with this event
   * @return The window ID
   */
  WindowId GetWindowId() const { return window_id_; }

  /**
   * Get a string representation of the event type (for debugging)
   * Default implementation returns "WindowEvent"
   */
  std::string GetTypeName() const override { return "WindowEvent"; }

 private:
  WindowId window_id_;
};

/**
 * @brief Title bar style options for windows.
 *
 * Defines how a window's title bar should be displayed. This affects the
 * appearance and visibility of the standard window title bar including the
 * title text and window control buttons (minimize, maximize, close).
 *
 * @note Platform behavior may vary:
 * - Windows: Hidden style removes the title bar; the desktop compositor's frame (border,
 *   shadow, rounded corners) stays. Without a shadow, or with a custom shadow or a shape,
 *   the window has no frame at all and its edges resize from inside the content
 * - macOS: Hidden style creates a borderless window with transparent title bar
 * - Linux: Hidden style removes window decorations entirely
 */
enum class TitleBarStyle {
  /**
   * Standard title bar with default platform appearance.
   * Shows title text and standard window control buttons.
   */
  Normal,

  /**
   * No title bar and no window control buttons: the window is bare on every
   * platform, for an application that draws its own chrome.
   *
   * The content owns the area where the title bar was: dragging there does
   * not move the window. Move it from custom chrome with
   * Window::StartDragging() (or a WindowDragSession). Handle a double-click
   * in that custom chrome with Window::PerformTitleBarDoubleClick(). On macOS,
   * empty backgrounds in the original title-bar band automatically perform that
   * action; controls, custom mouse handlers and host window dispatchers keep it.
   * - macOS: the content extends under a title bar that is transparent and
   *   empty; the window buttons are hidden. The system is kept from moving
   *   the window, even while IsMovable() is true. To keep the buttons over
   *   the content, use Normal with SetContentUnderTitleBar() instead,
   *   or turn them back on with SetWindowControlButtonsVisible() after
   *   setting this style.
   * - Windows: the content reaches the top edge of the window; the resize
   *   border stays on the other sides. A band as thick as that border along
   *   the top of the content still resizes the window, also over child
   *   windows of the same thread (such as a Flutter view), so content there
   *   does not receive the mouse.
   * - Linux: the window's header bar is hidden, which takes its buttons with
   *   it.
   *
   * Switching between styles keeps the window's frame (position and outer
   * size); the content area grows or shrinks by the title bar instead, and
   * the window control buttons go back to what the style implies - set
   * SetWindowControlButtonsVisible() afterwards to override that.
   */
  Hidden
};

/**
 * @brief Screen edge reserved by a window docked as a desktop toolbar.
 * @see Window::Dock() for platform availability.
 */
enum class WindowDockSide {
  /** No desktop space is reserved. */
  None,
  /** Reserve space at the left edge of the window's monitor. */
  Left,
  /** Reserve space at the right edge of the window's monitor. */
  Right
};

/**
 * @brief Preferred rounding of the corners drawn by the window compositor.
 * @see Window::SetCornerPreference() for platform availability.
 */
enum class WindowCornerPreference {
  /** Let the system choose its normal corner style. */
  Default,
  /** Ask the system to leave the corners square. */
  DoNotRound,
  /** Ask the system to round the corners. */
  Round,
  /** Ask the system to use a smaller corner radius. */
  RoundSmall
};

/**
 * @brief A window property reported by WindowPropertyChangedEvent.
 *
 * Each value names a getter on Window; read it there for the new value.
 */
enum class WindowProperty {
  /** Window::GetTitle() */
  Title,
  /** Window::IsResizable() */
  Resizable,
  /** Window::IsMovable() */
  Movable,
  /** Window::IsMinimizable() */
  Minimizable,
  /** Window::IsMaximizable() */
  Maximizable,
  /** Window::IsFullScreenable() */
  FullScreenable,
  /** Window::IsClosable() */
  Closable,
  /** Window::IsWindowControlButtonsVisible() */
  WindowControlButtonsVisible,
  /** Window::IsAlwaysOnTop() */
  AlwaysOnTop,
  /** Window::IsAlwaysOnBottom() */
  AlwaysOnBottom,
  /** Window::GetTitleBarStyle() */
  TitleBarStyle
};

/**
 * @brief Whether any part of a window can be seen.
 * @see Window::GetOcclusionState() for platform availability.
 */
enum class WindowOcclusionState {
  /** The platform cannot tell. */
  Unknown,
  /** Some part of the window is on screen and not covered. */
  Visible,
  /** Nothing of the window can be seen: hidden, minimized, on another
      workspace, off screen or covered by other windows. */
  Occluded
};

/**
 * @brief Translucent materials that can replace a window's background.
 *
 * A visual effect blurs or samples whatever is behind the window and draws the
 * result where the background color would be. The window's content has to leave
 * that area unpainted for the material to show.
 *
 * The values are named after the platform that defines the material. Each of
 * them is accepted on every platform that has visual effects at all; where the
 * exact material does not exist, the closest one is used, as listed per value.
 * Linux, Android, iOS and OpenHarmony have no visual effects.
 *
 * @see Window::SetVisualEffect() for platform availability.
 */
enum class VisualEffect {
  /** No visual effect: the window shows its background color. */
  None,

  /**
   * A plain blur of what is behind the window, the most see-through material.
   * - Windows: the acrylic system backdrop on Windows 11 22H2 and later, the
   *   nearest material that covers the whole window there; the plain blur behind
   *   on Windows 10, which stops at the title bar
   * - macOS: NSVisualEffectMaterialSidebar
   */
  Blur,

  /**
   * A heavier, tinted blur.
   * - Windows: Acrylic - the system backdrop on Windows 11 22H2 and later, the
   *   acrylic blur-behind on Windows 10 1803 and later
   * - macOS: NSVisualEffectMaterialUnderWindowBackground
   */
  Acrylic,

  /**
   * A nearly opaque material tinted by the desktop wallpaper.
   * - Windows: Mica (Windows 11 22H2 and later)
   * - macOS: NSVisualEffectMaterialWindowBackground
   */
  Mica,

  /**
   * Mica with a stronger tint, meant for windows with tabs in the title bar.
   * - Windows: Mica Alt (Windows 11 22H2 and later)
   * - macOS: NSVisualEffectMaterialTitlebar
   */
  MicaAlt,

  /**
   * The dark material of heads-up panels.
   * - Windows: same as Acrylic
   * - macOS: NSVisualEffectMaterialHUDWindow
   */
  Hud,

  /**
   * The material of popovers.
   * - Windows: same as Acrylic
   * - macOS: NSVisualEffectMaterialPopover
   */
  Popover,

  /**
   * The material of menus, which suits a window shown from a tray icon.
   * - Windows: same as Acrylic
   * - macOS: NSVisualEffectMaterialMenu
   */
  Menu
};

/**
 * @brief Window edges and corners that a user-driven resize can start from.
 *
 * Passed to Window::StartResizing() to select which edge or corner follows
 * the mouse. Edges are named from the user's point of view, so Top is the
 * edge nearest the title bar on every platform.
 */
enum class ResizeEdge {
  /** The top edge; dragging changes the height while the bottom edge stays. */
  Top,
  /** The left edge; dragging changes the width while the right edge stays. */
  Left,
  /** The right edge; dragging changes the width while the left edge stays. */
  Right,
  /** The bottom edge; dragging changes the height while the top edge stays. */
  Bottom,
  /** The top-left corner; both width and height change. */
  TopLeft,
  /** The top-right corner; both width and height change. */
  TopRight,
  /** The bottom-left corner; both width and height change. */
  BottomLeft,
  /** The bottom-right corner; both width and height change. */
  BottomRight
};

/**
 * @class Window
 * @brief Cross-platform window abstraction class.
 *
 * This class provides a unified interface for creating and managing windows
 * across different operating systems. It encapsulates all window-related
 * functionality including size, position, visibility, focus, and appearance.
 *
 * The Window class uses the PIMPL idiom to hide platform-specific implementation
 * details and provide a clean, consistent API across all supported platforms.
 *
 * @note OpenHarmony: the ArkTS host must attach its WindowStage or existing
 *       windows through platform/ohos/ohos_window.h. The host owns creation,
 *       content and Ability lifecycle. Geometry is in logical units, converted
 *       with the current display density. Asynchronous native operations take
 *       effect later; getters read the actual native state. Unsupported device
 *       or window operations leave that state unchanged. GetTitle() and policy
 *       getters without an SDK query remember only successful nativeapi changes.
 *
 * @note This class is not thread-safe. All window operations should be performed
 *       on the main UI thread.
 */
class Window : public EventEmitter<WindowEvent>, public NativeObjectProvider,
               public std::enable_shared_from_this<Window> {
 public:
  /**
   * @brief Default constructor creates a new window with default settings.
   *
   * Creates a new window with platform-default size, position, and properties.
   * The window is initially hidden and must be explicitly shown.
   * The window is automatically registered in the WindowRegistry.
   * @note OpenHarmony: creation is asynchronous and belongs to WindowStage.
   *       Use the platform host's CreateSubWindow completion instead. This
   *       constructor leaves an unattached, invalid wrapper on OpenHarmony.
   */
  Window();

  /**
   * @brief Constructor that wraps an existing native window object.
   *
   * @param window Pointer to an existing platform-specific window object
   * @note The Window instance takes ownership of the native window object.
   *       On OpenHarmony the argument is a borrowed opaque token returned by
   *       an attached Window's GetNativeObject(), not an OHNativeWindow surface
   *       or a napi_value. The host retains native ownership.
   */
  Window(void* native_window);

  /**
   * @brief Virtual destructor ensures proper cleanup of resources.
   *
   * Destroys the window and releases all associated resources including
   * the native window object.
   */
  virtual ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;

  /**
   * @brief Whether cancellable close requests are supported on this platform.
   * @return True on desktop platforms and an initialized OpenHarmony host.
   *         OpenHarmony accepts requests only for attached child windows;
   *         Ability-owned main windows cannot be destroyed with Close().
   */
  static bool IsCloseSupported();

  /**
   * @brief Requests closing this native window, subject to confirmation.
   *
   * Emits WindowCloseRequestedEvent on this window's subscribed wrappers. A
   * listener may cancel, or keep an owned EventDecision while awaiting work.
   * All approvals continue the host's original close handling on the UI thread;
   * a host can still refuse. Repeated calls share the pending request. Native
   * destruction invalidates its decisions, even if a handle still exists.
   * Close requests are object-level events, not WindowManager notifications.
   *
   * @return True if the request was submitted (not proof the window closed),
   *         false for unsupported platforms, immediate invalidity or dispatch failure.
   * @note May queue a lifetime-guarded request to the target UI loop. Successful
   *       submission does not prove native validity; it is rechecked on UI.
   *       Host close handling may also exit the app.
   * @note Platform availability:
   * - macOS: ✅ Supported - Continues performClose and the host delegate.
   * - Windows: ✅ Supported - Continues the host WM_CLOSE handler.
   * - Linux: ✅ Supported - Continues GTK delete-event handling, X11 or Wayland.
   * - Android: ❌ Unsupported - Returns false.
   * - iOS: ❌ Unsupported - Returns false.
   * - OpenHarmony: ⚠️ Close() accepts attached child windows only; main windows
   *   return false. Public SDK title-button closes on attached main/child windows
   *   also emit this object's cancellable/deferred request. Approval resumes the
   *   original SDK close; nativeapi does not terminate the Ability itself.
   */
  bool Close();

  /**
   * @brief Gets the unique identifier for this window.
   *
   * @return WindowId The unique identifier assigned to this window
   */
  WindowId GetId() const;

  // === Content view ===

  /**
   * @brief Gets the view filling the window's content area.
   *
   * Created on first call and cached: the same instance is returned for the
   * life of the window. It wraps the window's existing content view (a Flutter
   * or GPUI view when a host framework owns the window), so subviews added to
   * it sit on top of that content. Its frame follows the content area;
   * View::SetFrame() on it is ignored.
   *
   * On OpenHarmony, the host attaches a native ContentSlot first. The root
   * fills that slot, is cached across slot reattachment and leaves surrounding
   * ArkTS content alone. Without a slot this method returns nullptr.
   *
   * @return The root view, or nullptr when View::IsSupported() is false or
   *         the native window is gone.
   */
  std::shared_ptr<View> GetContentView() const;

  // === Focus Management ===

  /**
   * @brief Brings the window to the front and gives it keyboard focus.
   *
   * Makes this window the active window and brings it to the foreground,
   * restoring it first when minimized. Does nothing while IsFocusable() is
   * false. The window that had the focus before is remembered for Blur(); so
   * is it by Show(). The request is asynchronous on every platform: check
   * IsFocused() after the platform reports the change (WindowFocusedEvent).
   *
   * @note Platform availability:
   * - macOS: ✅ Supported - Activates the application unless IsNonActivating().
   *   macOS 14+ activation is cooperative, so the system can still decline it.
   * - Windows: ⚠️ Best effort - When the foreground lock refuses the request it
   *   is retried with the foreground thread's input state attached; the
   *   system may still only flash the taskbar button.
   * - Linux: ⚠️ Best effort - Presents with the current event or X server time;
   *   the window manager or compositor (Wayland: activation tokens) decides.
   * - Android: ❌ Not applicable - Focus follows the Activity lifecycle
   * - iOS: ⚠️ Partial - Makes the window the key window
   * - OpenHarmony: ⚠️ Attached main/child windows only; asynchronous focus transfer within the application.
   */
  void Focus();

  /**
   * @brief Removes keyboard focus from the window and returns it to where it
   *        was before.
   *
   * The window stays visible and in place. The focus goes to the window that
   * had it before the last Focus() or Show() of this window, if that one can
   * still take it: another window of this application, or another
   * application (the launcher case: hide a search bar and type into the
   * previous app again). Otherwise the platform picks the next window. Does
   * nothing when the window does not have the focus.
   *
   * @note Platform availability:
   * - macOS: ✅ Supported - Another app is activated after yielding activation
   *   to it; with no target, the app this one last took the focus from.
   * - Windows: ✅ Supported - Without a usable target, the next window down the
   *   Z order gets the foreground.
   * - Linux: ⚠️ Partial - X11 asks the window manager to activate the previous
   *   window, else the next one down its stacking order. Wayland can only
   *   return focus to another window of this application; otherwise the
   *   window is lowered and the compositor decides.
   * - Android: ❌ Not applicable - Focus follows the Activity lifecycle
   * - iOS: ⚠️ Partial - Resigns the key window
   * - OpenHarmony: ⚠️ Returns focus to a visible attached window of the same application.
   */
  void Blur();

  /**
   * @brief Checks if the window currently has keyboard focus.
   *
   * @return true if the window has focus, false otherwise
   */
  bool IsFocused() const;

  // === Visibility Management ===

  /**
   * @brief Shows the window and brings it to the front.
   *
   * Makes the window visible and typically gives it focus. If the window
   * was minimized, it will be restored to its previous state.
   * @note OpenHarmony also restores the owning Ability's SDK application-hidden
   * window group for an activating show. A non-activating show keeps the SDK's
   * focusOnShow=false behavior; restoration from application hiding may require
   * an explicit Application::Show().
   */
  void Show();

  /**
   * @brief Shows the window without giving it focus.
   *
   * Makes the window visible but does not change the currently focused window.
   * Useful for showing auxiliary windows or notifications.
   */
  void ShowInactive();

  /**
   * @brief Hides the window from view.
   *
   * Makes the window invisible but does not destroy it. The window can
   * be shown again later with Show() or ShowInactive().
   */
  void Hide();

  /**
   * @brief Checks if the window is currently visible.
   *
   * @return true if the window is visible, false if hidden or minimized
   * @note OpenHarmony includes successful SDK application hiding for the owning
   * stage's main window and registered descendants. The SDK's per-window showing
   * flag alone remains true while the Ability's whole window group is hidden.
   */
  bool IsVisible() const;

  /**
   * @brief Tells whether any part of the window can be seen.
   *
   * Unlike IsVisible(), which only says the window is shown, this accounts for
   * minimizing, other workspaces and other windows on top of it. Useful to
   * pause rendering or video nobody can see. WindowOcclusionChangedEvent
   * reports changes.
   *
   * @return The current state, or WindowOcclusionState::Unknown where the
   *         platform cannot tell.
   *
   * @note Platform availability:
   * - macOS: ✅ Supported - NSWindow.occlusionState, as the system computes it.
   * - Windows: ⚠️ Approximated - Hidden, minimized and cloaked (another
   *   virtual desktop) windows are occluded; otherwise the window is occluded
   *   when the opaque windows above it in the Z order, clipped to the screens,
   *   cover it entirely. Click-through and translucent layered windows do not
   *   count as covering.
   * - Linux: ⚠️ Partial - Hidden and minimized windows are occluded; whether
   *   other windows cover a shown one is not known, so it is Unknown (X11
   *   visibility is meaningless under a compositing manager, and Wayland
   *   does not tell).
   * - Android: ❌ Not applicable - Always Unknown
   * - iOS: ❌ Not applicable - Always Unknown
   * - OpenHarmony: ✅ Supported on an initialized Window.SessionManager host.
   *   Public SDK occlusionStateChanged (or windowVisibilityChange fallback)
   *   observations distinguish a visible portion from full occlusion. Before the
   *   first observation a shown window is Unknown; hidden windows are Occluded.
   *   Partial/absent occlusion both mean Visible. SDK rules for transparent,
   *   animated and masked windows apply; no screen-geometry approximation is used.
   *   Closed/foreign windows return Unknown. Workers marshal queries to UI.
   */
  WindowOcclusionState GetOcclusionState() const;

  /**
   * @brief Whether GetOcclusionState() can report anything but Unknown here.
   * @see GetOcclusionState() for platform availability.
   */
  static bool IsOcclusionStateSupported();
  // === Window State Management ===

  /**
   * @brief Maximizes the window to fill the available screen space.
   *
   * Expands the window to occupy the maximum available area on the screen,
   * typically excluding taskbars and docks. A window that is not shown yet
   * stays hidden and appears maximized when it is shown, also when its host
   * shows it as a restored window (a Flutter runner on its first frame);
   * IsMaximized() already reports true, and Unmaximize() takes it back.
   */
  void Maximize();

  /**
   * @brief Sets whether the window fills its monitor's work area vertically.
   * @param is_maximized_vertically true to stretch vertically, false to restore.
   * @return true if applied; false if unsupported, invalid, minimized, fully
   *         maximized, full screen, docked, or rejected by the window system.
   *
   * Preserves horizontal position and width. Disabling restores the vertical
   * position and height saved by the first successful enable. Native resizing
   * away from that geometry ends this state. The state belongs to the native
   * window and is shared by wrappers. This is not full maximization and emits
   * ordinary move/resize events, not WindowMaximizedEvent.
   *
   * @note Platform availability:
   * - macOS: ❌ Not supported - Returns false
   * - Windows: ✅ Supported - Uses the nearest monitor's work area
   * - Linux: ❌ Not supported - Returns false
   * - Android: ❌ Not applicable - Returns false
   * - iOS: ❌ Not applicable - Returns false
   * - OpenHarmony: ❌ Not supported - Returns false
   */
  bool SetMaximizedVertically(bool is_maximized_vertically);

  /**
   * @brief Checks if this window is vertically maximized by this library.
   * @return true while the applied geometry remains; false otherwise.
   * @see SetMaximizedVertically() for platform availability.
   */
  bool IsMaximizedVertically() const;

  /**
   * @brief Checks if vertical maximization is available on this platform.
   * @return true on Windows, false elsewhere.
   */
  static bool IsVerticalMaximizeSupported();

  /**
   * @brief Docks the window as a desktop toolbar, reserving screen space.
   * @param side Left or Right; None is invalid (use Undock()).
   * @param width Desired outer width in logical pixels, finite and positive.
   * @return true if registered and positioned; false if unsupported, invalid,
   *         minimized, maximized, full screen, too wide, or rejected by the shell.
   *
   * Uses the nearest monitor and the space left by other desktop toolbars.
   * Repeated calls update the same reservation. Changes to other toolbars or
   * display geometry reposition it. Undock() restores the original bounds;
   * native destruction releases the reservation. All wrappers share the state.
   * Successful changes emit WindowDockedEvent; Undock() emits WindowUndockedEvent.
   * This is a desktop toolbar reservation, not Windows Snap Layouts.
   *
   * @note Platform availability:
   * - macOS: ❌ Not supported - Returns false
   * - Windows: ✅ Supported - Shell application desktop toolbar (appbar)
   * - Linux: ❌ Not supported - Returns false
   * - Android: ❌ Not applicable - Returns false
   * - iOS: ❌ Not applicable - Returns false
   * - OpenHarmony: ❌ Not supported - Returns false
   */
  bool Dock(WindowDockSide side, double width);

  /**
   * @brief Removes this window's desktop space reservation and restores its bounds.
   * @return true if undocked or already undocked on a supported platform;
   *         false if unsupported, invalid, or the shell rejects removal.
   * @see Dock() for platform availability.
   */
  bool Undock();

  /**
   * @brief Gets the screen edge this window reserves through Dock().
   * @return The reserved edge, or None when undocked, unsupported or invalid.
   * @see Dock() for platform availability.
   */
  WindowDockSide GetDockSide() const;

  /**
   * @brief Checks if desktop toolbar docking is available on this platform.
   * @return true on Windows with a running shell, false elsewhere.
   */
  static bool IsDockingSupported();


  /**
   * @brief Restores the window from maximized state to its previous size.
   *
   * Returns the window to the size and position it had before being maximized.
   */
  void Unmaximize();

  /**
   * @brief Checks if the window is currently maximized.
   *
   * @return true if the window is maximized, false otherwise
   */
  bool IsMaximized() const;

  /**
   * @brief Minimizes the window, hiding it from the desktop.
   *
   * Reduces the window to an icon in the taskbar or dock. The window
   * remains open but is not visible on the desktop.
   */
  void Minimize();

  /**
   * @brief Restores a minimized window: the opposite of Minimize().
   *
   * The window comes back in the state it was minimized from, maximized
   * included. A window that is not minimized is left as it is; Unmaximize()
   * is the opposite of Maximize().
   */
  void Restore();

  /**
   * @brief Checks if the window is currently minimized.
   *
   * @return true if the window is minimized, false otherwise
   */
  bool IsMinimized() const;

  /**
   * @brief Sets the window's fullscreen state.
   *
   * @param is_full_screen true to enter fullscreen mode, false to exit
   *
   * In fullscreen mode, the window occupies the entire screen with no
   * window decorations (title bar, borders) visible. The change is reported by
   * WindowEnteredFullScreenEvent and WindowExitedFullScreenEvent; on macOS and
   * Linux it completes asynchronously, so IsFullScreen() may still return the old
   * state right after this call.
   *
   * @note Platform availability:
   * - macOS: ✅ Native fullscreen, completed asynchronously.
   * - Windows: ✅ Borderless screen-sized window.
   * - Linux: ⚠️ Window-manager controlled; completion is asynchronous.
   * - Android: ❌ The current backend does not change fullscreen state.
   * - iOS: ⚠️ Sets the root controller's modal presentation style; it does not
   *   present or dismiss the controller.
   * - OpenHarmony: ⚠️ Supported freeform main/child windows use public SDK
   *   immersive maximize and recover, restoring a previous maximized state.
   *   The SDK may reject unsupported children or supported-mode restrictions.
   *   Outside a freeform workspace, content layout/system-bar requests retain
   *   their SDK restrictions. A phone child's SDK fullscreen flag can be true
   *   while its floating frame is unchanged. Calls submit asynchronously;
   *   queries and events report confirmed SDK state, not request submission.
   */
  void SetFullScreen(bool is_full_screen);

  /**
   * @brief Checks if the window is currently in fullscreen mode.
   *
   * @return true if the window is fullscreen, false otherwise
   * @see SetFullScreen() for platform availability and confirmed-state semantics.
   */
  bool IsFullScreen() const;
  // === Size and Bounds Management ===

  // void SetBackgroundColor(Color color);
  // Color GetBackgroundColor() const;

  /**
   * @brief Sets the window's position and size simultaneously.
   *
   * @param bounds Rectangle containing the desired position and size
   *
   * This method sets both the window's position and size in a single operation,
   * which can be more efficient than separate calls to SetPosition() and SetSize().
   */
  void SetBounds(Rectangle bounds);

  /**
   * @brief Animates the window to the supplied outer bounds.
   * @param bounds Outer bounds in logical screen coordinates, with a top-left
   *        origin and finite positive width and height.
   * @return true if the native animation was submitted; false if unsupported,
   *         invalid or full screen. False leaves the window unchanged.
   *
   * Completion is asynchronous. Observe move/resize events or GetBounds() for
   * the result. For an immediate fallback after false, call SetBounds().
   *
   * @note Platform availability:
   * - macOS: ✅ Supported - Uses the NSWindow frame animator
   * - Windows: ❌ Not supported - Returns false
   * - Linux: ❌ Not supported - Returns false
   * - Android: ❌ Not applicable - Returns false
   * - iOS: ❌ Not applicable - Returns false
   * - OpenHarmony: ❌ Not supported - Returns false
   */
  bool AnimateBounds(Rectangle bounds);

  /**
   * @brief Checks if native window frame animations are available.
   * @return true on macOS, false elsewhere.
   */
  static bool IsBoundsAnimationSupported();

  /**
   * @brief Gets the window's current position and size.
   *
   * @return Rectangle containing the current position and size of the window
   *
   * The returned rectangle includes the window frame and decorations.
   */
  Rectangle GetBounds() const;

  /**
   * @brief Sets the position and size of the window's content area.
   *
   * @param bounds Rectangle containing the desired position and size of the content area
   *
   * This method sets both the content area's position and size in a single operation,
   * which can be more efficient than separate calls to SetPosition() and SetContentSize().
   * The content area excludes window decorations like title bar and borders.
   */
  void SetContentBounds(Rectangle bounds);

  /**
   * @brief Gets the position and size of the window's content area.
   *
   * @return Rectangle containing the current position and size of the content area
   *
   * The returned rectangle excludes window decorations and represents the drawable
   * content area of the window.
   */
  Rectangle GetContentBounds() const;

  /**
   * @brief Sets the window's size with optional animation.
   *
   * @param size The new size for the window
   * @param animate Whether to animate the size change
   *
   * Changes the window's outer size including frame and decorations.
   * If animate is true, the resize will be smoothly animated on supported platforms.
   */
  void SetSize(Size size, bool animate);

  /**
   * @brief Gets the window's current outer size.
   *
   * @return Size The current size of the window including frame and decorations
   */
  Size GetSize() const;

  /**
   * @brief Sets the size of the window's content area.
   *
   * @param size The desired size of the content area
   *
   * This sets the size of the drawable content area, excluding window
   * decorations like title bar and borders. The actual window size will
   * be larger to accommodate the frame.
   */
  void SetContentSize(Size size);

  /**
   * @brief Gets the size of the window's content area.
   *
   * @return Size The current size of the content area excluding decorations
   */
  Size GetContentSize() const;

  /**
   * @brief Sets the minimum size the window can be resized to.
   *
   * @param size The minimum allowed size
   *
   * Prevents the user from resizing the window smaller than the specified size.
   * This applies to the outer window size including decorations.
   */
  void SetMinimumSize(Size size);

  /**
   * @brief Gets the current minimum size constraint.
   *
   * @return Size The minimum size the window can be resized to
   */
  Size GetMinimumSize() const;

  /**
   * @brief Sets the maximum size the window can be resized to.
   *
   * @param size The maximum allowed size
   *
   * Prevents the user from resizing the window larger than the specified size.
   * This applies to the outer window size including decorations.
   */
  void SetMaximumSize(Size size);

  /**
   * @brief Gets the current maximum size constraint.
   *
   * @return Size The maximum size the window can be resized to
   */
  Size GetMaximumSize() const;

  /**
   * @brief Constrains user-driven resizing to a fixed width/height ratio.
   *
   * @param aspect_ratio Desired width divided by height, e.g. 16.0 / 9.0.
   *        Values of 0 or less remove the constraint.
   *
   * The ratio applies to the content area (the title bar and borders are not
   * counted) while the user drags a window edge. It does not change the current
   * size and is not enforced by SetSize(), SetContentSize() or SetBounds().
   * Minimum and maximum sizes still apply on top of the ratio.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported
   * - Windows: ✅ Fully supported
   * - Linux: ⚠️ Partial - Applied via GDK aspect geometry hints, which the
   *   window manager may honor loosely. GTK also applies them to programmatic
   *   resizes, so SetSize(), SetContentSize() and SetBounds() are adjusted to
   *   the ratio while one is set. With client-side decorations the content
   *   ratio can be off by a pixel after a resize.
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ⚠️ API 21+ uses a nonpersistent content-ratio constraint on
   *   supported main windows, effective during floating-window edge drags.
   *   SDK rejection leaves the last successful constraint unchanged. UI calls
   *   submit without blocking; workers wait for SDK completion. Nonfinite inputs
   *   are ignored. Programmatic resizing remains unconstrained.
   *   Clearing uses resetAspectRatio(), which also clears a saved SDK ratio.
   */
  void SetAspectRatio(double aspect_ratio);

  /**
   * @brief Gets the aspect ratio constraint set by SetAspectRatio().
   *
   * On OpenHarmony this is the last successfully applied nativeapi constraint,
   * shared by aliases of the same live window. The SDK exposes no ratio getter;
   * changes made directly by the ArkTS host are not reflected here.
   *
   * @return Width divided by height, or 0 when no constraint is set.
   */
  double GetAspectRatio() const;
  // === Window Behavior Properties ===

  /**
   * @brief Sets whether the window can be resized by the user.
   *
   * @param is_resizable true to allow resizing, false to disable
   *
   * When disabled, the user cannot resize the window by dragging its edges
   * or corners. Programmatic resizing via SetSize() is still possible.
   */
  void SetResizable(bool is_resizable);

  /**
   * @brief Checks if the window can be resized by the user.
   *
   * @return true if user can resize the window, false otherwise
   */
  bool IsResizable() const;

  /**
   * @brief Sets whether the window can be moved by the user.
   *
   * @param is_movable true to allow moving, false to disable
   *
   * When disabled, the user cannot move the window by dragging its title bar.
   * Programmatic positioning via SetPosition() is still possible.
   *
   * With TitleBarStyle::Hidden the system does not move the window on its
   * own regardless; this setting is kept and applies again once the title
   * bar is shown.
   */
  void SetMovable(bool is_movable);

  /**
   * @brief Checks if the window can be moved by the user.
   *
   * @return true if user can move the window, false otherwise
   */
  bool IsMovable() const;

  /**
   * @brief Sets whether the window can be minimized by the user.
   *
   * @param is_minimizable true to allow minimizing, false to disable
   *
   * Controls the availability of minimize functionality in the window's
   * title bar and system menu. Programmatic minimizing is still possible.
   *
   * @note Platform availability:
   * - macOS: ✅ Native caption policy
   * - Windows: ✅ Native caption policy
   * - Linux: ⚠️ GTK controls and window-manager policy hints
   * - Android: ❌ Setter ignored
   * - iOS: ❌ Setter ignored
   * - OpenHarmony: ⚠️ Public SDK caption buttons on supported decorated windows.
   *   These are caption-only policies; other system gestures are unaffected.
   *   Independent policies survive collective button hiding and title-bar style
   *   changes; programmatic Minimize/Maximize remain available, while Close()
   *   retains its documented child-only restriction. SDK refusal preserves the
   *   last applied policies. Desktop main windows start with enabled policies;
   *   other window profiles return false until a button request succeeds. Native
   *   destruction returns false. There is no individual SDK getter; external
   *   button edits do not rewrite these independent policies.
   */
  void SetMinimizable(bool is_minimizable);

  /**
   * @brief Checks if the window can be minimized by the user.
   *
   * @return true if user can minimize the window, false otherwise
   * @see SetMinimizable() for platform availability and policy semantics.
   */
  bool IsMinimizable() const;

  /**
   * @brief Sets whether the window can be maximized by the user.
   *
   * @param is_maximizable true to allow maximizing, false to disable
   *
   * Controls the availability of maximize functionality in the window's
   * title bar and system menu. Programmatic maximizing is still possible.
   *
   * @note Platform availability:
   * - macOS: ✅ Native caption policy
   * - Windows: ✅ Native caption policy
   * - Linux: ⚠️ GTK controls and window-manager policy hints
   * - Android: ❌ Setter ignored
   * - iOS: ❌ Setter ignored
   * - OpenHarmony: ⚠️ Public SDK caption buttons on supported decorated windows.
   *   These are caption-only policies; native title-bar double-click
   *   still maximizes even when the maximize button is hidden.
   *   Independent policies survive collective button hiding and title-bar style
   *   changes; programmatic Minimize/Maximize remain available, while Close()
   *   retains its documented child-only restriction. SDK refusal preserves the
   *   last applied policies. Desktop main windows start with enabled policies;
   *   other window profiles return false until a button request succeeds. Native
   *   destruction returns false. There is no individual SDK getter; external
   *   button edits do not rewrite these independent policies.
   */
  void SetMaximizable(bool is_maximizable);

  /**
   * @brief Checks if the window can be maximized by the user.
   *
   * @return true if user can maximize the window, false otherwise
   * @see SetMaximizable() for platform availability and policy semantics.
   */
  bool IsMaximizable() const;

  /**
   * @brief Sets whether the window can enter fullscreen mode.
   *
   * @param is_full_screenable true to allow fullscreen, false to disable
   *
   * Controls whether the window supports fullscreen mode. On some platforms,
   * this affects the availability of fullscreen controls in the UI.
   *
   * @note Platform availability:
   * - macOS: ❌ The current setter is not implemented.
   * - Windows: ❌ Setter ignored; live native windows can enter borderless fullscreen.
   * - Linux: ❌ The current setter is not implemented.
   * - Android: ❌ Setter ignored.
   * - iOS: ❌ Setter ignored.
   * - OpenHarmony: ⚠️ Changes the public supported-mode mask (Window API 26+,
   *   or the primary WindowStage API 15+ fallback). The SDK couples fullscreen
   *   and nonimmersive maximize, so disabling this mode blocks both. Other mode
   *   bits are preserved. Main windows start from the attached Ability's declared
   *   modes; a floating child can establish its floating baseline. Hosts must
   *   publish known higher-priority StartOptions/direct-SDK overrides through the
   *   OHOS host seam, since there is no public live mask getter. Requests that
   *   would leave an invalid/empty mode set are ignored. UI requests are queued
   *   asynchronously; workers wait for their own SDK completion. SDK refusal
   *   preserves the preceding policy, and destruction cancels pending requests.
   *   This policy does not control phone content-layout/system-bar flags.
   */
  void SetFullScreenable(bool is_full_screenable);

  /**
   * @brief Checks if the window supports fullscreen mode.
   *
   * @return true if fullscreen is supported, false otherwise
   * @see SetFullScreenable() for platform policy semantics. On OpenHarmony the
   *   getter reports a host-published/successfully applied mask or the declared
   *   primary baseline; an unknown child baseline and destroyed windows return
   *   false. Unreported host SDK changes are not reflected.
   */
  bool IsFullScreenable() const;

  /**
   * @brief Sets whether the window can be closed by the user.
   *
   * @param is_closable true to allow closing, false to disable
   *
   * When disabled, the close button in the title bar is hidden or disabled.
   * The window can still be closed programmatically.
   *
   * @note Platform availability:
   * - macOS: ✅ Native caption policy
   * - Windows: ✅ Native caption policy
   * - Linux: ⚠️ GTK controls and window-manager policy hints
   * - Android: ❌ Setter ignored
   * - iOS: ❌ Setter ignored
   * - OpenHarmony: ⚠️ Public SDK caption buttons on supported decorated windows.
   *   These are caption-only policies; other system gestures are unaffected.
   *   Independent policies survive collective button hiding and title-bar style
   *   changes; programmatic Minimize/Maximize remain available, while Close()
   *   retains its documented child-only restriction. SDK refusal preserves the
   *   last applied policies. Desktop main windows start with enabled policies;
   *   other window profiles return false until a button request succeeds. Native
   *   destruction returns false. There is no individual SDK getter; external
   *   button edits do not rewrite these independent policies.
   */
  void SetClosable(bool is_closable);

  /**
   * @brief Checks if the window can be closed by the user.
   *
   * @return true if user can close the window, false otherwise
   * @see SetClosable() for platform availability and policy semantics.
   */
  bool IsClosable() const;

  /**
   * @brief Sets the visibility of window control buttons.
   *
   * @param is_visible true to show window control buttons, false to hide them
   *
   * Controls the visibility of window control buttons (minimize, maximize, close)
   * in the title bar. When hidden, the buttons are not visible but the window
   * can still be controlled programmatically.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Hides/shows the traffic light buttons (red, yellow, green)
   * - Windows: ❌ Not implemented - Returns default value (visible)
   * - Linux: ❌ Not implemented - Returns default value (visible)
   * - Android: ❌ Not applicable - Mobile apps don't have window control buttons
   * - iOS: ❌ Not applicable - Mobile apps don't have window control buttons
   * - OpenHarmony: ⚠️ Uses public SDK title-button visibility on supported decorated
   *   windows (normally a freeform main window). Unsupported phone/child requests
   *   are ignored. The getter reads the live SDK button-area size,
   *   so an externally configured nonempty subset also counts as visible. Layout
   *   updates and full-screen hover behavior are managed by the system.
   */
  void SetWindowControlButtonsVisible(bool is_visible);

  /**
   * @brief Checks if the window control buttons are visible.
   *
   * @return true if window control buttons are visible, false if hidden
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Returns actual visibility state
   * - Windows: ❌ Not implemented - Always returns true
   * - Linux: ❌ Not implemented - Always returns true
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ⚠️ Reads the live SDK title-button area, including controls
   *   explicitly shown over a hidden title bar. False for unavailable/closed
   *   windows, empty areas or SDK errors.
   *   @see SetWindowControlButtonsVisible() for details.
   */
  bool IsWindowControlButtonsVisible() const;

  /**
   * @brief Sets whether the window stays on top of other windows.
   *
   * @param is_always_on_top true to keep on top, false for normal behavior
   *
   * When enabled, the window will remain visible above other windows
   * even when it doesn't have focus.
   */
  void SetAlwaysOnTop(bool is_always_on_top);

  /**
   * @brief Checks if the window is set to always stay on top.
   *
   * @return true if window stays on top, false otherwise
   */
  bool IsAlwaysOnTop() const;

  /**
   * @brief Sets whether the window stays beneath all other normal windows.
   *
   * @param is_always_on_bottom true to keep the window at the bottom of the
   *        stacking order, false for normal behavior
   *
   * When enabled the window stays behind every other application window,
   * even while it has focus, but remains above the desktop. Use this for
   * desktop widgets or wallpaper-like windows. Enabling this clears any
   * SetAlwaysOnTop() setting and vice versa.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - The window level is lowered below the normal
   *   window level.
   * - Windows: ✅ Fully supported - The window is pinned to the bottom of the
   *   Z order and stays there when activated.
   * - Linux: ✅ Fully supported - Uses the _NET_WM_STATE_BELOW hint; honored by
   *   most window managers.
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ❌ Not applicable - Always ignored
   */
  void SetAlwaysOnBottom(bool is_always_on_bottom);

  /**
   * @brief Checks if the window is set to always stay at the bottom.
   *
   * @return true if the window stays beneath other windows, false otherwise
   */
  bool IsAlwaysOnBottom() const;

  /**
   * @brief Sets the window this window belongs to, making it a child window.
   *
   * A child window always stays above its parent and is hidden while the parent
   * is minimized. Tool palettes, floating toolbars and inspectors are child
   * windows. The relationship does not keep either window alive, and a window
   * has at most one parent.
   *
   * What else follows from the relationship is decided by the platform, see
   * below. For behaviour that must be the same everywhere — a child that
   * follows its parent — listen to the parent's WindowMovedEvent and
   * WindowResizedEvent; closing the children before their parent avoids the
   * difference in what closing the parent does to them.
   *
   * @param parent The new parent window, or nullptr to make this window
   *        independent again
   * @return false if the relationship was not established: parent is this
   *         window or one of its descendants, either native window is gone, or
   *         the platform has no child windows
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - The child also moves with its parent. A hidden
   *   child is attached when it is shown, because AppKit shows a window that is
   *   attached to a visible parent.
   * - Windows: ⚠️ Owned window - Stays above its parent and is hidden with it,
   *   but does not move with it, and is destroyed when its parent is.
   * - Linux: ⚠️ Transient window - Stays above its parent; does not move with
   *   it, and minimizing with the parent is up to the window manager. On Wayland
   *   nothing an application does can make it follow: a client neither places
   *   its toplevels nor learns where they are.
   * - Android: ❌ Not applicable - Always ignored, returns false
   * - iOS: ❌ Not applicable - Always ignored, returns false
   * - OpenHarmony: ⚠️ API 19+ public SDK reparenting for attached application
   *   subwindows and parents in the same UI environment, subject to device/freeform
   *   window support. nullptr, main-window receivers and overlapping requests are
   *   rejected. UI callers receive submission status; workers wait for SDK completion.
   */
  bool SetParentWindow(std::shared_ptr<Window> parent);

  /**
   * @brief Gets the window this window belongs to.
   *
   * Read from the native window, so it also reports a parent the embedding
   * framework has set. On OpenHarmony only attached parents can be returned;
   * API 19+ reads the live SDK relationship, while older SDKs use the creation
   * relationship. SDK query failure returns nullptr.
   *
   * @return The parent window, or nullptr if this window has none
   * @see SetParentWindow() for platform availability.
   */
  std::shared_ptr<Window> GetParentWindow() const;

  /**
   * @brief Sets whether showing or focusing the window activates the application.
   *
   * @param is_non_activating true to make the window non-activating, false for
   *        normal behavior
   *
   * A non-activating window can be shown and clicked without activating its
   * application. On macOS it can also receive keyboard input. The
   * previously active application keeps its activation state, and hiding the
   * window does not bring the application's other windows forward. Use this
   * for floating helper windows (quick-input palettes, pop-up translators,
   * pickers) that should sit above a foreign app while the user keeps working
   * in it.
   *
   * The window level is not changed by this call; combine it with
   * SetAlwaysOnTop() to keep the window above other applications' windows.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - The window becomes a non-activating NSPanel
   *   that can become key but never main.
   * - Windows: ✅ Supported - Uses WS_EX_NOACTIVATE and declines mouse activation;
   *   Show() and Focus() do not take keyboard focus while this flag is set.
   * - Linux: ⚠️ Window-manager dependent - Sets GTK/GDK accept-focus and
   *   focus-on-map hints; Show() and Focus() do not request activation. X11
   *   window managers normally honor these hints; Wayland compositors may ignore them.
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ✅ Show and Focus honor the non-activating policy for attached windows.
   */
  void SetNonActivating(bool is_non_activating);

  /**
   * @brief Checks if the window is non-activating.
   *
   * @return true if the non-activating policy is enabled, false otherwise.
   *         See SetNonActivating() for window-manager restrictions.
   *
   * @see SetNonActivating() for platform availability.
   */
  bool IsNonActivating() const;

  // === Position and Title ===

  /**
   * @brief Sets the window's position on the screen.
   *
   * @param point The new position for the window's top-left corner
   *
   * Coordinates are relative to the screen's origin (typically top-left).
   */
  void SetPosition(Point point);

  /**
   * @brief Gets the window's current position on the screen.
   *
   * @return Point The position of the window's top-left corner
   */
  Point GetPosition() const;

  /**
   * @brief Centers the window on the screen.
   *
   * Moves the window to the center of the primary display. The window
   * will be positioned so that its center point aligns with the center
   * of the screen.
   */
  void Center();

  /**
   * @brief Sets the text displayed in the window's title bar.
   *
   * @param title The new title text for the window
   */
  void SetTitle(std::string title);

  /**
   * @brief Gets the current title text of the window.
   *
   * @return std::string The current title displayed in the title bar
   */
  std::string GetTitle() const;

  /** Customize caption and caption-button colors. Windows WinUI3 backend only.
   * Operates on the existing window; does not replace the host's content.
   * Returns false when unsupported or the native window has been destroyed.
   */
  bool SetTitleBarColors(const Color& background, const Color& foreground);
  bool ResetTitleBarColors();

  /**
   * @brief Sets the style of the window's title bar.
   *
   * TitleBarStyle::Hidden leaves the window without a title bar and without
   * window control buttons; the application draws its own chrome and moves the
   * window with StartDragging(). It also resets the button visibility to what
   * the style implies, so a SetWindowControlButtonsVisible() that is meant to
   * override that goes after this call.
   *
   * @param style The desired title bar style
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported
   * - Windows: ✅ Fully supported
   * - Linux: ✅ Fully supported - Hides the window's header bar
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ⚠️ Changes native decorations where the device and window type support them.
   */
  void SetTitleBarStyle(TitleBarStyle style);

  /**
   * @brief Gets the current title bar style of the window.
   *
   * @return TitleBarStyle The current title bar style
   *
   * @see SetTitleBarStyle() for platform availability.
   */
  TitleBarStyle GetTitleBarStyle() const;

  /**
   * @brief Sets the preferred style of the compositor's window corners.
   * @param preference The desired corner style; Default restores system policy.
   * @return true if the native compositor accepted the preference; false if
   *         unsupported, invalid, or the native window no longer exists. A
   *         failed call leaves the previous preference unchanged.
   *
   * This leaves the title bar, border, shadow and content geometry in place.
   * It is a preference, not a guarantee of a particular radius: Windows does
   * not round maximized or snapped windows, and may ignore rounding for a
   * custom region, per-pixel transparency or a frame it does not draw.
   *
   * @note Platform availability:
   * - macOS: ❌ Not supported - Always returns false
   * - Windows: ✅ Supported on Windows 11 build 22000 and later - DWM corner preference
   * - Linux: ❌ Not supported - Always returns false
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ⚠️ Child windows use public SDK corner radii: square 0,
   *   RoundSmall 4 vp and Round 8 vp. Default restores the radius read before
   *   this library's first request; it does not re-enter changing system policy.
   *   Main windows and invalid/dead wrappers return false. UI calls report
   *   submission; workers wait for SDK success. The shared preference record
   *   changes only on successful completion. An overlapping request returns false.
   */
  bool SetCornerPreference(WindowCornerPreference preference);

  /**
   * @brief Gets the last corner preference successfully applied by this library.
   * @return The shared native window's preference, or Default when unset,
   *         unsupported or the native window no longer exists.
   *
   * This reads the library's successful preference record on the native window,
   * not a live radius or changes made directly through another native API.
   * @see SetCornerPreference() for platform availability.
   */
  WindowCornerPreference GetCornerPreference() const;

  /**
   * @brief Checks if this platform supports window corner preferences.
   * @return true on supported Windows versions or an initialized OpenHarmony
   *         Window.SessionManager host; false elsewhere.
   * @see SetCornerPreference() for platform availability.
   */
  static bool IsCornerPreferenceSupported();

  /**
   * @brief Lets the content area take in the title bar, which becomes a
   *        transparent overlay above it.
   *
   * The title bar keeps its window control buttons and its height, but stops
   * drawing a background of its own, and the content reaches the top edge of
   * the window behind it. Use it for a window whose background - a colour or a
   * visual effect - should run unbroken to the top edge while the system
   * buttons stay. An application that wants no buttons and no title bar at all
   * wants TitleBarStyle::Hidden instead.
   *
   * The window's frame (position and outer size) is kept; the content area
   * grows or shrinks by the title bar instead. With TitleBarStyle::Hidden the
   * flag is recorded but changes nothing, the content already covering the
   * window.
   *
   * @param is_content_under_title_bar true to take the title bar into the
   *        content area, false to give it back
   * @return true if the window is now in that state, false where the platform
   *         cannot do it; the window is then left as it was
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - NSWindowStyleMaskFullSizeContentView with a
   *   transparent title bar; the traffic lights stay over the content.
   * - Windows: ❌ Not supported - Always returns false. The client area can be
   *   given the caption band, but the caption buttons DWM draws there stop
   *   hit-testing (they answer HTCLIENT), so they would have to be drawn and
   *   hit-tested by hand. Use TitleBarStyle::Hidden and draw the chrome.
   * - Linux: ❌ Not supported - Always returns false. A GTK header bar is a
   *   sibling above the content, not an overlay over it.
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ⚠️ Uses public immersive content layout, including phone
   *   system-bar insets. UI calls report asynchronous submission; workers wait
   *   for SDK completion and confirm the actual layout flag. SDK no-op success
   *   is false on a worker when the live flag does not match the request. A pending
   *   layout request, invalid/destroyed window or SDK invocation failure returns
   *   false. Observe
   *   IsContentUnderTitleBar() for the actual layout; this does not change the
   *   native window mode or frame. On the tested API 26 computer, floating-main
   *   requests leave the live layout flag unchanged; child flags can change
   *   while the decorated content inset remains. This is not a transparent native-caption overlay.
   *   Native destruction cancels pending requests, and late completions do not
   *   publish fullscreen events.
   */
  bool SetContentUnderTitleBar(bool is_content_under_title_bar);

  /**
   * @brief Tells whether the content area has taken in the title bar.
   *
   * @return true if the title bar is an overlay above the content; false where
   *         there are no such title bars
   *
   * @see SetContentUnderTitleBar() for platform availability.
   */
  bool IsContentUnderTitleBar() const;

  /**
   * @brief Tells whether SetContentUnderTitleBar() can do anything here.
   *
   * @return true if this platform has a title bar the content can take in
   *
   * @see SetContentUnderTitleBar() for platform availability.
   */
  static bool IsContentUnderTitleBarSupported();

  /**
   * @brief Sets the native window's screen capture protection policy.
   * @param is_content_protected true to request protection, false to remove it
   * @return true if the native policy was applied; false when unsupported, the
   *         window is invalid, or the operating system rejects the request.
   *
   * This is a capture API policy, not a security boundary. Capture software may
   * ignore it. IsContentProtected() reads the policy, not whether a particular
   * screen recorder honours it. Other wrappers of this window share the policy.
   *
   * @note Platform availability:
   * - macOS: ⚠️ Sets NSWindowSharingNone, or NSWindowSharingReadOnly when disabled.
   *   Recent ScreenCaptureKit capture ignores this legacy sharing policy.
   * - Windows: ✅ Uses WDA_EXCLUDEFROMCAPTURE on Windows 10 version 2004 and later;
   *   older systems use WDA_MONITOR (blank content). Requires DWM composition
   *   and a top-level window belonging to this process.
   * - Linux: ❌ Not supported - Always returns false
   * - Android: ❌ Not supported - Always returns false
   * - iOS: ❌ Not supported - Always returns false
   * - OpenHarmony: ⚠️ Native privacy mode requires ohos.permission.PRIVACY_WINDOW; false on denial.
   */
  bool SetContentProtection(bool is_content_protected);

  /**
   * @brief Checks if the native window's capture protection policy is enabled.
   * @return true if enabled; false when unset, unsupported, or unavailable.
   * @see SetContentProtection() for platform availability and capture limitations.
   */
  bool IsContentProtected() const;

  /**
   * @brief Checks if this platform exposes a native capture protection policy.
   * @return true on macOS, or Windows with DWM composition enabled; false elsewhere.
   *         A supported platform may still reject a particular window.
   * @see SetContentProtection() for capture limitations.
   */
  static bool IsContentProtectionSupported();

  // === Appearance and Advanced Behavior ===

  /**
   * @brief Sets whether the window displays a shadow.
   *
   * @param has_shadow true to show shadow, false to hide it
   *
   * Controls the drop shadow effect around the window. On some platforms,
   * this may affect window compositing and visual effects.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Drops the shadow of any window
   * - Windows: ⚠️ Hidden-title-bar windows keep the desktop compositor's shadow, or use a
   *   core-managed click-through shadow once a custom shadow or a shape is applied.
   *   Without a shadow a hidden-title-bar window has no frame; its content stays in
   *   place. System-decorated windows retain the desktop compositor's restrictions.
   * - Linux: ⚠️ Hidden-title-bar GtkWindows use a core-rendered shadow with an
   *   internal non-interactive margin; content coordinates and sizes exclude it.
   *   SetInputShape() supplies its contour, or nullptr restores a rectangle.
   *   Native X11 visual regions still clip the shadow; use independent input
   *   shaping and transparent renderer clipping for a soft exterior shadow.
   *   Decorated GTK windows use their theme shadow; server-side X11 decorations
   *   remain controlled by the window manager.
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ⚠️ Child windows use public setWindowShadowRadius: zero
   *   disables, 18 logical pixels enables. Main windows use the permission-gated
   *   setWindowShadowEnabled SDK, requiring the normal system_grant permission
   *   ohos.permission.SET_WINDOW_TRANSPARENT on PCs/tablets; rejection preserves
   *   the last applied record.
   *   HasShadow reports this library's last successful request (false until
   *   configured); the SDK has no public getter for existing shadow state.
   *   Native color/offset/blur defaults remain system-controlled.
   */
  void SetHasShadow(bool has_shadow);

  /**
   * @brief Checks if the window currently displays a shadow.
   *
   * @return true if shadow is enabled, false otherwise
   *
   * @see SetHasShadow() for platform availability.
   */
  bool HasShadow() const;

  /**
   * @brief Applies a copied custom shadow, or restores the default with nullptr.
   * @return False if the window or its decoration mode does not support custom shadows.
   *
   * SetHasShadow() remains the visibility switch and preserves this configuration.
   * Custom shadows are rendered by core and do not receive pointer input.
   * Content dimensions and shape coordinates exclude the shadow's internal margin.
   *
   * @note Platform availability:
   * - macOS: ✅ Hidden-title-bar windows; core draws a non-interactive child window
   * - Windows: ✅ Hidden-title-bar windows; core draws a layered helper window
   * - Linux: ✅ Hidden-title-bar GtkWindows; core draws in the same surface
   * - Android: ❌ Unsupported, returns false
   * - iOS: ❌ Unsupported, returns false
   * - OpenHarmony: ⚠️ Attached hidden-title-bar windows with the optional
   *   NativeWindowShadowHost Stage companion and a registered transparent surface
   *   page. Core rasterizes the clipped contour on a worker; a private public-SDK
   *   helper is non-focusable and passes all pointer input through. UI calls
   *   report submission; workers wait for SDK completion. Overlapping configuration
   *   changes return false; visibility changes are serialized. Main-window native
   *   shadow suppression also needs SET_WINDOW_TRANSPARENT on supported devices.
   */
  bool SetCustomShadow(std::shared_ptr<WindowShadow> shadow);

  /** @brief Returns an independent copy of the custom shadow, or nullptr for default. */
  std::shared_ptr<WindowShadow> GetCustomShadow() const;

  /**
   * @brief Sets the window's opacity (transparency level).
   *
   * @param opacity Opacity value between 0.0 (fully transparent) and 1.0 (fully opaque)
   *
   * Controls the transparency of the entire window including its content.
   * Values outside the 0.0-1.0 range will be clamped to valid values.
   */
  void SetOpacity(float opacity);

  /**
   * @brief Gets the window's current opacity level.
   *
   * @return float Current opacity value between 0.0 and 1.0
   */
  float GetOpacity() const;

  /**
   * @brief Replaces the window's background with a translucent material.
   *
   * While an effect is active it stands in for the background color: the color
   * set with SetBackgroundColor() is kept but not shown, and comes back with
   * VisualEffect::None. The material only shows where the window's content
   * leaves the background unpainted. A Flutter view is made to do so for as
   * long as the effect is active; what Flutter itself paints on top (an opaque
   * Scaffold, for one) is the application's to make transparent.
   *
   * @param effect The material to use, or VisualEffect::None to remove it
   * @return true if the effect is now in force, false if the platform or this
   *         version of it does not have it; the previous effect then stays
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - An NSVisualEffectView behind the content view,
   *   blending with what is behind the window. It stays active while the window
   *   is in the background.
   * - Windows: ⚠️ Depends on the version - Blur needs Windows 10, Acrylic
   *   Windows 10 1803, Mica and MicaAlt Windows 11 22H2. Windows 11 22H2 draws
   *   every effect as a system backdrop, which covers the title bar as well;
   *   before that they are blur-behind kinds, which reach the client area only and
   *   make the window lag while it is dragged. A system backdrop falls back to a
   *   solid color while the window is inactive; that is the system's doing.
   * - Linux: ❌ Not supported - Always returns false. Blur behind a window is up
   *   to the compositor there, and GNOME has none.
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ❌ Not applicable - Always returns false
   */
  bool SetVisualEffect(VisualEffect effect);

  /**
   * @brief Gets the visual effect that is in force on the window.
   *
   * This is the last effect SetVisualEffect() returned true for. It belongs to
   * the native window, not to this object.
   *
   * @return VisualEffect The active effect; VisualEffect::None where there are
   *         no visual effects
   *
   * @see SetVisualEffect() for platform availability.
   */
  VisualEffect GetVisualEffect() const;

  /**
   * @brief Tells whether SetVisualEffect() can apply an effect here.
   *
   * @param effect The effect to ask about
   * @return true if this platform, in the version that is running, has the
   *         effect. VisualEffect::None is supported everywhere.
   *
   * @see SetVisualEffect() for platform availability.
   */
  static bool IsVisualEffectSupported(VisualEffect effect);

  /**
   * @brief Sets a polygonal visible region, or restores the rectangle with nullptr.
   * @param shape Polygon in content-local logical pixels; at least three vertices.
   * @return False if unsupported, invalid, or the native operation fails.
   *
   * Hide the title bar before applying a shape. Points are copied when applied;
   * editing the builder does not change the window. Reapply after resizing or a
   * display scale change. A shape does not change the window's rectangular bounds.
   * The caller supplies its own drag and close controls. Clear before restoring
   * decorations. On macOS use a transparent background and no visual effect;
   * transparent pixels participate in AppKit's normal alpha-based hit testing.
   *
   * @note Platform availability:
   * - macOS: ⚠️ Content-layer mask; requires a transparent background and hidden title bar
   * - Windows: ✅ Native window region clips rendering and mouse input; the window drops
   *   its frame, keeping its content in place
   * - Linux: ⚠️ GDK visual and input regions, only on backends supporting both (X11)
   * - Android: ❌ Unsupported, returns false
   * - iOS: ❌ Unsupported, returns false
   * - OpenHarmony: ⚠️ API 24+ child-window compositor mask with hidden decorations.
   *   Maximum 16 Mi physical pixels with the API 26 alpha-mask method, or 1 Mi with
   *   the API 24/25 nested-array method; each dimension is at most 16384 pixels.
   *   Main windows are rejected. UI calls report submission; worker calls wait for
   *   SDK completion. IsShaped() records the last successful library request;
   *   external SDK mask changes are not tracked. A failed request keeps that state.
   *   Clearing a window with no SDK mask may fail. Wait for the new rectangle
   *   after resizing before reapplying; a resize promise alone may precede it.
   */
  bool SetShape(std::shared_ptr<WindowShape> shape);

  /** @brief Checks if a window shape is active. @see SetShape() for availability. */
  bool IsShaped() const;

  /** @brief Checks if the current platform/backend supports SetShape(). */
  static bool IsShapeSupported();

  /**
   * @brief Sets a polygonal pointer/touch input region without clipping drawing.
   * @param shape Polygon in content-local logical pixels, with at least three
   *        vertices; nullptr restores the default input region.
   * @return False if unsupported, invalid, or the native window is unavailable.
   *
   * Hide the title bar first. Points are copied; reapply after resizing or display
   * scale changes. Outside the region, pointer/touch events go to windows beneath.
   * Keyboard focus is unchanged. The renderer must paint outside the polygon
   * transparent when using this to implement a shaped window on Wayland.
   * On Linux, SetShape() also replaces this input region; clearing either API
   * restores default input handling. SetInputShape() never clips content drawing;
   * when the core-managed shadow is enabled, it also updates the shadow contour.
   * The internal shadow margin remains non-interactive even after clearing.
   *
   * @note Platform availability:
   * - macOS: ❌ Unsupported, returns false; use SetShape() for a content mask
   * - Windows: ❌ Unsupported, returns false; use SetShape() for a native region
   * - Linux: ✅ GDK input regions, including X11 and Wayland
   * - Android: ❌ Unsupported, returns false
   * - iOS: ❌ Unsupported, returns false
   * - OpenHarmony: ❌ Unsupported for ordinary applications without restricted
   *   SET_WINDOW_TOUCH_AREAS permission (system_basic, PCs/API 26+). The SDK
   *   accepts at most ten rectangles, not a general independent polygon mask.
   */
  bool SetInputShape(std::shared_ptr<WindowShape> shape);

  /**
   * @brief Checks if an explicit input polygon was applied through this API or SetShape().
   * @return False if no polygon is active, unsupported, or the window is unavailable.
   * @see SetInputShape() for platform availability.
   */
  bool IsInputShaped() const;

  /** @brief Checks if the current platform/backend supports SetInputShape(). */
  static bool IsInputShapeSupported();

  /**
   * @brief Sets the background color of the window.
   *
   * Sets a solid color for the window background. This color will be visible
   * if the window content does not fully cover the window area. While a visual
   * effect is active the color is kept but not shown, see SetVisualEffect().
   *
   * @param color The background color to apply
   *
   * @note Platform behavior may vary:
   * - Windows: Sets the window background brush color. A color with alpha is
   *   drawn by the desktop compositor instead, behind whatever the window and
   *   its children leave transparent, which makes the window see-through (a
   *   Flutter view clears to transparent, so it needs nothing else).
   * - macOS: Sets the window backgroundColor property. A color with alpha also
   *   makes the window non-opaque, so that it really is see-through, and is
   *   handed to a content view controller that paints a backing of its own
   *   (a Flutter view is opaque black otherwise).
   * - Linux: Sets the window background color via GTK CSS, and hands the color
   *   to a Flutter view in the window, which paints an opaque black backing of
   *   its own otherwise. A color with alpha is see-through where the desktop
   *   composites windows (always on Wayland).
   */
  void SetBackgroundColor(const Color& color);

  /**
   * @brief Gets the current background color of the window.
   *
   * @return Color The current background color
   */
  Color GetBackgroundColor() const;

  /**
   * @brief Sets whether the window appears on all virtual desktops/workspaces.
   *
   * @param is_visible_on_all_workspaces true to appear on all workspaces, false for current only
   *
   * When enabled, the window will be visible regardless of which virtual
   * desktop or workspace the user switches to. Platform support may vary.
   */
  void SetVisibleOnAllWorkspaces(bool is_visible_on_all_workspaces);

  /**
   * @brief Checks if the window appears on all workspaces.
   *
   * @return true if visible on all workspaces, false if only on current workspace
   */
  bool IsVisibleOnAllWorkspaces() const;

  /**
   * @brief Sets whether this window may join another window's full-screen space.
   * @param is_visible_on_full_screen true to allow joining, false to disable.
   * @return true if applied; false if unsupported or the window is invalid.
   *
   * Independent of SetVisibleOnAllWorkspaces() and this window's own full-screen
   * state. Both setters preserve unrelated native collection-behavior flags.
   * On macOS, conflicting flags in the same collection-behavior group are
   * displaced while enabled and restored when disabled by any wrapper.
   *
   * @note Platform availability:
   * - macOS: ✅ Supported - NSWindowCollectionBehaviorFullScreenAuxiliary
   * - Windows: ❌ Not supported - Returns false
   * - Linux: ❌ Not supported - Returns false
   * - Android: ❌ Not applicable - Returns false
   * - iOS: ❌ Not applicable - Returns false
   * - OpenHarmony: ❌ Not supported - Returns false
   */
  bool SetVisibleOnFullScreen(bool is_visible_on_full_screen);

  /**
   * @brief Checks if this window may join full-screen spaces.
   * @return true if enabled; false if unset, unsupported or invalid.
   * @see SetVisibleOnFullScreen() for platform availability.
   */
  bool IsVisibleOnFullScreen() const;

  /**
   * @brief Checks if joining full-screen spaces is available.
   * @return true on macOS, false elsewhere.
   */
  static bool IsVisibleOnFullScreenSupported();

  /**
   * @brief Grabs the keyboard exclusively for this window until released or hidden.
   * @return true if acquired or already held by this window; false if unsupported,
   *         not viewable, invalid, or another client/window holds a grab.
   *
   * Grabs only the keyboard, leaving pointer input unchanged. The native window
   * owns the grab; wrappers share it. Hiding or destroying the native surface
   * releases it. UngrabKeyboard() never releases another window's grab.
   *
   * @note Platform availability:
   * - macOS: ❌ Not supported - Returns false
   * - Windows: ❌ Not supported - Returns false
   * - Linux: ⚠️ X11 only - Uses the GDK seat keyboard grab; Wayland returns false
   * - Android: ❌ Not applicable - Returns false
   * - iOS: ❌ Not applicable - Returns false
   * - OpenHarmony: ❌ Not supported - Returns false
   */
  bool GrabKeyboard();

  /**
   * @brief Releases the keyboard grab belonging to this native window.
   * @return true if released or already released on a supported platform;
   *         false if unsupported or invalid.
   * @see GrabKeyboard() for platform availability.
   */
  bool UngrabKeyboard();

  /**
   * @brief Checks if this native window still holds its keyboard grab.
   * @return true if held; false if released, hidden, destroyed or unsupported.
   * @see GrabKeyboard() for platform availability.
   */
  bool IsKeyboardGrabbed() const;

  /**
   * @brief Checks if the current display supports exclusive keyboard grabs.
   * @return true on Linux X11, false on Wayland and other platforms.
   */
  static bool IsKeyboardGrabSupported();


  /**
   * @brief Sets whether the window is listed in the taskbar.
   *
   * @param is_visible_in_taskbar true to list the window, false to hide it from the
   *        taskbar
   *
   * A window hidden from the taskbar keeps its own appearance and behavior; only the
   * shell's list of open windows drops it. Useful for overlays, tool palettes and
   * windows an app shows from its tray icon. The window stays reachable through
   * Alt+Tab on the platforms noted below.
   *
   * @note Platform availability:
   * - macOS: ⚠️ Window menu only - The Dock lists applications, not windows, so the
   *   window is only dropped from the application's Window menu
   * - Windows: ✅ Fully supported - Adds or removes the window's taskbar button
   * - Linux: ✅ Fully supported - Sets the window manager's skip-taskbar hint
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ❌ Not applicable - Always ignored
   */
  void SetVisibleInTaskbar(bool is_visible_in_taskbar);

  /**
   * @brief Checks if the window is listed in the taskbar.
   *
   * @return true if the window has a taskbar entry, false if it is hidden from it
   *
   * @see SetVisibleInTaskbar() for platform availability.
   */
  bool IsVisibleInTaskbar() const;

  /**
   * @brief Sets whether the window ignores mouse input events.
   *
   * @param is_ignore_mouse_events true to ignore mouse events, false to receive them
   * @param forward true to keep delivering mouse movement to this window's content
   *        while clicks and scrolling pass through. Ignored when disabling pass-through.
   * @return true if the policy was applied; false for an invalid window, an
   *         unsupported forward request, or native failure. Failure preserves the policy.
   *
   * When enabled, mouse events (clicks, hovers, etc.) pass through the window
   * to whatever is behind it. Useful for overlay or heads-up display windows.
   * Movement forwarding lets content detect hover over interactive regions and
   * turn pass-through off. Forwarded movement is sampled on the UI event loop;
   * it does not forward button or wheel events or activate the window.
   * Disabling pass-through also disables forwarding. The policy belongs to the
   * native window and is shared by wrappers, including after a wrapper is freed.
   *
   * @note Platform availability:
   * - macOS: ✅ Pass-through and movement forwarding
   * - Windows: ✅ Pass-through and movement forwarding to child content HWNDs
   * - Linux: ✅ Input-region pass-through; movement forwarding on X11 only.
   *   Wayland cannot report global pointer movement outside the input region.
   *   Disabling pass-through restores the latest explicit input shape.
   * - Android, iOS: ❌ Always returns false
   * - OpenHarmony: ✅ Native touchability; forwarding is unsupported and returns false without changing the window.
   */
  bool SetIgnoreMouseEvents(bool is_ignore_mouse_events, bool forward = false);

  /**
   * @brief Checks if the window ignores mouse events.
   *
   * @return true if mouse events are ignored, false if they are received
   */
  bool IsIgnoreMouseEvents() const;

  /** @brief Checks whether mouse movement is currently forwarded while ignoring input. */
  bool IsMouseMoveForwardingEnabled() const;

  /** @brief Checks whether the active platform can forward movement during pass-through.
   *  @return true on macOS, Windows and X11; false on Wayland and mobile platforms.
   */
  static bool IsMouseMoveForwardingSupported();

  /**
   * @brief Sets whether the window can receive keyboard focus.
   *
   * @param is_focusable true to allow focus, false to prevent it
   *
   * When disabled, the window cannot receive keyboard focus and will not
   * respond to keyboard input. Useful for utility or informational windows.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Overrides the window's ability to become key
   * - Windows: ✅ Supported - Declines mouse activation without disabling clicks
   * - Linux: ⚠️ Window-manager dependent - Sets GTK/GDK focus hints; Wayland
   *   compositors may ignore them
   * - Android: ❌ Not applicable - Focus is managed by the system
   * - iOS: ❌ Not applicable - Focus is managed by the system
   * - OpenHarmony: ✅ Updates native window focusability for attached windows.
   *
   * On Windows and Linux, SetNonActivating(true) also suppresses keyboard focus.
   * Turning it off restores the last SetFocusable() policy. Focus() does nothing
   * while focus is suppressed, and Show() shows the window without activating it.
   */
  void SetFocusable(bool is_focusable);

  /**
   * @brief Checks if the window can receive keyboard focus.
   *
   * @return true if the effective native focus policy permits focus, false otherwise.
   *         On Linux this reports the GTK/GDK hint; the window manager may ignore it.
   */
  bool IsFocusable() const;

  // === User Interaction ===

  /**
   * @brief Shows the native window menu at a content-relative position.
   * @param position Logical pixels from the top-left of the content area.
   *        The operating system may reposition the menu to fit the display.
   * @return true if the menu was displayed or the window manager accepted the
   *         request, including cancellation; false if unsupported or rejected.
   *
   * Use this from a secondary click in a custom title bar: on Windows from the
   * button's release, as a native title bar does, and on Linux from its press
   * (see below). Windows runs a nested native menu loop until selection or
   * cancellation, which takes the mouse messages meanwhile: called from the
   * press, the release goes to the menu and the content never sees it (Flutter
   * then ignores the next secondary click). Selected items are delivered as
   * system window commands. Linux asks the window manager to show its own menu.
   * Its contents and actions remain the manager's choice.
   *
   * @note Platform availability:
   * - macOS: ❌ No equivalent - Always returns false
   * - Windows: ✅ Native system menu with current restore/move/size/minimize/
   *   maximize states. Requires a visible top-level window with a system menu.
   * - Linux: ⚠️ X11 requires the _GTK_SHOW_WINDOW_MENU window-manager hint;
   *   Wayland requires an active pointer button press in this window, and the
   *   compositor may ignore the request. No GTK fallback menu is substituted.
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ❌ Not applicable - Always returns false
   */
  bool ShowSystemMenu(Point position);

  /**
   * @brief Checks if the active platform can request a native window menu.
   * @return true on Windows, supported X11 managers, or Wayland; false elsewhere.
   *         A particular window or input gesture may still be rejected.
   * @see ShowSystemMenu() for platform availability.
   */
  static bool IsSystemMenuSupported();

  /**
   * @brief Tells the window where the app draws its own maximize button.
   *
   * A hidden title bar takes the system's caption buttons with it, and on
   * Windows 11 the snap layouts that open when the pointer rests on the
   * maximize button. Report the rectangle of the app's own button and the
   * window answers the system's hit test there as its maximize button, so
   * resting on it opens the snap layouts again. The button keeps receiving
   * the pointer: moves, presses and releases over it are passed on to the
   * content, so it still draws its hover and pressed states and handles the
   * click (maximize or restore) itself.
   *
   * Update it whenever the button moves or changes size; an empty rectangle
   * removes it. The window's resize borders still win where they overlap.
   *
   * @param bounds The button, in logical coordinates relative to the content
   *               area's top-left corner (the space ShowSystemMenu() uses).
   * @return true if the platform uses it; false where unsupported, or for a
   *         rectangle that is not finite.
   *
   * @note Platform availability:
   * - macOS: ❌ Not applicable - Always returns false; the zoom button has no
   *   snap layouts to bring back.
   * - Windows: ✅ Supported - Snap layouts on Windows 11; Windows 10 shows the
   *   maximize tooltip only. Not with NATIVEAPI_ENABLE_WINUI3.
   * - Linux: ❌ Not applicable - Always returns false
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ❌ Not applicable - Always returns false
   */
  bool SetMaximizeButtonBounds(Rectangle bounds);

  /**
   * @brief The rectangle last given to SetMaximizeButtonBounds().
   * @return It, or an empty rectangle when none is set or it is unsupported.
   * @see SetMaximizeButtonBounds() for platform availability.
   */
  Rectangle GetMaximizeButtonBounds() const;

  /**
   * @brief Whether SetMaximizeButtonBounds() can take effect here.
   * @see SetMaximizeButtonBounds() for platform availability.
   */
  static bool IsMaximizeButtonBoundsSupported();

  /**
   * @brief Performs the double-click action for a custom title bar.
   * @return true if an action was requested, or the macOS preference explicitly
   *         disables it; false if unsupported, unavailable or rejected.
   *
   * Call from your custom title bar's double-click handler. Hidden title bars
   * leave hit testing to the application. On macOS, empty backgrounds within the
   * original title-bar band also invoke this action automatically; controls and
   * custom mouse handlers and host window event dispatchers are excluded.
   * The macOS preference is read on each call, so changes take effect immediately.
   * A successful request does not guarantee the window manager applies it.
   *
   * @note Platform availability:
   * - macOS: ✅ Uses the user's AppleActionOnDoubleClick preference: Maximize
   *   (or unset) zooms, Minimize miniaturizes, None deliberately does nothing.
   *   Legacy Zoom is also accepted. Fill requests the OS native Fill action when
   *   available; this currently requires a runtime-checked private AppKit action.
   *   Unsupported or unknown actions return false.
   * - Windows: ✅ Toggles maximize/restore using the native system command.
   * - Linux: ⚠️ Requests maximize/unmaximize; the window manager may ignore it.
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ⚠️ Public SDK maximize in floating mode; recover in
   *   maximized or split-screen mode. A live API 22+ free-window query must
   *   confirm support; only older SDKs without that query use a 2in1 fallback.
   *   An applied disabled maximize caption policy rejects this action; missing
   *   individual caption-button support does not disable otherwise valid SDK
   *   actions. Fullscreen, minimized, unknown, destroyed and pending windows
   *   reject requests. UI callers report SDK promise submission without blocking;
   *   workers wait for its result. The SDK may reject an unsupported window
   *   asynchronously after a UI caller returns. Hosts forward their custom
   *   title-bar gesture explicitly; the native system gesture is unchanged.
   *
   * Full-screen and minimized windows reject state changes. Disabled maximize or
   * minimize controls reject their corresponding action.
   */
  bool PerformTitleBarDoubleClick();

  /**
   * @brief Initiates a user drag operation for moving the window.
   *
   * Allows the user to drag the window by clicking and dragging anywhere
   * within the window's content area, not just the title bar. This is
   * commonly used for frameless windows or custom title bars.
   *
   * Call it from a mouse-down handler: the window then moves as if the user had
   * grabbed its title bar, and the move ends when the button is released.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Hands the drag to the window server.
   * - Windows: ✅ Fully supported - Hands the drag to the system frame.
   * - Linux: ✅ Fully supported - Starts a window-manager move drag (X11 and Wayland).
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ⚠️ Calls public startMoving from an active ArkTS onTouch down
   *   or move handler. Supports child/floating windows and freeform main windows;
   *   full-screen phone main windows are rejected by the SDK.
   */
  void StartDragging();

  /**
   * @brief Initiates a user resize operation from the given edge or corner.
   *
   * @param edge The window edge or corner that follows the mouse
   *
   * Call this from a mouse-down handler in a custom resize grip: the window
   * then resizes as if the user had grabbed the native frame at the given
   * edge, and the operation ends when the mouse button is released. Minimum
   * and maximum sizes and any aspect ratio constraint are respected. This is
   * intended for frameless windows or custom chrome.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Tracks the mouse until the button is released.
   * - Windows: ✅ Fully supported - Hands the drag to the system frame.
   * - Linux: ✅ Fully supported - Starts a window-manager resize drag.
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ❌ Not applicable - Always ignored
   */
  void StartResizing(ResizeEdge edge);

 protected:
  using EventEmitter<WindowEvent>::Emit;
  using EventEmitter<WindowEvent>::EmitAsync;
  void StartEventListening() override;
  void StopEventListening() override;

  /**
   * @brief Internal method to get the platform-specific native window object.
   *
   * This method must be implemented by platform-specific code to return
   * the underlying native window object.
   *
   * @return Pointer to the native window object
   */
  void* GetNativeObjectInternal() const override;

 private:
  /**
   * @brief Forward declaration of platform-specific implementation class.
   *
   * This class uses the PIMPL (Pointer to Implementation) idiom to hide
   * platform-specific details and reduce compilation dependencies.
   */
  class Impl;

  /** @brief Pointer to the platform-specific implementation */
  std::unique_ptr<Impl> pimpl_;

  // Shared by every platform's GetContentView(): wraps the native content view
  // once and hands the same View back afterwards (view.cpp).
  std::shared_ptr<View> ContentViewFor(void* native_content_view) const;
  mutable std::shared_ptr<View> content_view_;
  std::shared_ptr<detail::WindowEventSubscription> event_subscription_;
  bool DispatchWindowTask(std::function<void()> task);
};

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

/**
 * Event class for window focus gained
 *
 * This event is emitted when a window gains focus and becomes the active window.
 */
class WindowFocusedEvent : public WindowEvent {
 public:
  explicit WindowFocusedEvent(WindowId window_id) : WindowEvent(window_id) {}

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowFocusedEvent"; }

  /**
   * Get the static type index for this event type
   */
};

/**
 * Event class for window focus lost
 *
 * This event is emitted when a window loses focus and is no longer the active window.
 */
class WindowBlurredEvent : public WindowEvent {
 public:
  explicit WindowBlurredEvent(WindowId window_id) : WindowEvent(window_id) {}

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowBlurredEvent"; }

  /**
   * Get the static type index for this event type
   */
};

/**
 * Event class for window minimized
 *
 * This event is emitted when a window is minimized to the taskbar or dock.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported
 * - Windows: ✅ Fully supported
 * - Linux: ⚠️ X11 only - A Wayland compositor does not tell a client that it was minimized.
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ⚠️ Native window mode changes; hiding a child does not imply minimization.
 */
class WindowMinimizedEvent : public WindowEvent {
 public:
  explicit WindowMinimizedEvent(WindowId window_id) : WindowEvent(window_id) {}

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowMinimizedEvent"; }

  /**
   * Get the static type index for this event type
   */
};

/**
 * Event class for window maximized
 *
 * This event is emitted when a window is maximized to fill the entire screen.
 * Entering full screen is not maximizing and does not emit it.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported - A zoom counts as maximizing; emitted while the zoom animation
 *   is still settling.
 * - Windows: ✅ Fully supported
 * - Linux: ✅ Fully supported
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ⚠️ Native window mode changes where the device supports maximization.
 */
class WindowMaximizedEvent : public WindowEvent {
 public:
  explicit WindowMaximizedEvent(WindowId window_id) : WindowEvent(window_id) {}

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowMaximizedEvent"; }

  /**
   * Get the static type index for this event type
   */
};

/**
 * Event class for window restored
 *
 * This event is emitted when a window leaves the minimized or the maximized state. A
 * maximized window that was minimized and comes back is restored (to maximized) once.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported
 * - Windows: ✅ Fully supported
 * - Linux: ⚠️ Leaving the maximized state everywhere; leaving the minimized state on X11 only.
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ⚠️ Native transitions out of maximized/minimized mode.
 */
class WindowRestoredEvent : public WindowEvent {
 public:
  explicit WindowRestoredEvent(WindowId window_id) : WindowEvent(window_id) {}

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowRestoredEvent"; }

  /**
   * Get the static type index for this event type
   */
};

/**
 * Event class for window moved
 *
 * This event is emitted when a window is moved to a new position on the screen,
 * repeatedly while the user drags it. A resize from the top or left edge moves the
 * window as well and emits both events.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported
 * - Windows: ✅ Fully supported
 * - Linux: ⚠️ X11 only - A Wayland client never learns where its window is.
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ✅ Native rectangle-change notifications on attached windows, in logical coordinates.
 */
class WindowMovedEvent : public WindowEvent {
 public:
  WindowMovedEvent(WindowId window_id, Point new_position)
      : WindowEvent(window_id), new_position_(new_position) {}

  /**
   * Get the new position of the window
   * @return The position Window::GetPosition() reported when the event was emitted
   */
  Point GetNewPosition() const { return new_position_; }

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowMovedEvent"; }

  /**
   * Get the static type index for this event type
   */

 private:
  Point new_position_;
};

/**
 * Event class for window resized
 *
 * This event is emitted when a window is resized to a new size, repeatedly while the
 * user drags an edge and for every step of an animated resize. Maximizing and
 * restoring resize the window too.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported
 * - Windows: ✅ Fully supported
 * - Linux: ✅ Fully supported
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ✅ Native rectangle-change notifications on attached windows, in logical coordinates.
 */
class WindowResizedEvent : public WindowEvent {
 public:
  WindowResizedEvent(WindowId window_id, Size new_size)
      : WindowEvent(window_id), new_size_(new_size) {}

  /**
   * Get the new size of the window
   * @return The size Window::GetSize() reported when the event was emitted
   */
  Size GetNewSize() const { return new_size_; }

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowResizedEvent"; }

  /**
   * Get the static type index for this event type
   */

 private:
  Size new_size_;
};

/**
 * Event class for a window appearing
 *
 * This event is emitted the first time a window is shown — not when the native
 * window object is allocated, which no platform reports for windows the library
 * did not create. A window that is created and never shown emits nothing; hiding
 * and showing it again does not emit a second event.
 *
 * Windows that were already on screen when the first listener was added emit no
 * WindowCreatedEvent, but still emit WindowClosedEvent.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported
 * - Windows: ✅ Fully supported
 * - Linux: ✅ Fully supported
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ✅ Emitted once when the host attaches the native window.
 */
class WindowCreatedEvent : public WindowEvent {
 public:
  explicit WindowCreatedEvent(WindowId window_id) : WindowEvent(window_id) {}

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowCreatedEvent"; }
};

/**
 * Event class for a window going away for good
 *
 * This event is emitted when a window is closed and its native window is being
 * torn down, whoever closed it. Hiding a window does not emit it, and neither
 * does closing a window that was never shown. By the time
 * the event arrives WindowManager::Get() may no longer return the window: use
 * the ID to drop whatever was kept for it. Events the close itself causes, such
 * as WindowBlurredEvent, may still follow it.
 *
 * It cannot veto the close; that is what a close-requested event is for.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported
 * - Windows: ✅ Fully supported
 * - Linux: ✅ Fully supported
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ✅ Native destruction invalidates all wrappers and removes the registry entry.
 */
class WindowClosedEvent : public WindowEvent {
 public:
  explicit WindowClosedEvent(WindowId window_id) : WindowEvent(window_id) {}

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowClosedEvent"; }
};

/**
 * Event class for a window entering full screen
 *
 * This event is emitted once a window is full screen, whoever made it so: SetFullScreen(),
 * or the user through the system's own controls where there are any. IsFullScreen()
 * already returns true. The new size is reported by WindowResizedEvent.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported - The green button, the View menu and SetFullScreen() alike;
 *   emitted once the full-screen animation has finished.
 * - Windows: ⚠️ SetFullScreen() only - Windows has no full-screen window state, so a window
 *   another component makes borderless and monitor-sized is not reported.
 * - Linux: ✅ Fully supported - Whatever the window manager reports, SetFullScreen() and
 *   its own shortcuts (F11 in many) alike; emitted when the window manager confirms it.
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ⚠️ Confirmed SDK fullscreen changes. Freeform windows use native
 *   window mode; phone/non-freeform windows use the SDK fullscreen property,
 *   which can change with content layout without enlarging a child frame.
 */
class WindowEnteredFullScreenEvent : public WindowEvent {
 public:
  explicit WindowEnteredFullScreenEvent(WindowId window_id) : WindowEvent(window_id) {}

  std::string GetTypeName() const override { return "WindowEnteredFullScreenEvent"; }
};

/**
 * Event class for a window leaving full screen
 *
 * This event is emitted once a window is no longer full screen, whoever ended it.
 * IsFullScreen() already returns false.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported - Emitted once the animation back has finished.
 * - Windows: ⚠️ SetFullScreen() only - See WindowEnteredFullScreenEvent.
 * - Linux: ✅ Fully supported - Emitted when the window manager confirms it.
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ⚠️ Confirmed SDK fullscreen exits; see WindowEnteredFullScreenEvent
 *   for freeform mode versus phone content-layout semantics.
 */
class WindowExitedFullScreenEvent : public WindowEvent {
 public:
  explicit WindowExitedFullScreenEvent(WindowId window_id) : WindowEvent(window_id) {}

  std::string GetTypeName() const override { return "WindowExitedFullScreenEvent"; }
};

/**
 * A close-button / native close gesture or Window::Close request. Each native
 * window has one shared request across all subscribed wrappers. Bare forced
 * close and system shutdown requests have IsCancelable() == false and cannot
 * be held by an observer. WindowManager continues reporting WindowClosedEvent
 * after the native window actually closes; it does not produce this request.
 */
class WindowCloseRequestedEvent : public WindowEvent {
 public:
  WindowCloseRequestedEvent(WindowId window_id, std::shared_ptr<EventRequest> request)
      : WindowEvent(window_id), request_(std::move(request)) {}
  std::shared_ptr<EventRequest> GetRequest() const { return request_; }
  std::string GetTypeName() const override { return "WindowCloseRequestedEvent"; }
 private:
  std::shared_ptr<EventRequest> request_;
};

/**
 * Event class for a change of one of the window's properties
 *
 * Emitted once per property whose value changed, whoever changed it: a Window
 * setter on any wrapper of the native window, or another component or the
 * system changing the native window directly where the platform reports that.
 * Setting a property to the value it already has emits nothing. Read the new
 * value through the getter WindowProperty names; it already returns it.
 *
 * Listen on the Window to follow one window, or on WindowManager for all.
 *
 * @note Platform availability:
 * - macOS: ✅ Supported - Setters, and direct changes to the NSWindow's title,
 *   style mask (resizable, minimizable, closable, title bar) and level.
 * - Windows: ✅ Supported - Setters, and direct changes to the window text,
 *   styles and topmost state.
 * - Linux: ✅ Supported - Setters, and direct changes to the GtkWindow's title,
 *   resizable and deletable properties and the window manager's above/below
 *   state.
 * - Android: ❌ Not applicable - Never emitted
 * - iOS: ❌ Not applicable - Never emitted
 * - OpenHarmony: ⚠️ Successful nativeapi property changes; external title/policy changes have no public SDK query.
 */
class WindowPropertyChangedEvent : public WindowEvent {
 public:
  WindowPropertyChangedEvent(WindowId window_id, WindowProperty property)
      : WindowEvent(window_id), property_(property) {}

  /**
   * @return The property whose value changed
   */
  WindowProperty GetProperty() const { return property_; }

  std::string GetTypeName() const override { return "WindowPropertyChangedEvent"; }

 private:
  WindowProperty property_;
};

/**
 * Event class for a change of whether a window can be seen
 *
 * Emitted when Window::GetOcclusionState() changes for a window that was
 * already seen once, whoever caused it: showing, hiding, minimizing, moving
 * it or other windows. GetOcclusionState() on the event is the new state.
 *
 * @see Window::GetOcclusionState() for platform availability. On Windows the
 * state is recomputed shortly after windows change, so a brief covering may
 * not be reported.
 */
class WindowOcclusionChangedEvent : public WindowEvent {
 public:
  WindowOcclusionChangedEvent(WindowId window_id, WindowOcclusionState occlusion_state)
      : WindowEvent(window_id), occlusion_state_(occlusion_state) {}

  /**
   * @return The new state
   */
  WindowOcclusionState GetOcclusionState() const { return occlusion_state_; }

  std::string GetTypeName() const override { return "WindowOcclusionChangedEvent"; }

 private:
  WindowOcclusionState occlusion_state_;
};

/**
 * @brief Emitted after a desktop toolbar reservation is created or changed.
 * @see Window::Dock() for platform availability.
 */
class WindowDockedEvent : public WindowEvent {
 public:
  WindowDockedEvent(WindowId window_id, WindowDockSide side)
      : WindowEvent(window_id), side_(side) {}
  /** @brief Gets the reserved screen edge. @return Left or Right. */
  WindowDockSide GetSide() const { return side_; }
  std::string GetTypeName() const override { return "WindowDockedEvent"; }

 private:
  WindowDockSide side_;
};

/**
 * @brief Emitted after this window's desktop toolbar reservation is removed.
 * @see Window::Undock() for platform availability.
 */
class WindowUndockedEvent : public WindowEvent {
 public:
  explicit WindowUndockedEvent(WindowId window_id) : WindowEvent(window_id) {}
  std::string GetTypeName() const override { return "WindowUndockedEvent"; }
};

}  // namespace nativeapi
