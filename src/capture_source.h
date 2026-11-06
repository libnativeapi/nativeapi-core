#pragma once

#include <memory>
#include <string>

namespace nativeapi {
class Display;
class Window;
namespace detail { struct CaptureAccess; }

/** @brief The kind of content bound to a capture source. */
enum class CaptureSourceType {
  /** No live target. */
  None,
  /** One display. */
  Display,
  /** One application window. */
  Window,
  /** A valid system-selected source whose kind is not disclosed by the platform. */
  Unknown,
};

/**
 * @brief A capture target and its optional system authorization session.
 *
 * Sources are shared identity objects, not persistent permission tokens. A source
 * accepts only one in-flight screenshot or recording. Destroying the last reference
 * releases its authorization resources. A disappearing target is never replaced.
 * Factories only describe targets; permission may be requested during capture.
 * Obtain other applications' windows through ScreenCapturer::SelectSource().
 *
 * @note Platform availability:
 * - macOS: ⚠️ ScreenCaptureKit on macOS 14+; system permission required
 * - Windows: ⚠️ Windows Graphics Capture on Windows 10 2004+; desktop session required
 * - Linux: ❌ Not implemented - Factories return nullptr
 * - Android: ❌ Not implemented - Factories return nullptr
 * - iOS: ❌ Not implemented - Factories return nullptr
 * - OpenHarmony: ❌ Not implemented - Factories return nullptr
 *
 * @code
 * auto source = CaptureSource::FromDisplay(DisplayManager::GetInstance().GetPrimary());
 * @endcode
 */
class CaptureSource {
 public:
  /** @brief Describes a display without prompting or starting capture.
   * @param display Target display.
   * @return nullptr for null, unavailable or unsupported targets. */
  static std::shared_ptr<CaptureSource> FromDisplay(std::shared_ptr<Display> display);
  /** @brief Describes a window without prompting or starting capture.
   * @param window Target window.
   * @return nullptr for null, unavailable or unsupported targets. */
  static std::shared_ptr<CaptureSource> FromWindow(std::shared_ptr<Window> window);

  ~CaptureSource();
  CaptureSource(const CaptureSource&) = delete;
  CaptureSource& operator=(const CaptureSource&) = delete;
  CaptureSource(CaptureSource&&) = delete;
  CaptureSource& operator=(CaptureSource&&) = delete;

  /** @brief Gets the target kind, or None when known to be unavailable. */
  CaptureSourceType GetType() const;
  /** @brief Gets the current display name, or an empty string when unavailable. */
  std::string GetName() const;

 private:
  CaptureSource();
  friend struct detail::CaptureAccess;
  class Impl;
  std::unique_ptr<Impl> pimpl_;
};
}  // namespace nativeapi
