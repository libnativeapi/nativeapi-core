#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "foundation/event.h"
#include "foundation/event_emitter.h"

namespace nativeapi {

namespace detail {
class ProtocolHandlerDispatch;
}  // namespace detail

/** @brief Base class for incoming custom protocol (deep link) notifications. */
class ProtocolHandlerEvent : public Event {
 public:
  std::string GetTypeName() const override { return "ProtocolHandlerEvent"; }
};

/** @brief A URL that targets one of the application's schemes arrived while it was running. */
class ProtocolUrlReceivedEvent : public ProtocolHandlerEvent {
 public:
  explicit ProtocolUrlReceivedEvent(std::string url) : url_(std::move(url)) {}

  /** @brief Gets the complete URL as the system delivered it, scheme included. */
  std::string GetUrl() const { return url_; }

  std::string GetTypeName() const override { return "ProtocolUrlReceivedEvent"; }

 private:
  std::string url_;
};

/**
 * @brief Receives URLs of the application's custom schemes (deep links).
 *
 * This is the inbound side of URL handling; UrlOpener sends links out. The URL
 * that launched the process is kept as the initial URL and is not repeated as an
 * event, unless it arrived after GetInitialUrl() had already returned nullopt: the
 * system can deliver it after a binding's startup code ran, and then it is emitted
 * as well. Each URL delivered to the running process afterwards is emitted as a
 * ProtocolUrlReceivedEvent. URLs that arrive while nothing listens are queued (at
 * most 32, oldest dropped) and delivered to the first listener, so a listener added
 * during startup does not miss a link. Events are emitted asynchronously on the UI
 * thread.
 *
 * Launch URLs arrive before most bindings load this library. Hosts have to capture
 * them early: macOS and native C++ hosts call GetInstance() before the application
 * finishes launching; Flutter apps depend on the protocol_handler plugin (leanflutter).
 * The scheme itself is declared to the system by the application package (Info.plist
 * CFBundleURLTypes, an Android intent filter, an MSIX uap:Protocol), or on unpackaged
 * Windows apps written by Register().
 *
 * @note Platform availability:
 * - macOS: ✅ Fully supported - kAEGetURL Apple events; launch URL needs GetInstance()
 *   before applicationDidFinishLaunching
 * - Windows: ✅ Fully supported - HKCU registration, command-line launch URL; a second
 *   instance forwards its URL with WM_COPYDATA (specs/protocol-handler.md)
 * - Linux: ❌ Not applicable - IsSupported() returns false
 * - Android: ⚠️ Host attachment required - VIEW intents forwarded by the Flutter plugin
 *   or a host bridge; IsSupported() is false until then
 * - iOS: ⚠️ Host attachment required - openURL / scene URL contexts forwarded by the
 *   Flutter plugin or a host bridge
 * - OpenHarmony: ❌ Not applicable - IsSupported() returns false
 *
 * @code
 * auto& handler = ProtocolHandler::GetInstance();
 * handler.Register("myapp");
 * if (auto url = handler.GetInitialUrl()) {
 *   // The app was launched by *url.
 * }
 * handler.AddListener<ProtocolUrlReceivedEvent>(
 *     [](const ProtocolUrlReceivedEvent& event) { auto url = event.GetUrl(); });
 * @endcode
 */
class ProtocolHandler : public EventEmitter<ProtocolHandlerEvent> {
 public:
  /** @brief Gets the process-wide handler and starts capturing URLs; callers do not own it. */
  static ProtocolHandler& GetInstance();
  /** @brief Checks if this process can receive custom protocol URLs. */
  static bool IsSupported();

  virtual ~ProtocolHandler();
  ProtocolHandler(const ProtocolHandler&) = delete;
  ProtocolHandler& operator=(const ProtocolHandler&) = delete;
  ProtocolHandler(ProtocolHandler&&) = delete;
  ProtocolHandler& operator=(ProtocolHandler&&) = delete;

  /**
   * @brief Registers this application as the handler of a URL scheme.
   *
   * @param scheme The scheme without "://", e.g. "myapp". It must start with a
   * letter followed by letters, digits, "+", "-" or "."; it is matched case-insensitively.
   * @return true if URLs of the scheme are routed to this application after the
   * call; false for an invalid scheme, an undeclared scheme, a write failure, or an
   * unsupported platform.
   *
   * @note Platform availability:
   * - macOS: ⚠️ Declaration check - true when CFBundleURLTypes declares the scheme;
   *   re-registers the bundle with LaunchServices, never changes the user's default
   * - Windows: ✅ Fully supported - writes HKCU\Software\Classes\<scheme> pointing at
   *   this executable; returns false in an MSIX package, whose manifest declares it
   * - Linux: ❌ Not applicable - Always returns false
   * - Android: ⚠️ Declaration check - true when an activity of this package has a
   *   VIEW intent filter for the scheme
   * - iOS: ⚠️ Declaration check - true when CFBundleURLTypes declares the scheme
   * - OpenHarmony: ❌ Not applicable - Always returns false
   */
  bool Register(const std::string& scheme);

  /**
   * @brief Gets the URL that launched the application.
   * @return The launch URL, or nullopt when the application was not launched by a
   * URL, the host did not capture it, or it has not arrived yet; a launch URL that
   * arrives after a nullopt answer is also emitted as a ProtocolUrlReceivedEvent.
   * @see ProtocolHandler for platform availability.
   */
  std::optional<std::string> GetInitialUrl() const;

 protected:
  void StartEventListening() override;
  void StopEventListening() override;

 private:
  friend class detail::ProtocolHandlerDispatch;
  ProtocolHandler();
  class Impl;
  std::unique_ptr<Impl> pimpl_;
};

}  // namespace nativeapi
