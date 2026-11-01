#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "foundation/event_emitter.h"
#include "foundation/id_allocator.h"
#include "shortcut.h"

namespace nativeapi {

typedef IdAllocator::IdType ShortcutId;

/**
 * @brief ShortcutManager is a singleton class that manages global keyboard shortcuts.
 *
 * The ShortcutManager provides centralized access to system-wide keyboard shortcut
 * registration and handling. It follows the singleton pattern to ensure there's only
 * one instance managing all shortcuts throughout the application lifetime.
 *
 * Key features:
 * - Singleton pattern ensures centralized shortcut management
 * - Event-driven architecture for shortcut activation notifications
 * - Cross-platform shortcut registration and monitoring
 * - Thread-safe access to the singleton instance
 * - Automatic cleanup of resources on destruction
 * - Support for both global and application-local shortcuts
 *
 * @note Platform availability:
 * - macOS: ✅ Carbon ordinary global hotkeys, with activation/release events.
 *   Media keys use a Quartz event tap for global scope and AppKit monitoring for
 *   application scope. Global taps require OS authorization and may be restricted
 *   by sandbox policy; permission loss unregisters affected global shortcuts.
 *   Application media monitoring requires AppKit dispatch but no global tap;
 *   app deactivation releases held application media keys.
 * - Windows: ✅ Win32 global hotkeys; activation events only.
 * - Linux: ⚠️ X11 registration, or asynchronous Wayland GlobalShortcuts Portal
 *   registration through RequestRegistration. Portal support requires a desktop
 *   provider and valid application identity; the user controls the final binding.
 * - Android: ❌ Native shortcut registration is not implemented.
 * - iOS: ❌ Native shortcut registration is not implemented.
 * - OpenHarmony: ⚠️ API 14+ public NDK global hotkeys, with Ctrl/Alt/Shift
 *   modifiers and an active main dispatcher. Meta/Fn/lock modifiers, release
 *   events are not implemented. Application scope uses a mounted companion
 *   NativeShortcutHost forwarding normal component key events, without reserving
 *   global hotkeys. It requires an initialized UI host and at least one attached
 *   key host; only the focused host receives unconsumed input. Global SDK conflicts
 *   fail registration. IsAvailable checks global availability by briefly probing
 *   native reservations; a competing application may still race a later Register call.
 *
 * @note This class is thread-safe for singleton access and shortcut operations.
 * @note Shortcut instances should be created and managed through this manager.
 */
class ShortcutManager : public EventEmitter<ShortcutEvent> {
 public:
  /**
   * @brief Get the singleton instance of ShortcutManager.
   *
   * This method provides access to the unique instance of ShortcutManager using
   * the Meyer's singleton pattern. The instance is created on first call and
   * remains alive for the duration of the application. This method is thread-safe
   * and guarantees that only one instance will be created even in multi-threaded
   * environments.
   *
   * @return Reference to the singleton ShortcutManager instance
   * @thread_safety This method is thread-safe
   *
   * @code
   * // Usage example:
   * auto& manager = ShortcutManager::GetInstance();
   * auto shortcut = manager.Register("Ctrl+Shift+A", callback);
   * @endcode
   */
  static ShortcutManager& GetInstance();

  /**
   * @brief Destructor for ShortcutManager.
   *
   * Cleans up all managed shortcuts, unregisters them from the system,
   * and releases system resources. This is automatically called when
   * the application terminates.
   */
  virtual ~ShortcutManager();

  /**
   * @brief Check if global shortcuts are supported on the current platform.
   *
   * Some platforms or configurations may not support global keyboard shortcuts
   * due to security restrictions or desktop environment limitations. This method
   * allows checking for availability before attempting to register shortcuts.
   *
   * @return true if global shortcuts are supported, false otherwise
   */
  bool IsSupported();

  /**
   * @brief Register a new global keyboard shortcut.
   *
   * Creates and registers a new keyboard shortcut that can be triggered
   * system-wide, regardless of which application has focus. The shortcut
   * will trigger the provided callback when activated.
   *
   * @param accelerator The keyboard shortcut string (e.g., "Ctrl+Shift+A", "Cmd+Space")
   * @param callback Function to call when the shortcut is activated
   * @return Shared pointer to the created Shortcut instance, nullptr if registration failed
   * @thread_safety This method is thread-safe
   *
   * @note Accelerator format follows Electron-style conventions:
   *       - Modifiers: Ctrl, Alt, Shift, Cmd (macOS), Super (Linux), Meta
   *       - Keys: A-Z, 0-9, F1-F12, Space, Tab, Enter, Escape, etc.
   *       - macOS media: MediaPlayPause, MediaNextTrack, MediaPreviousTrack,
   *         MediaFastForward, MediaRewind, VolumeUp, VolumeDown, VolumeMute,
   *         BrightnessUp, BrightnessDown, Eject. NativeMediaKey<N> denotes an
   *         explicit macOS NX auxiliary-control code, distinct from Carbon
   *         NativeKey<N>. Apple FAST/REWIND and NEXT/PREVIOUS are canonical aliases.
   *         One accelerator string identifies one registration; distinct named
   *         and numeric aliases can be used to register global and app scopes.
   *       - Examples: "Ctrl+C", "Cmd+Shift+4", "Alt+F4", "Ctrl+Alt+Delete"
   *
   * @example
   * ```cpp
   * auto shortcut = manager.Register("Ctrl+Shift+Q", []() {
   *     std::cout << "Quick action triggered!" << std::endl;
   * });
   *
   * if (!shortcut) {
   *     std::cerr << "Failed to register shortcut" << std::endl;
   * }
   * ```
   */
  std::shared_ptr<Shortcut> Register(const std::string& accelerator,
                                     std::function<void()> callback);

  /**
   * @brief Register a new keyboard shortcut with detailed options.
   *
   * Creates and registers a keyboard shortcut with additional configuration
   * options such as scope (global vs application-local) and description.
   *
   * @param options ShortcutOptions struct containing shortcut configuration
   * @return Shared pointer to the created Shortcut instance, nullptr if registration failed
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * ShortcutOptions options;
   * options.accelerator = "Ctrl+Alt+T";
   * options.callback = []() { OpenTerminal(); };
   * options.description = "Open terminal";
   * options.scope = ShortcutScope::Global;
   *
   * auto shortcut = manager.Register(options);
   * ```
   */
  std::shared_ptr<Shortcut> Register(const ShortcutOptions& options);

  /**
   * @brief Requests registration without waiting for interactive approval.
   *
   * Subscribe before requesting. A nonzero return value identifies this request
   * and its eventual ShortcutRegisteredEvent or ShortcutRegistrationFailedEvent.
   * The shortcut is available through Get only after successful registration.
   * Completion may occur before this method returns; listeners must retain the
   * event's ID rather than assume the caller has already stored the return value.
   *
   * @param options The requested shortcut configuration.
   * @param registration_key Stable, nonempty application-defined action key.
   * Reuse it across launches; description remains human-readable display text.
   * @return A nonzero request/shortcut ID if accepted, or 0 for invalid options,
   * an unsupported immediate global backend, or an accelerator/key already pending
   * or active. Deferred providers report unavailable services through the failure
   * event, without synchronously contacting the desktop in this call.
   * Rejected requests emit no completion event. Accepted requests complete once,
   * including cancellation, and failure events always carry this request's ID.
   * @note Platform availability:
   * - macOS: ✅ Immediate Carbon/media registration; same permission and scope
   *   requirements as Register. Completion uses shortcut events.
   * - Windows: ✅ Win32 registration; completion uses shortcut events.
   * - Linux: ⚠️ X11 registration or asynchronous Wayland Portal approval. Requires
   *   a GlobalShortcuts desktop provider. A portal accelerator is a preference;
   *   GetTriggerDescription reports the desktop's binding label. Unregister stops
   *   the session but does not erase the desktop's saved binding configuration.
   * - Android: ❌ Global requests return 0; application registration is unsupported.
   * - iOS: ❌ Global requests return 0; application registration is unsupported.
   * - OpenHarmony: ⚠️ Same host and scope requirements as Register.
   */
  ShortcutId RequestRegistration(const ShortcutOptions& options,
                                 const std::string& registration_key);

  /**
   * @brief Cancels a pending registration request.
   * @param request_id The nonzero ID returned by RequestRegistration.
   * @return true if the request was pending and is now cancelled, false if it
   * already completed or does not exist. Use Unregister for active shortcuts.
   * Cancellation produces one ShortcutRegistrationFailedEvent with this ID.
   */
  bool CancelRegistration(ShortcutId request_id);

  /**
   * @brief Cancels pending requests belonging to an application-defined key namespace.
   *
   * Useful after an interpreter or Flutter isolate restarts while the native
   * manager survives. Active shortcuts present before this call are unaffected.
   * A matching pending request approved during cancellation is unregistered.
   * Completion callbacks and resource cleanup run outside the registry lock.
   * @param registration_key_prefix Nonempty prefix of registration keys to reclaim.
   * An empty prefix does nothing, protecting other modules in the same process.
   * @return Number of matching pending requests cancelled or concurrently approved
   * shortcuts unregistered. Returns 0 if none match or the prefix is empty.
   * @see RequestRegistration for platform availability.
   */
  size_t CancelPendingRegistrations(const std::string& registration_key_prefix);

  /**
   * @brief Checks if a registration request is still pending.
   * @param request_id The ID returned by RequestRegistration.
   * @return true until completion or cancellation, false for unknown IDs.
   */
  bool IsRegistrationPending(ShortcutId request_id) const;

  /**
   * @brief Unregister a keyboard shortcut by its ID.
   *
   * Removes a previously registered shortcut from the system and
   * stops monitoring for its activation.
   *
   * @param id The unique identifier of the shortcut to unregister
   * @return true if the shortcut was successfully unregistered, false otherwise
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * ShortcutId id = shortcut->GetId();
   * bool success = manager.Unregister(id);
   * ```
   */
  bool Unregister(ShortcutId id);

  /**
   * @brief Unregister a keyboard shortcut by its accelerator string.
   *
   * Removes a previously registered shortcut from the system using
   * its accelerator string identifier.
   *
   * @param accelerator The keyboard shortcut string to unregister
   * @return true if the shortcut was successfully unregistered, false otherwise
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * bool success = manager.Unregister("Ctrl+Shift+Q");
   * ```
   */
  bool Unregister(const std::string& accelerator);

  /**
   * @brief Unregister all keyboard shortcuts.
   *
   * Removes all currently registered shortcuts from the system.
   * This is useful for cleanup or when switching shortcut profiles.
   *
   * @return Number of shortcuts unregistered and pending requests cancelled
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * int count = manager.UnregisterAll();
   * std::cout << "Unregistered " << count << " shortcuts" << std::endl;
   * ```
   */
  int UnregisterAll();

  /**
   * @brief Get a shortcut by its unique ID.
   *
   * Retrieves a previously registered shortcut using its assigned ID.
   *
   * @param id The unique identifier of the shortcut
   * @return Shared pointer to the Shortcut if found, nullptr otherwise
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * auto shortcut = manager.Get(shortcut_id);
   * if (shortcut) {
   *     std::cout << "Found shortcut: " << shortcut->GetAccelerator() << std::endl;
   * }
   * ```
   */
  std::shared_ptr<Shortcut> Get(ShortcutId id);

  /**
   * @brief Get a shortcut by its accelerator string.
   *
   * Retrieves a previously registered shortcut using its accelerator string.
   *
   * @param accelerator The keyboard shortcut string to find
   * @return Shared pointer to the Shortcut if found, nullptr otherwise
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * auto shortcut = manager.Get("Ctrl+Shift+Q");
   * if (shortcut) {
   *     shortcut->SetEnabled(false);
   * }
   * ```
   */
  std::shared_ptr<Shortcut> Get(const std::string& accelerator);

  /**
   * @brief Get all managed shortcuts.
   *
   * Returns a vector containing all currently registered shortcut instances.
   * The returned vector is a snapshot of the current state and modifications
   * to it won't affect the internal shortcut registry.
   *
   * @return Vector of shared pointers to all Shortcut instances
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * auto all_shortcuts = manager.GetAll();
   * for (auto& shortcut : all_shortcuts) {
   *     std::cout << shortcut->GetAccelerator() << std::endl;
   * }
   * ```
   */
  std::vector<std::shared_ptr<Shortcut>> GetAll();

  /**
   * @brief Get shortcuts filtered by scope.
   *
   * Returns shortcuts that match the specified scope (global or application-local).
   *
   * @param scope The shortcut scope to filter by
   * @return Vector of shared pointers to matching Shortcut instances
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * auto global_shortcuts = manager.GetByScope(ShortcutScope::Global);
   * auto app_shortcuts = manager.GetByScope(ShortcutScope::Application);
   * ```
   */
  std::vector<std::shared_ptr<Shortcut>> GetByScope(ShortcutScope scope);

  /**
   * @brief Check if a specific accelerator is available for registration.
   *
   * Determines whether a keyboard shortcut string is available for use,
   * i.e., not already registered by this application or conflicting with
   * system shortcuts.
   *
   * @param accelerator The keyboard shortcut string to check
   * @return true if the accelerator is available, false if already in use
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * if (manager.IsAvailable("Ctrl+Shift+N")) {
   *     auto shortcut = manager.Register("Ctrl+Shift+N", callback);
   * } else {
   *     std::cout << "Shortcut already in use" << std::endl;
   * }
   * ```
   */
  bool IsAvailable(const std::string& accelerator);

  /**
   * @brief Validate an accelerator string format.
   *
   * Checks if the provided accelerator string follows the correct format
   * and contains valid key combinations. This is useful for validating
   * user input before attempting registration.
   *
   * @param accelerator The keyboard shortcut string to validate
   * @return true if the format is valid, false otherwise
   *
   * @example
   * ```cpp
   * if (manager.IsValidAccelerator("Ctrl+Shift+Q")) {
   *     // Valid format, safe to register
   * } else {
   *     std::cout << "Invalid shortcut format" << std::endl;
   * }
   * ```
   */
  bool IsValidAccelerator(const std::string& accelerator);

  /**
   * @brief Enable or disable shortcut processing.
   *
   * Allows temporarily disabling all shortcut processing without unregistering
   * shortcuts. When disabled, shortcuts will remain registered but won't trigger
   * their callbacks. This is useful for modal dialogs or when the application
   * needs to temporarily suppress shortcut handling.
   *
   * @param enabled true to enable shortcut processing, false to disable
   * @thread_safety This method is thread-safe
   *
   * @example
   * ```cpp
   * // Disable shortcuts during modal dialog
   * manager.SetEnabled(false);
   * ShowModalDialog();
   * manager.SetEnabled(true);  // Re-enable after dialog closes
   * ```
   */
  void SetEnabled(bool enabled);

  /**
   * @brief Check if shortcut processing is enabled.
   *
   * @return true if shortcut processing is enabled, false otherwise
   * @thread_safety This method is thread-safe
   */
  bool IsEnabled() const;

  /**
   * @brief Emit a shortcut activated event (internal use).
   *
   * Platform implementations should call this when a registered shortcut fires.
   */
  void EmitShortcutActivated(ShortcutId id, const std::string& accelerator);

  // Prevent copy construction and assignment to maintain singleton property
  ShortcutManager(const ShortcutManager&) = delete;
  ShortcutManager& operator=(const ShortcutManager&) = delete;
  ShortcutManager(ShortcutManager&&) = delete;
  ShortcutManager& operator=(ShortcutManager&&) = delete;

  /**
   * @brief Private implementation class using the PIMPL idiom.
   */
  class Impl {
   public:
    virtual ~Impl() = default;
    virtual bool IsSupported() = 0;
    // Legacy backends retain their registry-only preflight. OHOS additionally
    // checks canonical aliases and probes actual SDK reservations.
    virtual bool CanRegister(const std::string& /*accelerator*/) { return true; }
    virtual bool RegisterShortcut(const std::shared_ptr<Shortcut>& shortcut) = 0;
    // Interactive providers must return without waiting for user interaction.
    // Legacy providers use the existing atomic, synchronous registry path.
    using RegistrationCompletion = std::function<void(bool, const std::string&)>;
    virtual bool ShouldDeferRegistration(const std::shared_ptr<Shortcut>&) { return false; }
    virtual void RequestShortcutRegistration(const std::shared_ptr<Shortcut>& /*shortcut*/,
                                             std::shared_ptr<std::atomic<bool>> /*cancelled*/,
                                             RegistrationCompletion completion) {
      completion(false, "Interactive registration is unsupported");
    }
    // May arrive before RequestShortcutRegistration. Providers must check the
    // shared cancellation flag before starting and serialize start/cancel in
    // their own event context. No manager lock is held across either call.
    virtual void CancelShortcutRegistration(ShortcutId /*request_id*/) {}
    virtual std::optional<std::string> GetShortcutTriggerDescription(ShortcutId /*id*/) { return std::nullopt; }
    virtual bool UnregisterShortcut(const std::shared_ptr<Shortcut>& shortcut) = 0;
    virtual void SetupEventMonitoring() = 0;
    virtual void CleanupEventMonitoring() = 0;
  };

 protected:
  /**
   * @brief Called when the first listener is added.
   *
   * Starts platform-specific shortcut monitoring. This is called automatically
   * by the EventEmitter when transitioning from 0 to 1+ listeners.
   */
  void StartEventListening() override;

  /**
   * @brief Called when the last listener is removed.
   *
   * Stops platform-specific shortcut monitoring. This is called automatically
   * by the EventEmitter when transitioning from 1+ to 0 listeners.
   */
  void StopEventListening() override;

 private:
  friend class ShortcutManagerImpl;

  // Platform entry point; release delivery is not part of the public API.
  void EmitShortcutReleased(ShortcutId id, const std::string& accelerator);
  void UpdateTriggerDescription(ShortcutId id, const std::string& description);

  /**
   * @brief Private constructor to enforce singleton pattern.
   *
   * Initializes the ShortcutManager instance and sets up initial state.
   */
  ShortcutManager();

  /**
   * @brief Pointer to the private implementation instance.
   */
  std::unique_ptr<Impl> pimpl_;

  /**
   * @brief Container for storing active shortcut instances by ID.
   *
   * Maps shortcut IDs to their corresponding Shortcut instances
   * for efficient lookup and management.
   */
  std::unordered_map<ShortcutId, std::shared_ptr<Shortcut>> shortcuts_by_id_;

  /**
   * @brief Container for storing active shortcut instances by accelerator.
   *
   * Maps accelerator strings to their corresponding Shortcut instances
   * for efficient lookup by keyboard combination.
   */
  std::unordered_map<std::string, std::shared_ptr<Shortcut>> shortcuts_by_accelerator_;

  struct PendingRegistration {
    explicit PendingRegistration(std::shared_ptr<Shortcut> value) : shortcut(std::move(value)) {}
    std::shared_ptr<Shortcut> shortcut;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    std::atomic<bool> completed{false};
  };
  std::unordered_map<ShortcutId, std::shared_ptr<PendingRegistration>> pending_registrations_;
  bool HasPendingAcceleratorUnlocked(const std::string& accelerator) const;
  void CompleteRegistration(const std::shared_ptr<PendingRegistration>& request,
                            bool success, const std::string& reason);

  /**
   * @brief ID generator for creating unique shortcut identifiers.
   *
   * Maintains the next available ID to assign to newly created shortcuts.
   * This ensures each shortcut has a unique identifier.
   */
  ShortcutId next_shortcut_id_;

  /**
   * @brief Flag indicating whether shortcut processing is enabled.
   */
  bool enabled_;

  /**
   * @brief Mutex for thread-safe operations.
   *
   * Protects access to internal data structures to ensure thread safety
   * when multiple threads access the ShortcutManager simultaneously.
   */
  mutable std::mutex mutex_;

  /**
   * @brief Availability check for callers that already hold mutex_.
   *
   * The public IsAvailable() acquires mutex_, so calling it from an already-
   * locked section self-deadlocks (mutex_ is not recursive). Register() needs
   * the check and the subsequent insert to be one atomic step, so it must not
   * drop the lock in between — it uses this helper instead.
   *
   * @param accelerator The accelerator string to check.
   * @return true if no shortcut is currently registered for this accelerator.
   */
  bool IsAvailableUnlocked(const std::string& accelerator) const;
};

}  // namespace nativeapi
