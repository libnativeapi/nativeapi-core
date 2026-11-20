#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "foundation/event.h"
#include "foundation/event_emitter.h"
#include "foundation/geometry.h"
#include "menu.h"
#include "window.h"

namespace nativeapi {

class EventRequest;
namespace detail {
class ApplicationQuitDispatch;
struct ApplicationQuitState;
}  // namespace detail

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

/**
 * @brief Application lifecycle events
 */
class ApplicationEvent : public Event {
 public:
  ApplicationEvent() = default;
  virtual ~ApplicationEvent() = default;
};

/**
 * @brief Event emitted when the application starts
 */
class ApplicationStartedEvent : public ApplicationEvent {
 public:
  ApplicationStartedEvent() = default;
  std::string GetTypeName() const override { return "ApplicationStartedEvent"; }
};

/**
 * @brief Event emitted when the application is about to exit
 */
class ApplicationExitingEvent : public ApplicationEvent {
 public:
  ApplicationExitingEvent(int exit_code) : exit_code_(exit_code) {}

  int GetExitCode() const { return exit_code_; }
  std::string GetTypeName() const override { return "ApplicationExitingEvent"; }

 private:
  int exit_code_;
};

/**
 * @brief Event emitted when the application is activated (brought to foreground)
 */
class ApplicationActivatedEvent : public ApplicationEvent {
 public:
  ApplicationActivatedEvent() = default;
  std::string GetTypeName() const override { return "ApplicationActivatedEvent"; }
};

/**
 * @brief Event emitted when the application is deactivated (sent to background)
 */
class ApplicationDeactivatedEvent : public ApplicationEvent {
 public:
  ApplicationDeactivatedEvent() = default;
  std::string GetTypeName() const override { return "ApplicationDeactivatedEvent"; }
};

/**
 * @brief Event emitted when the application receives a quit request
 *
 * Programmatic Quit() and macOS native quit queries supply a shared,
 * cancelable request. Native termination that must continue supplies a
 * noncancelable request. An existing host retains its final termination decision.
 */
class ApplicationQuitRequestedEvent : public ApplicationEvent {
 public:
  explicit ApplicationQuitRequestedEvent(std::shared_ptr<EventRequest> request = nullptr)
      : request_(std::move(request)) {}
  /**
   * @brief Gets the shared quit confirmation request.
   * @return The shared request supplied by the producer.
   */
  std::shared_ptr<EventRequest> GetRequest() const { return request_; }
  std::string GetTypeName() const override { return "ApplicationQuitRequestedEvent"; }

 private:
  std::shared_ptr<EventRequest> request_;
};

/**
 * @brief Light or dark appearance for the application's user interface.
 *
 * Passed to Application::SetBrightness() to override the appearance the
 * operating system would otherwise apply to this application's windows,
 * and returned by Application::GetSystemBrightness().
 */
enum class Brightness {
  /** Follow the operating system's current appearance setting, or unknown. */
  System,
  /** Always use the light appearance. */
  Light,
  /** Always use the dark appearance. */
  Dark
};

/**
 * @brief Event emitted when the operating system's light or dark appearance changes.
 */
class ApplicationSystemBrightnessChangedEvent : public ApplicationEvent {
 public:
  explicit ApplicationSystemBrightnessChangedEvent(Brightness brightness)
      : brightness_(brightness) {}

  /**
   * @brief Gets the operating system's new appearance.
   * @return Brightness::Light or Brightness::Dark.
   */
  Brightness GetBrightness() const { return brightness_; }
  std::string GetTypeName() const override { return "ApplicationSystemBrightnessChangedEvent"; }

 private:
  Brightness brightness_;
};

/**
 * @brief Application is a singleton class that manages the application lifecycle
 *
 * The Application class provides centralized management of application-wide state,
 * lifecycle events, and coordination between different managers. It follows the
 * singleton pattern to ensure there's only one application instance throughout
 * the application lifetime.
 *
 * Key features:
 * - Singleton pattern ensures single application instance
 * - Event-driven architecture for application lifecycle notifications
 * - Cross-platform application management
 * - Integration with existing managers (WindowManager, DisplayManager, etc.)
 * - Thread-safe access to the singleton instance
 * - Automatic cleanup of resources on destruction
 *
 * @note This class is thread-safe for singleton access, but individual operations
 *       may require additional synchronization depending on the platform implementation.
 * @note OpenHarmony hosts initialize the ArkTS UI environment, attach their owning
 * UIAbilityContext and forward actual onCreate/onForeground/onBackground/onDestroy
 * callbacks through the platform-internal application bridge. One owning UIAbility
 * is tracked per linked core/UI environment; this does not manage other Abilities
 * or forcibly kill the application's processes. Subscribe before onCreate to
 * observe ApplicationStartedEvent. State queries and appearance calls marshal
 * worker calls to the initialized UI environment.
 */
class Application : public EventEmitter<ApplicationEvent> {
 public:
  /**
   * @brief Get the singleton instance of Application
   *
   * This method provides access to the unique instance of Application using
   * the Meyer's singleton pattern. The instance is created on first call and
   * remains alive for the duration of the application. This method is thread-safe
   * and guarantees that only one instance will be created even in multi-threaded
   * environments.
   *
   * @return Reference to the singleton Application instance
   * @thread_safety This method is thread-safe
   *
   * @code
   * // Usage example:
   * auto& app = Application::GetInstance();
   * int exit_code = app.Run();
   * @endcode
   */
  static Application& GetInstance();

  /**
   * @brief Destructor
   *
   * Cleans up all resources, stops event monitoring, and performs final cleanup.
   * This is automatically called when the application terminates.
   */
  virtual ~Application();

  /**
   * @brief Run the application main event loop
   *
   * Starts the main event loop and blocks until Quit() stops it.
   * This method handles platform-specific event processing and coordination
   * between different managers.
   *
   * @return The exit code passed to Quit()
   * @note OpenHarmony's UIAbility owns the event loop. This method returns -1
   * without starting a nested loop; use the host lifecycle bridge instead.
   *
   * @code
   * auto& app = Application::GetInstance();
   * int exit_code = app.Run();
   * @endcode
   */
  int Run();

  /**
   * @brief Run the application with the specified window
   *
   * Starts the main event loop with the given window and blocks until
   * Quit() stops it. This method sets the window as the primary window
   * and starts the event loop.
   *
   * @param window The window to run the application with
   * @return The exit code passed to Quit(), or -1 when @p window is null
   * @note On OpenHarmony the host owns the event loop: always returns -1,
   * without changing the primary window or starting another loop.
   *
   * @code
   * auto& app = Application::GetInstance();
   * auto window = std::make_shared<Window>();
   * int exit_code = app.Run(window);
   * @endcode
   */
  int Run(std::shared_ptr<Window> window);

  /**
   * @brief Request the application to quit
   *
   * Emits ApplicationQuitRequestedEvent with a cancelable EventRequest. A
   * listener can Cancel() or Defer() it for asynchronous confirmation. Only
   * after every listener has finished and every deferred decision has accepted
   * does the event loop stop: Run()
   * returns @p exit_code after emitting ApplicationExitingEvent, and the
   * process keeps going. Windows stay open until released. Callable from any
   * thread: the request is carried over to the main thread, where the events
   * are emitted.
   * Repeated Quit() calls during confirmation share the pending request and
   * update its exit code. A cancelled request leaves the application running;
   * a later Quit() starts a fresh request. Exceptions during request dispatch
   * cancel it. A failed main-thread dispatch does not quit from a worker.
   *
   * When the loop is not Run()'s own (a host such as a Flutter runner owns
   * it), there is nothing to return to and the process ends instead, after
   * ApplicationExitingEvent: on macOS through -[NSApplication terminate:],
   * which exits with status 0 whatever @p exit_code is after the host delegate
   * allows termination (the host may cancel or defer); on Linux with
   * @p exit_code; on Windows the main thread's windows are destroyed while
   * the host's loop still runs (a Flutter view shuts its engine down then),
   * and the loop gets WM_QUIT and ends it.
   *
   * On macOS, a native AppKit quit query (Cmd+Q, Dock Quit, or an eligible
   * system request) also emits a cancelable ApplicationQuitRequestedEvent.
   * Native approval resumes the existing host's decision; the host may still
   * refuse or defer. Duplicate attempts share the pending confirmation through
   * the host's asynchronous reply. Mandatory termination only emits a
   * noncancelable request, invalidates pending votes, and continues. Allowed
   * native termination ends the process without Run() returning.
   *
   * @param exit_code The exit code to use when quitting (default: 0)
   *
   * @note Platform availability:
   * - macOS: Supported - cancelable programmatic and native quit queries, then the host decision
   * - Windows: Supported - cancelable Quit(), then the native loop stops
   * - Linux: Supported - cancelable Quit(), then the native loop stops
   * - Android: Not applicable - ignored; the Activity owns its lifecycle
   * - iOS: Not applicable - ignored; UIApplication owns its lifecycle
   * - OpenHarmony: Supported - cancelable programmatic Quit(), then public
   *   UIAbilityContext.terminateSelf() for the attached owning Ability. Actual
   *   onDestroy emits ApplicationExitingEvent; the requested code is event data,
   *   not a process exit status. Required host destruction cannot be vetoed.
   *
   * @code
   * auto& app = Application::GetInstance();
   * app.Quit(0);  // Quit with success code
   * @endcode
   */
  void Quit(int exit_code = 0);

  /**
   * @brief Check if the application is currently running
   *
   * @return true if the application is running, false otherwise
   * @note OpenHarmony reports the attached owning UIAbility's actual creation /
   * destruction state. Before lifecycle attachment or after environment cleanup
   * it returns false; a background Ability remains running.
   */
  bool IsRunning() const;

  /**
   * @brief Check if this is a single instance application
   *
   * @return true if only one instance is allowed, false otherwise
   * @note OpenHarmony reads ApplicationInfo.multiAppMode from the actual owning
   * context. Multi-instance/clone modes, missing context or failed queries return
   * false. A singleton Ability launch type alone is not treated as proof.
   */
  bool IsSingleInstance() const;

  /**
   * @brief Shows the application again after Hide(), and makes it active.
   *
   * The windows that Hide() took away come back where they were, and the
   * application moves to the front, as when the user picks it in the Dock.
   * Calling it on an application that is not hidden only activates it.
   *
   * @return true if the application was shown, false where the platform has no
   *         application-level hiding
   *
   * @see Hide() for platform availability.
   */
  bool Show();

  /**
   * @brief Hides the application: all of its windows at once.
   *
   * This is the system's own hiding (Cmd+H on macOS), not Window::Hide() on
   * every window: the system knows the application is hidden, so the Dock
   * offers "Show" rather than "Hide", clicking the Dock icon brings the
   * windows back, and the application that was active before gets the focus.
   * Hiding a hidden application does nothing.
   *
   * The system hides the application asynchronously: right after this call
   * IsVisible() may still return true. While the application is hidden,
   * Window::IsVisible() returns false for its windows, and Window::Show() on
   * one of them shows the whole application again.
   *
   * @return true if the application was hidden, false where the platform has no
   *         application-level hiding
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - -[NSApplication hide:], for an application
   *   without a Dock icon (SetDockIconVisible(false)) as well.
   * - Windows: ❌ Not applicable - No application-level hiding; always returns
   *   false. Hide the windows with Window::Hide().
   * - Linux: ❌ Not applicable - No application-level hiding; always returns
   *   false. Hide the windows with Window::Hide().
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ⚠️ The attached owning UIAbility uses public showAbility /
   *   hideAbility on a desktop with the status-bar capability. Add the application
   *   to the status bar first. The system hides/restores the Ability's window group;
   *   no per-window hide loop is substituted. The SDK removes the running Dock
   *   item while hidden; restore through Show, a status-icon action or a normal
   *   launcher activation. UI calls return submission status;
   *   workers wait for the SDK result. Pending requests, absent lifecycle/context,
   *   phone profiles, synchronous SDK errors and rejected worker requests return
   *   false. Actual visibility changes update IsVisible and activation events.
   */
  bool Hide();

  /**
   * @brief Checks if the application is visible, that is, not hidden.
   *
   * An application without any window on screen is still visible; only Hide(),
   * or the user hiding the application through the system (Cmd+H, the Dock
   * menu), makes this false.
   *
   * @return false while the application is hidden; always true where the
   *         platform has no application-level hiding
   *
   * @note OpenHarmony combines owning-Ability foreground/background state with
   * actual SDK application hiding. Native primary-window restoration (including
   * an external launcher restore) clears that hidden state. An ordinary per-window Hide does
   * not mark the application hidden. State changes precede activation events.
   * Before lifecycle attachment or after destruction it returns false.
   * @see Hide() for platform availability.
   */
  bool IsVisible() const;

  /**
   * @brief Set the application icon
   *
   * Sets the application icon that appears in the dock (macOS), taskbar (Windows),
   * or application list (Linux).
   *
   * @param icon_path Path to the icon file (UTF-8)
   * @return true if the icon was set successfully, false otherwise
   *
   * @note Platform availability:
   * - macOS: ✅ Supported - The Dock icon, from any image file NSImage reads.
   * - Windows: ⚠️ Current windows - An .ico file, set as the big (taskbar,
   *   Alt+Tab) and small (title bar) icon of every top-level window the
   *   application has now; windows created later keep the executable's icon.
   *   False when the file is no icon or there is no window yet.
   * - Linux: ✅ Supported - GTK's default icon for every window without its own.
   * - Android: ❌ Not supported - Always returns false
   * - iOS: ❌ Not supported - Always returns false; the icon comes from Info.plist
   * - OpenHarmony: ⚠️ Desktop UIAbilityContext.setMissionWindowIcon updates the
   *   owning Ability's window, mission card and shortcut-bar snapshot icon. It
   *   does not change the installed launcher icon or another Ability's icon.
   *   Requires an initialized owning context and loaded main window. Images must
   *   be square, decodable and at most 128 MB as RGBA. Invalid paths/images and
   *   phone profiles return false. UI calls report submission; workers wait for
   *   SDK success. The original Image data is isolated from SDK ownership.
   */
  bool SetIcon(const std::string& icon_path);

  /**
   * @brief Show or hide the dock icon (macOS only)
   *
   * Controls whether the application appears in the macOS dock.
   * This method has no effect on other platforms.
   *
   * @param visible true to show the dock icon, false to hide it
   * @return true if the operation succeeded, false otherwise
   */
  bool SetDockIconVisible(bool visible);

  /**
   * @brief Show a progress bar on the application's dock or taskbar icon.
   *
   * @param progress Fraction complete in the range 0.0 to 1.0. A negative
   *        value removes the progress bar; a value above 1.0 shows an
   *        indeterminate (busy) indicator where the platform supports one.
   * @return true if the progress bar was updated, false otherwise
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Drawn on the Dock tile.
   * - Windows: ✅ Fully supported - Shown on the taskbar button of the primary
   *   window (or the first window when no primary window is set). Values above
   *   1.0 show the marquee style.
   * - Linux: ⚠️ Partial - Sent as a com.canonical.Unity.LauncherEntry signal
   *   keyed on the desktop file named after the program name; honored by
   *   Unity, KDE Plasma, elementary and GNOME's Dash to Dock. Indeterminate is
   *   shown as full.
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ❌ Not applicable - Always returns false
   */
  bool SetProgressBar(double progress);

  /**
   * @brief Show a short text badge on the application's dock or taskbar icon.
   *
   * @param label Text to display, typically a count such as "3" or "99+".
   *        An empty string removes the badge. Keep it to a few characters.
   * @return true if the badge was updated, false otherwise
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - The Dock tile badge; any text is allowed.
   * - Windows: ✅ Fully supported - Rendered as a taskbar overlay icon on the
   *   primary window (or the first window when no primary window is set);
   *   only the first three characters fit.
   * - Linux: ⚠️ Partial - Sent as a LauncherEntry count, so the label must be
   *   an integer; other text returns false. See SetProgressBar() for which
   *   desktops honor it.
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ⚠️ Public notificationManager.setBadgeNumber updates this
   *   application's numeric launcher badge. Accepts nonnegative ASCII decimal
   *   counts through INT32_MAX and the label "99+" (count 100). Empty or "0"
   *   clears the badge; counts above 99 render as "99+". Other text returns false.
   *   UI calls report submission; workers wait for SDK success. Notification /
   *   badge settings control actual display. No notification is published and
   *   notification authorization is not requested automatically.
   */
  bool SetBadgeLabel(const std::string& label);

  /**
   * @brief Force the light or dark appearance for the whole application.
   *
   * @param brightness The appearance to use; Brightness::System restores the
   *        operating system's setting
   * @return true if the appearance was applied, false otherwise
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Sets the NSApplication appearance, which all
   *   windows and menus inherit.
   * - Windows: ⚠️ Partial - Toggles the dark title bar and frame on every
   *   window that exists at the time of the call; windows created later are
   *   not updated. Application content is unaffected.
   * - Linux: ✅ Fully supported - Sets the GTK prefer-dark-theme setting; when
   *   switching to light while a "-dark" theme variant is active, the light
   *   variant of that theme is selected. Brightness::System restores both.
   * - Android: ❌ Not applicable - Always returns false
   * - iOS: ❌ Not applicable - Always returns false
   * - OpenHarmony: ✅ ApplicationContext.setColorMode() updates application
   *   resources. System restores the system mode. Requires the owning Ability's
   *   window/content to be loaded; a UIAbility-specific override takes precedence.
   *   Invalid values, missing initialized context or SDK exceptions return false.
   */
  bool SetBrightness(Brightness brightness);

  /**
   * @brief Gets the operating system's current light or dark appearance.
   *
   * This reads the system preference rather than any application override
   * applied with SetBrightness(). Subscribe to
   * ApplicationSystemBrightnessChangedEvent to observe runtime changes.
   *
   * @return Brightness::Light or Brightness::Dark when known, or
   *         Brightness::System when the platform cannot determine it.
   *
   * @note Platform availability:
   * - macOS: ✅ Fully supported - Reads AppleInterfaceStyle and observes
   *   AppleInterfaceThemeChangedNotification.
   * - Windows: ✅ Fully supported - Reads HKCU AppsUseLightTheme and observes
   *   WM_SETTINGCHANGE (ImmersiveColorSet).
   * - Linux: ✅ Supported - Reads XDG Desktop Portal org.freedesktop.appearance
   *   color-scheme with GtkSettings fallback, and observes portal changes.
   * - Android: ❌ Not implemented - Always returns Brightness::System.
   * - iOS: ⚠️ Query only - Reads UIScreen.mainScreen.traitCollection on iOS 13+.
   * - OpenHarmony: ⚠️ Query only - Reads owning Ability context.config.colorMode.
   */
  Brightness GetSystemBrightness() const;

  /**
   * @brief Set the application menu bar
   *
   * Sets the application-wide menu bar that appears at the top of the screen.
   * This is primarily used on macOS, but may have effects on other platforms.
   *
   * @param menu Shared pointer to the menu to set as the application menu
   * @return true if the menu was set successfully, false otherwise
   */
  bool SetMenuBar(std::shared_ptr<Menu> menu);

  /**
   * @brief Get the primary window of the application
   *
   * Returns the main window of the application, if one exists.
   *
   * @return Shared pointer to the primary window, or nullptr if none exists
   * @note OpenHarmony initially discovers the attached stage's main window.
   * An explicit selection overrides it. A cleared or destroyed selection returns
   * nullptr; destruction of the owning Ability clears this primary selection.
   */
  std::shared_ptr<Window> GetPrimaryWindow() const;

  /**
   * @brief Set the primary window of the application
   *
   * Sets the main window of the application. This window will be used for
   * application-level operations and may receive special treatment from
   * the platform.
   *
   * @param window Shared pointer to the window to set as primary
   * @note On OpenHarmony nullptr explicitly clears selection; invalid native
   * wrappers are ignored. This changes application selection, not WindowStage
   * ownership or the system window type.
   */
  void SetPrimaryWindow(std::shared_ptr<Window> window);

  /**
   * @brief Get all application windows
   *
   * Returns a vector containing all windows belonging to this application.
   *
   * @return Vector of shared pointers to all application windows
   */
  std::vector<std::shared_ptr<Window>> GetAllWindows() const;

 protected:
  void StartEventListening() override;
  void StopEventListening() override;

 private:
  /**
   * @brief Private constructor to enforce singleton pattern
   *
   * Automatically initializes the Application instance and sets up platform-specific
   * event monitoring. This constructor is private to prevent direct instantiation.
   */
  Application();

  friend class detail::ApplicationQuitDispatch;
  void RequestQuit(int exit_code,
                   std::function<void(int)> stop_loop = nullptr,
                   std::shared_ptr<void> loop_owner = nullptr,
                   std::function<void(bool)> native_confirmation = nullptr);
  void PerformQuit(int exit_code);
  void InvalidateQuitRequest();
  std::shared_ptr<detail::ApplicationQuitState> quit_state_;

  // Prevent copy construction and assignment to maintain singleton property
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  /**
   * @brief Platform-specific implementation details
   *
   * Uses the PIMPL (Pointer to Implementation) idiom to hide platform-specific
   * details and reduce compilation dependencies.
   */
  class Impl;
  std::unique_ptr<Impl> pimpl_;

  /**
   * @brief Application state
   */
  bool initialized_;
  bool running_;
  int exit_code_;

  /**
   * @brief Primary application window
   */
  std::shared_ptr<Window> primary_window_;
};

/**
 * @brief Convenience function to run the application with the specified window
 *
 * This is equivalent to calling Application::GetInstance().Run(window).
 * This function provides a simple way to run an application without
 * explicitly accessing the singleton.
 *
 * @param window The window to run the application with
 * @return Exit code of the application (0 for success)
 *
 * @code
 * auto window = std::make_shared<Window>();
 * int exit_code = RunApp(window);
 * @endcode
 */
int RunApp(std::shared_ptr<Window> window);

}  // namespace nativeapi
