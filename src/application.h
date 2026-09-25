#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "foundation/event.h"
#include "foundation/event_emitter.h"
#include "foundation/geometry.h"
#include "menu.h"
#include "window.h"

namespace nativeapi {

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
 */
class ApplicationQuitRequestedEvent : public ApplicationEvent {
 public:
  ApplicationQuitRequestedEvent() = default;
  std::string GetTypeName() const override { return "ApplicationQuitRequestedEvent"; }
};

/**
 * @brief Light or dark appearance for the application's user interface.
 *
 * Passed to Application::SetBrightness() to override the appearance the
 * operating system would otherwise apply to this application's windows.
 */
enum class Brightness {
  /** Follow the operating system's current appearance setting. */
  System,
  /** Always use the light appearance. */
  Light,
  /** Always use the dark appearance. */
  Dark
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
   * Emits ApplicationQuitRequestedEvent, then stops the event loop: Run()
   * returns @p exit_code after emitting ApplicationExitingEvent, and the
   * process keeps going. Windows stay open until released. Callable from any
   * thread: the request is carried over to the main thread, where the events
   * are emitted.
   *
   * When the loop is not Run()'s own (a host such as a Flutter runner owns
   * it), there is nothing to return to and the process ends instead, after
   * ApplicationExitingEvent: on macOS through -[NSApplication terminate:],
   * which exits with status 0 whatever @p exit_code is; on Linux with
   * @p exit_code; on Windows the host's loop gets WM_QUIT and ends it.
   *
   * A quit the user or the system starts outside Quit() (Cmd+Q, logout) also
   * emits ApplicationQuitRequestedEvent. On macOS it cannot be vetoed and
   * ends the process without Run() returning.
   *
   * @param exit_code The exit code to use when quitting (default: 0)
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
   */
  bool IsRunning() const;

  /**
   * @brief Check if this is a single instance application
   *
   * @return true if only one instance is allowed, false otherwise
   */
  bool IsSingleInstance() const;

  /**
   * @brief Set the application icon
   *
   * Sets the application icon that appears in the dock (macOS), taskbar (Windows),
   * or application list (Linux).
   *
   * @param icon_path Path to the icon file
   * @return true if the icon was set successfully, false otherwise
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
   * - OpenHarmony: ❌ Not applicable - Always returns false
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
   * - OpenHarmony: ❌ Not applicable - Always returns false
   */
  bool SetBrightness(Brightness brightness);

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

 private:
  /**
   * @brief Private constructor to enforce singleton pattern
   *
   * Automatically initializes the Application instance and sets up platform-specific
   * event monitoring. This constructor is private to prevent direct instantiation.
   */
  Application();

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

 private:
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
