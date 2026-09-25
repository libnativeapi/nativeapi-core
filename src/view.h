#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "foundation/color.h"
#include "foundation/event.h"
#include "foundation/event_emitter.h"
#include "foundation/geometry.h"
#include "foundation/id_allocator.h"
#include "foundation/native_object_provider.h"

namespace nativeapi {

class Image;
class Window;

/**
 * @typedef ViewId
 * @brief Unique identifier for a view instance. Shared by every View subclass.
 */
typedef IdAllocator::IdType ViewId;

/**
 * @brief How a View places its subviews.
 */
enum class ViewLayout {
  /**
   * Subviews keep the frame given by View::SetFrame(). The default.
   */
  Absolute,

  /**
   * Left to right. SetSpacing() separates neighbours, SetPadding() insets
   * the whole row from the container's edges.
   */
  Row,

  /**
   * Top to bottom, with the same knobs as Row.
   */
  Column
};

/**
 * @brief Cross-axis placement of a subview inside a Row or Column.
 */
enum class ViewAlignment {
  /**
   * The subview fills the cross axis. The default.
   */
  Stretch,

  /**
   * Top of a Row, left of a Column.
   */
  Start,

  /**
   * Centred on the cross axis.
   */
  Center,

  /**
   * Bottom of a Row, right of a Column.
   */
  End
};

/**
 * @brief Horizontal placement of text inside a Label or TextField.
 */
enum class TextAlignment {
  /**
   * Leading edge: left in left-to-right locales.
   */
  Start,

  /**
   * Centred.
   */
  Center,

  /**
   * Trailing edge: right in left-to-right locales.
   */
  End
};

/**
 * @brief Which toolkit draws a view's controls.
 *
 * A view keeps the backend it was created with. Views of different backends
 * cannot be put in one tree: View::AddSubview() ignores such a subview.
 */
enum class ViewBackend {
  /**
   * The platform's own controls: AppKit on macOS, Win32 common controls on
   * Windows, GTK 3 on Linux.
   */
  Native,

  /**
   * WinUI 3 (Windows App SDK) XAML controls. Windows builds with
   * NATIVEAPI_ENABLE_WINUI3 only, where it is the default, as for menus. A
   * window's root view is then a XAML Island covering its content area, and
   * GetNativeObject() of a control returns its XAML element as an IInspectable*.
   */
  WinUI3
};

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

/**
 * @brief Base class for every view event. Carries the id of the view it
 * concerns.
 */
class ViewEvent : public Event {
 public:
  explicit ViewEvent(ViewId view_id) : view_id_(view_id) {}
  virtual ~ViewEvent() = default;

  ViewId GetViewId() const { return view_id_; }

  std::string GetTypeName() const override { return "ViewEvent"; }

 private:
  ViewId view_id_;
};

/**
 * @brief A focusable view gained keyboard focus.
 */
class ViewFocusedEvent : public ViewEvent {
 public:
  explicit ViewFocusedEvent(ViewId view_id) : ViewEvent(view_id) {}

  std::string GetTypeName() const override { return "ViewFocusedEvent"; }
};

/**
 * @brief A focusable view lost keyboard focus.
 */
class ViewBlurredEvent : public ViewEvent {
 public:
  explicit ViewBlurredEvent(ViewId view_id) : ViewEvent(view_id) {}

  std::string GetTypeName() const override { return "ViewBlurredEvent"; }
};

/**
 * @brief A Button was activated by the mouse or the keyboard.
 */
class ButtonClickedEvent : public ViewEvent {
 public:
  explicit ButtonClickedEvent(ViewId view_id) : ViewEvent(view_id) {}

  std::string GetTypeName() const override { return "ButtonClickedEvent"; }
};

/**
 * @brief The text of a TextField changed through user input.
 *
 * Not emitted for TextField::SetText().
 */
class TextFieldChangedEvent : public ViewEvent {
 public:
  TextFieldChangedEvent(ViewId view_id, std::string text)
      : ViewEvent(view_id), text_(std::move(text)) {}

  std::string GetText() const { return text_; }

  std::string GetTypeName() const override { return "TextFieldChangedEvent"; }

 private:
  std::string text_;
};

/**
 * @brief Enter was pressed in a single-line TextField.
 */
class TextFieldSubmittedEvent : public ViewEvent {
 public:
  explicit TextFieldSubmittedEvent(ViewId view_id) : ViewEvent(view_id) {}

  std::string GetTypeName() const override { return "TextFieldSubmittedEvent"; }
};

// ---------------------------------------------------------------------------
// View: the container and the base of every control
// ---------------------------------------------------------------------------

/**
 * @class View
 * @brief A rectangle inside a window that can hold other views.
 *
 * View on its own is a plain container. The controls (Label, Button,
 * TextField, ImageView) derive from it and add their own methods; everything
 * here — the subview tree, geometry, layout, visibility, focus — applies to
 * all of them. A window hands out its root view through
 * Window::GetContentView(); views are only ever shown by adding them, directly
 * or through parents, to such a root.
 *
 * Coordinates are logical points with the origin at the parent's top-left
 * corner and y growing downwards, like Window::SetBounds(). A parent whose
 * layout is Row or Column positions its subviews itself; SetFrame() then only
 * matters until the next layout pass.
 *
 * Ownership follows the tree: a parent keeps its subviews alive, a subview
 * refers to its parent weakly. Destroying a view created by this library
 * destroys its native control; a root view wraps the window's own content
 * view and leaves it alone.
 *
 * @note Every method must be called on the main thread. Events are emitted
 * synchronously from the platform callback.
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported - AppKit views and controls
 * - Windows: ✅ Fully supported - Win32 common controls
 * - Linux: ✅ Fully supported - GTK 3 widgets
 * - Android: ❌ Not applicable - IsSupported() is false, every call is ignored
 * - iOS: ❌ Not applicable - IsSupported() is false, every call is ignored
 * - OpenHarmony: ❌ Not applicable - IsSupported() is false, every call is ignored
 *
 * @example
 * ```cpp
 * auto window = std::make_shared<Window>();
 * auto root = window->GetContentView();
 * root->SetLayout(ViewLayout::Column);
 * root->SetPadding(EdgeInsets::All(16));
 * root->SetSpacing(8);
 *
 * auto name = std::make_shared<TextField>();
 * name->SetPlaceholder("Username");
 * auto status = std::make_shared<Label>();
 * auto button = std::make_shared<Button>("Sign in");
 * button->AddListener<ButtonClickedEvent>([=](const ButtonClickedEvent&) {
 *   status->SetText("Signing in as " + name->GetText() + "...");
 * });
 *
 * root->AddSubview(name);
 * root->AddSubview(status);
 * root->AddSubview(button);
 * window->Show();
 * ```
 */
class View : public EventEmitter<ViewEvent>,
             public NativeObjectProvider,
             public std::enable_shared_from_this<View> {
 public:
  /**
   * @brief Checks if views are available on this platform.
   *
   * @return false on Android, iOS and OpenHarmony, where every View method
   *         is a no-op and Window::GetContentView() returns nullptr.
   */
  static bool IsSupported();

  /**
   * @brief Checks if a backend is available on this platform and build.
   *
   * @return true for Native wherever views are supported; true for WinUI3 only
   *         in a Windows build with NATIVEAPI_ENABLE_WINUI3. Whether the Windows
   *         App Runtime can actually start is known only when a view is created.
   */
  static bool IsBackendSupported(ViewBackend backend);

  /**
   * @brief Sets the backend of views created from now on, on every thread.
   *
   * Views that exist keep theirs.
   *
   * @return false, changing nothing, when @p backend is not supported.
   */
  static bool SetDefaultBackend(ViewBackend backend);

  /**
   * @brief Gets the backend new views are created with.
   *
   * @return WinUI3 in a Windows build with NATIVEAPI_ENABLE_WINUI3 until
   *         SetDefaultBackend() says otherwise; Native everywhere else.
   */
  static ViewBackend GetDefaultBackend();

  /**
   * @brief Creates an empty container.
   */
  View();

  /**
   * @brief Wraps an existing native view without taking ownership.
   *
   * @param native_view NSView* on macOS, HWND on Windows, GtkWidget* on Linux.
   *        Used by Window for its root view.
   */
  explicit View(void* native_view);

  virtual ~View();

  View(const View&) = delete;
  View& operator=(const View&) = delete;
  View(View&&) = delete;
  View& operator=(View&&) = delete;

  /**
   * @brief Gets the unique identifier of this view.
   *
   * @return The id, carried by every ViewEvent this view emits.
   */
  ViewId GetId() const;

  /**
   * @brief Gets the backend this view was created with.
   *
   * @return The default backend at creation time; Native if that was WinUI3
   *         but the Windows App Runtime could not start on this thread (a
   *         diagnostic is written to stderr once).
   */
  ViewBackend GetBackend() const;

  // === Tree ===

  /**
   * @brief Appends a subview, on top of the existing ones.
   *
   * A view already inside another parent is removed from it first. Ignored
   * for nullptr, for this view itself, for one of its ancestors, and for a
   * view of another backend (GetBackend()).
   *
   * @param subview The view to add.
   */
  void AddSubview(std::shared_ptr<View> subview);

  /**
   * @brief Inserts a subview at @p index in the z-order and layout order.
   *
   * @param index Position in the subview list; clamped to the count.
   * @param subview The view to insert.
   */
  void InsertSubview(size_t index, std::shared_ptr<View> subview);

  /**
   * @brief Removes a subview. The caller's reference keeps it alive.
   *
   * @param subview The subview to remove.
   * @return true if @p subview was a direct subview and is now detached.
   */
  bool RemoveSubview(std::shared_ptr<View> subview);

  /**
   * @brief Removes the subview at @p index.
   *
   * @param index Position in the subview list.
   * @return false if @p index is out of range.
   */
  bool RemoveSubviewAt(size_t index);

  /**
   * @brief Removes every subview.
   */
  void ClearSubviews();

  /**
   * @brief Gets the number of direct subviews.
   *
   * @return The count.
   */
  size_t GetSubviewCount() const;

  /**
   * @brief Gets the subview at @p index.
   *
   * @param index Position in the subview list.
   * @return The subview, or nullptr if @p index is out of range.
   */
  std::shared_ptr<View> GetSubviewAt(size_t index) const;

  /**
   * @brief Gets every direct subview, bottom-most first.
   *
   * @return A copy of the subview list.
   */
  std::vector<std::shared_ptr<View>> GetSubviews() const;

  /**
   * @brief Gets the parent view.
   *
   * @return The parent, or nullptr for a root view or a detached view.
   */
  std::shared_ptr<View> GetParent() const;

  /**
   * @brief Gets the window this view is currently inside.
   *
   * @return The window, or nullptr while the view is not under a window's
   *         root view.
   */
  std::shared_ptr<Window> GetWindow() const;

  // === Geometry & layout ===

  /**
   * @brief Sets the frame, relative to the parent's top-left corner.
   *
   * Under a Row or Column parent the frame is overwritten by the next layout
   * pass; use SetPreferredSize() and SetFlex() there instead. Ignored on a
   * root view, whose frame follows the window's content area.
   *
   * @param frame Origin and size in logical points.
   */
  void SetFrame(Rectangle frame);

  /**
   * @brief Gets the current frame, relative to the parent's top-left corner.
   *
   * @return The frame in logical points, read from the native view.
   */
  Rectangle GetFrame() const;

  /**
   * @brief Sets the size a Row or Column parent should give this view.
   *
   * A zero width or height means "use the intrinsic size" on that axis.
   *
   * @param size The preferred size in logical points.
   */
  void SetPreferredSize(Size size);

  /**
   * @brief Gets the preferred size.
   *
   * @return The size set by SetPreferredSize(); zero by default.
   */
  Size GetPreferredSize() const;

  /**
   * @brief Gets the size the view wants to be when nothing stretches it.
   *
   * A Row or Column parent uses it on every axis where SetPreferredSize()
   * left 0, so nested rows and columns size to their content.
   *
   * @return The intrinsic size in logical points: the native control's own
   *         size; for a Row or Column container, what its visible subviews
   *         need at their preferred or intrinsic sizes, plus spacing and
   *         padding; zero for an Absolute container.
   */
  Size GetIntrinsicSize() const;

  /**
   * @brief Sets this view's share of the leftover main-axis space in a Row or
   * Column parent.
   *
   * @param flex 0 keeps the preferred or intrinsic size; a positive value
   *        takes leftover space in proportion to the other flex values.
   */
  void SetFlex(double flex);

  /**
   * @brief Gets the flex factor.
   *
   * @return The value set by SetFlex(); 0 by default.
   */
  double GetFlex() const;

  /**
   * @brief Sets the cross-axis placement inside a Row or Column parent.
   *
   * @param alignment Stretch by default.
   */
  void SetAlignment(ViewAlignment alignment);

  /**
   * @brief Gets the cross-axis placement.
   *
   * @return The alignment set by SetAlignment().
   */
  ViewAlignment GetAlignment() const;

  /**
   * @brief Sets how this view places its subviews.
   *
   * @param layout Absolute by default.
   */
  void SetLayout(ViewLayout layout);

  /**
   * @brief Gets how this view places its subviews.
   *
   * @return The layout set by SetLayout().
   */
  ViewLayout GetLayout() const;

  /**
   * @brief Sets the gap between neighbouring subviews of a Row or Column.
   *
   * @param spacing Gap in logical points; 0 by default.
   */
  void SetSpacing(double spacing);

  /**
   * @brief Gets the gap between neighbouring subviews.
   *
   * @return The spacing set by SetSpacing().
   */
  double GetSpacing() const;

  /**
   * @brief Sets the inset between this view's edges and its Row or Column
   * content.
   *
   * @param padding Insets in logical points; zero by default.
   */
  void SetPadding(EdgeInsets padding);

  /**
   * @brief Gets the inset between this view's edges and its content.
   *
   * @return The padding set by SetPadding().
   */
  EdgeInsets GetPadding() const;

  // === Appearance & state ===

  /**
   * @brief Sets whether the view is drawn. A hidden view takes no space in a
   * Row or Column.
   *
   * @param is_visible true to show, false to hide.
   */
  void SetVisible(bool is_visible);

  /**
   * @brief Checks if the view is visible.
   *
   * @return true unless hidden with SetVisible(false).
   */
  bool IsVisible() const;

  /**
   * @brief Sets whether the view accepts input.
   *
   * @param is_enabled false greys out a control and ignores its input.
   */
  void SetEnabled(bool is_enabled);

  /**
   * @brief Checks if the view accepts input.
   *
   * @return true unless disabled with SetEnabled(false).
   */
  bool IsEnabled() const;

  /**
   * @brief Sets the background colour.
   *
   * @param color The colour; a zero alpha restores the platform default.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Drawn by the view's layer
   * - Windows: ⚠️ Containers and labels only - Other controls keep the theme colour
   * - Linux: ⚠️ Applied through a CSS provider - The theme may override it
   * - Android: ❌ Not applicable - Always ignored
   * - iOS: ❌ Not applicable - Always ignored
   * - OpenHarmony: ❌ Not applicable - Always ignored
   */
  void SetBackgroundColor(Color color);

  /**
   * @brief Gets the background colour.
   *
   * @return The colour set by SetBackgroundColor(); transparent by default.
   */
  Color GetBackgroundColor() const;

  /**
   * @brief Sets the tooltip shown when the pointer rests on the view.
   *
   * @param tooltip The text, or std::nullopt for none.
   */
  void SetTooltip(const std::optional<std::string>& tooltip);

  /**
   * @brief Gets the tooltip.
   *
   * @return The text set by SetTooltip(), or std::nullopt.
   */
  std::optional<std::string> GetTooltip() const;

  /**
   * @brief Gives the view keyboard focus. Ignored for views that cannot take
   * focus, such as a container or a label.
   */
  void Focus();

  /**
   * @brief Takes keyboard focus away from the view.
   */
  void Blur();

  /**
   * @brief Checks if the view has keyboard focus.
   *
   * @return true while the view is the window's focused control.
   */
  bool IsFocused() const;

 protected:
  /**
   * @brief Wraps the native control a subclass constructor created; the view
   * owns it. Not a public constructor: View(void*) never takes ownership.
   */
  struct NativeControl;
  explicit View(NativeControl control);

  void StartEventListening() override;
  void StopEventListening() override;
  void* GetNativeObjectInternal() const override;

  // The controls reach their native object through this; Impl itself is
  // private to the library (view_impl.h).
  class Impl;
  std::unique_ptr<Impl> pimpl_;

 private:
  friend class Window;
  friend struct ViewInternal;
};

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

/**
 * @class Label
 * @brief Read-only text.
 *
 * @see View for tree, geometry, layout and platform availability.
 */
class Label : public View {
 public:
  /**
   * @brief Creates a label.
   *
   * @param text Initial text.
   */
  explicit Label(const std::string& text = "");
  virtual ~Label();

  /**
   * @brief Sets the text.
   *
   * @param text Any string; newlines start a new line.
   */
  void SetText(const std::string& text);

  /**
   * @brief Gets the text.
   *
   * @return The current text.
   */
  std::string GetText() const;

  /**
   * @brief Sets the text colour.
   *
   * @param color The colour; a zero alpha restores the platform default.
   */
  void SetTextColor(Color color);

  /**
   * @brief Gets the text colour.
   *
   * @return The colour set by SetTextColor(); transparent when at the default.
   */
  Color GetTextColor() const;

  /**
   * @brief Sets the font size.
   *
   * @param size Size in points; 0 restores the platform default.
   */
  void SetFontSize(double size);

  /**
   * @brief Gets the font size.
   *
   * @return The size in points; 0 while at the platform default.
   */
  double GetFontSize() const;

  /**
   * @brief Sets the horizontal placement of the text.
   *
   * @param alignment Start by default.
   */
  void SetTextAlignment(TextAlignment alignment);

  /**
   * @brief Gets the horizontal placement of the text.
   *
   * @return The alignment set by SetTextAlignment().
   */
  TextAlignment GetTextAlignment() const;
};

/**
 * @class Button
 * @brief A push button. Emits ButtonClickedEvent.
 *
 * @see View for tree, geometry, layout and platform availability.
 */
class Button : public View {
 public:
  /**
   * @brief Creates a button.
   *
   * @param text Initial title.
   */
  explicit Button(const std::string& text = "");
  virtual ~Button();

  /**
   * @brief Sets the title.
   *
   * @param text The title.
   */
  void SetText(const std::string& text);

  /**
   * @brief Gets the title.
   *
   * @return The current title.
   */
  std::string GetText() const;
};

/**
 * @class TextField
 * @brief Text entry. Emits TextFieldChangedEvent as the user types and
 * TextFieldSubmittedEvent on Enter (single-line only).
 *
 * @see View for tree, geometry, layout and platform availability.
 */
class TextField : public View {
 public:
  /**
   * @brief Creates a text field.
   *
   * @param text Initial text.
   */
  explicit TextField(const std::string& text = "");
  virtual ~TextField();

  /**
   * @brief Sets the text. Does not emit TextFieldChangedEvent.
   *
   * @param text The text.
   */
  void SetText(const std::string& text);

  /**
   * @brief Gets the text.
   *
   * @return The current text, including what the user has typed.
   */
  std::string GetText() const;

  /**
   * @brief Sets the text colour.
   *
   * @param color The colour; a zero alpha restores the platform default.
   */
  void SetTextColor(Color color);

  /**
   * @brief Gets the text colour.
   *
   * @return The colour set by SetTextColor(); transparent when at the default.
   */
  Color GetTextColor() const;

  /**
   * @brief Sets the font size.
   *
   * @param size Size in points; 0 restores the platform default.
   */
  void SetFontSize(double size);

  /**
   * @brief Gets the font size.
   *
   * @return The size in points; 0 while at the platform default.
   */
  double GetFontSize() const;

  /**
   * @brief Sets the horizontal placement of the text.
   *
   * @param alignment Start by default.
   */
  void SetTextAlignment(TextAlignment alignment);

  /**
   * @brief Gets the horizontal placement of the text.
   *
   * @return The alignment set by SetTextAlignment().
   */
  TextAlignment GetTextAlignment() const;

  /**
   * @brief Sets the hint shown while the field is empty.
   *
   * @param placeholder The hint, or std::nullopt for none.
   */
  void SetPlaceholder(const std::optional<std::string>& placeholder);

  /**
   * @brief Gets the hint shown while the field is empty.
   *
   * @return The hint set by SetPlaceholder(), or std::nullopt.
   */
  std::optional<std::string> GetPlaceholder() const;

  /**
   * @brief Sets whether the user can change the text.
   *
   * @param is_editable false makes the field read-only but still selectable.
   */
  void SetEditable(bool is_editable);

  /**
   * @brief Checks if the user can change the text.
   *
   * @return true unless SetEditable(false) was called.
   */
  bool IsEditable() const;

  /**
   * @brief Sets password entry: the glyphs are masked.
   *
   * @param is_secure true to mask the text.
   */
  void SetSecure(bool is_secure);

  /**
   * @brief Checks if the text is masked.
   *
   * @return true after SetSecure(true).
   */
  bool IsSecure() const;

  /**
   * @brief Sets multi-line editing. Enter then inserts a newline instead of
   * emitting TextFieldSubmittedEvent.
   *
   * @param is_multiline true for a multi-line field.
   */
  void SetMultiline(bool is_multiline);

  /**
   * @brief Checks if the field is multi-line.
   *
   * @return true after SetMultiline(true).
   */
  bool IsMultiline() const;
};

/**
 * @class ImageView
 * @brief Shows an Image, scaled to fit while keeping its aspect ratio.
 *
 * @see View for tree, geometry, layout and platform availability.
 */
class ImageView : public View {
 public:
  /**
   * @brief Creates an empty image view.
   */
  ImageView();
  virtual ~ImageView();

  /**
   * @brief Sets the image to show.
   *
   * @param image The image, or nullptr to show nothing.
   */
  void SetImage(std::shared_ptr<Image> image);

  /**
   * @brief Gets the image shown.
   *
   * @return The image set by SetImage(), or nullptr.
   */
  std::shared_ptr<Image> GetImage() const;
};

}  // namespace nativeapi
