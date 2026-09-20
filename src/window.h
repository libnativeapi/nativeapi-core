#pragma once
#include <memory>
#include <string>
#include "foundation/color.h"
#include "foundation/event.h"
#include "foundation/geometry.h"
#include "foundation/id_allocator.h"
#include "foundation/native_object_provider.h"

namespace nativeapi {

/**
 * @typedef WindowId
 * @brief Unique identifier for a window instance.
 *
 * This type is used to uniquely identify window instances across the system.
 * Each window gets assigned a unique ID when created.
 */
typedef IdAllocator::IdType WindowId;

/**
 * @brief Title bar style options for windows.
 *
 * Defines how a window's title bar should be displayed. This affects the
 * appearance and visibility of the standard window title bar including the
 * title text and window control buttons (minimize, maximize, close).
 *
 * @note Platform behavior may vary:
 * - Windows: Hidden style removes the title bar; the resize border stays on the sides and bottom
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
   * Window::StartDragging() (or a WindowDragSession).
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
 * @note This class is not thread-safe. All window operations should be performed
 *       on the main UI thread.
 */
class Window : public NativeObjectProvider, public std::enable_shared_from_this<Window> {
 public:
  /**
   * @brief Default constructor creates a new window with default settings.
   *
   * Creates a new window with platform-default size, position, and properties.
   * The window is initially hidden and must be explicitly shown.
   * The window is automatically registered in the WindowRegistry.
   */
  Window();

  /**
   * @brief Constructor that wraps an existing native window object.
   *
   * @param window Pointer to an existing platform-specific window object
   * @note The Window instance takes ownership of the native window object
   */
  Window(void* native_window);

  /**
   * @brief Virtual destructor ensures proper cleanup of resources.
   *
   * Destroys the window and releases all associated resources including
   * the native window object.
   */
  virtual ~Window();

  /**
   * @brief Gets the unique identifier for this window.
   *
   * @return WindowId The unique identifier assigned to this window
   */
  WindowId GetId() const;

  // === Focus Management ===

  /**
   * @brief Brings the window to the front and gives it keyboard focus.
   *
   * Makes this window the active window and brings it to the foreground.
   * The window will receive keyboard input after this call.
   */
  void Focus();

  /**
   * @brief Removes keyboard focus from the window.
   *
   * The window will no longer receive keyboard input, but remains visible.
   * Focus may be transferred to another window or removed entirely.
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
   */
  bool IsVisible() const;
  // === Window State Management ===

  /**
   * @brief Maximizes the window to fill the available screen space.
   *
   * Expands the window to occupy the maximum available area on the screen,
   * typically excluding taskbars and docks.
   */
  void Maximize();

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
   * @brief Restores the window from minimized or maximized state.
   *
   * Returns the window to its normal state and size. If the window was
   * minimized, it becomes visible again. If maximized, it returns to
   * its previous non-maximized size.
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
   * window decorations (title bar, borders) visible.
   */
  void SetFullScreen(bool is_full_screen);

  /**
   * @brief Checks if the window is currently in fullscreen mode.
   *
   * @return true if the window is fullscreen, false otherwise
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
   * The constraint applies while the user drags a window edge; it does not
   * change the current size and is not enforced by SetSize() or SetBounds().
   * Minimum and maximum sizes still apply on top of the ratio.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - The ratio is applied to the content area.
   * - Windows: ✅ Fully supported - The ratio is applied to the outer frame.
   * - Linux: ✅ Fully supported - Applied via GDK aspect geometry hints; the
   *   window manager decides how strictly they are honored.
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ❌ Not applicable - Always ignored
   */
  void SetAspectRatio(double aspect_ratio);

  /**
   * @brief Gets the aspect ratio constraint set by SetAspectRatio().
   *
   * @return Width divided by height, or 0 when no constraint is set
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
   */
  void SetMinimizable(bool is_minimizable);

  /**
   * @brief Checks if the window can be minimized by the user.
   *
   * @return true if user can minimize the window, false otherwise
   */
  bool IsMinimizable() const;

  /**
   * @brief Sets whether the window can be maximized by the user.
   *
   * @param is_maximizable true to allow maximizing, false to disable
   *
   * Controls the availability of maximize functionality in the window's
   * title bar and system menu. Programmatic maximizing is still possible.
   */
  void SetMaximizable(bool is_maximizable);

  /**
   * @brief Checks if the window can be maximized by the user.
   *
   * @return true if user can maximize the window, false otherwise
   */
  bool IsMaximizable() const;

  /**
   * @brief Sets whether the window can enter fullscreen mode.
   *
   * @param is_full_screenable true to allow fullscreen, false to disable
   *
   * Controls whether the window supports fullscreen mode. On some platforms,
   * this affects the availability of fullscreen controls in the UI.
   */
  void SetFullScreenable(bool is_full_screenable);

  /**
   * @brief Checks if the window supports fullscreen mode.
   *
   * @return true if fullscreen is supported, false otherwise
   */
  bool IsFullScreenable() const;

  /**
   * @brief Sets whether the window can be closed by the user.
   *
   * @param is_closable true to allow closing, false to disable
   *
   * When disabled, the close button in the title bar is hidden or disabled.
   * The window can still be closed programmatically.
   */
  void SetClosable(bool is_closable);

  /**
   * @brief Checks if the window can be closed by the user.
   *
   * @return true if user can close the window, false otherwise
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
   * - OpenHarmony: ❌ Not applicable - Mobile apps don't have window control buttons
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
   * - OpenHarmony: ❌ Not applicable - Always returns false
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
   * - OpenHarmony: ❌ Not applicable - Always ignored, returns false
   */
  bool SetParentWindow(std::shared_ptr<Window> parent);

  /**
   * @brief Gets the window this window belongs to.
   *
   * Read from the native window, so it also reports a parent the embedding
   * framework has set.
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
   * A non-activating window can be shown, ordered to the front and receive
   * keyboard input without making its application the active one. The
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
   *   that can become key but never main. This is the only platform where
   *   keyboard focus is tied to application activation, so it is the only one
   *   with observable behavior.
   * - Windows: ⚠️ Recorded only - Keyboard focus is per window, so the flag
   *   is stored and reported back by IsNonActivating() but changes nothing.
   * - Linux: ⚠️ Recorded only - Same as Windows.
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ❌ Not applicable - Always ignored
   */
  void SetNonActivating(bool is_non_activating);

  /**
   * @brief Checks if the window is non-activating.
   *
   * @return true if showing or focusing the window does not activate the
   *         application, false otherwise
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
   * - OpenHarmony: ❌ Not applicable - Always ignored
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
   * - OpenHarmony: ❌ Not applicable - Always returns false
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
   * - Windows: ⚠️ Frameless windows only - The desktop compositor always draws the
   *   shadow of a window that has a title bar; with TitleBarStyle::Hidden the shadow
   *   follows this flag
   * - Linux: ⚠️ Client-side decorations only - Removed from the windows GTK decorates
   *   itself: every window on Wayland, windows with a header bar on X11. On a window
   *   that is not shown yet it takes effect when the window is shown. A window the
   *   window manager decorates keeps its shadow, and HasShadow() keeps saying so.
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ❌ Not applicable - Always ignored
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
   *
   * When enabled, mouse events (clicks, hovers, etc.) pass through the window
   * to whatever is behind it. Useful for overlay or heads-up display windows.
   */
  void SetIgnoreMouseEvents(bool is_ignore_mouse_events);

  /**
   * @brief Checks if the window ignores mouse events.
   *
   * @return true if mouse events are ignored, false if they are received
   */
  bool IsIgnoreMouseEvents() const;

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
   * - Windows: ❌ Not implemented
   * - Linux: ❌ Not implemented
   * - Android / iOS / OpenHarmony: ❌ Not applicable - Focus is managed by the system
   */
  void SetFocusable(bool is_focusable);

  /**
   * @brief Checks if the window can receive keyboard focus.
   *
   * @return true if the window can be focused, false otherwise
   */
  bool IsFocusable() const;

  // === User Interaction ===

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
   * - OpenHarmony: ❌ Not applicable - Always ignored
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
};

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

/**
 * Base class for all window-related events
 *
 * This class provides common functionality for window events,
 * including access to the window ID that triggered the event.
 *
 * WindowManager emits them for every window of the process — also for windows
 * the library did not create, such as the ones of the embedding framework — and
 * no matter what caused the change: the user, the system or a call to Window.
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
 * - OpenHarmony: ❌ Not applicable - Never emitted
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
 * - OpenHarmony: ❌ Not applicable - Never emitted
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
 * - OpenHarmony: ❌ Not applicable - Never emitted
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
 * - OpenHarmony: ❌ Not applicable - Never emitted
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
 * - OpenHarmony: ❌ Not applicable - Never emitted
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
 * - OpenHarmony: ❌ Not applicable - Never emitted
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
 * - OpenHarmony: ❌ Not applicable - Never emitted
 */
class WindowClosedEvent : public WindowEvent {
 public:
  explicit WindowClosedEvent(WindowId window_id) : WindowEvent(window_id) {}

  /**
   * Get a string representation of the event type
   */
  std::string GetTypeName() const override { return "WindowClosedEvent"; }
};

}  // namespace nativeapi
