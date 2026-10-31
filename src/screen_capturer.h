#pragma once

#include <functional>
#include <memory>

#include "capture_source.h"
#include "foundation/geometry.h"

namespace nativeapi {
class Image;
class Window;

/** @brief Allowed source types and the owner of the system selection UI. */
struct CaptureSourceSelectionOptions {
  /** Allow a display; at least one source type must be allowed. */
  bool allow_displays = true;
  /** Allow a window, including other applications' windows. */
  bool allow_windows = true;
  /** Owner of the picker, not the captured window; nullptr uses the current window. */
  std::shared_ptr<Window> parent_window;
};

/** @brief Shared screenshot and recording configuration, copied when submitted. */
struct ScreenCaptureOptions {
  /** Include the pointer in the captured pixels; unsupported modes fail. */
  bool include_cursor = false;
  /** Apply crop; when false the rectangle is ignored. */
  bool crop_enabled = false;
  /** Source-local physical pixels, top-left origin, not global logical coordinates.
   * All components must be finite integers, x/y >= 0, width/height > 0, in bounds. */
  Rectangle crop = {0, 0, 0, 0};
};

/**
 * @brief Asynchronously selects capture targets and obtains independent SDR images.
 *
 * Calls use the host UI thread and require a running event loop. Completions run
 * on a later UI-loop turn exactly once. Without a dispatcher, failure is delivered
 * synchronously on the caller thread. An empty callback does nothing. Runtime
 * shutdown can revoke pending deliveries. Queries never prompt for permission.
 *
 * @note Platform availability:
 * - macOS: ⚠️ ScreenCaptureKit on macOS 14+; selection uses the system sharing picker
 * - Windows: ⚠️ Windows 10 2004+; picker needs an owner window and both source types
 *   enabled; system-selected sources have type Unknown
 * - Linux: ❌ Not implemented - Queries return false and operations fail
 * - Android: ❌ Not implemented - Queries return false and operations fail
 * - iOS: ❌ Not implemented - Queries return false and operations fail
 * - OpenHarmony: ❌ Not implemented - Queries return false and operations fail
 *
 * @code
 * auto source = CaptureSource::FromDisplay(DisplayManager::GetInstance().GetPrimary());
 * ScreenCapturer::Capture(source, {}, [](bool ok, std::shared_ptr<Image> image) {
 *   if (ok) image->SaveToFile("/tmp/screenshot.png");
 * });
 * @endcode
 */
class ScreenCapturer {
 public:
  /** @brief Checks for a usable capture backend, not permission or target validity. */
  static bool IsSupported();
  /** @brief Checks for an available interactive source selection backend. */
  static bool IsSourceSelectionSupported();
  /** @brief Checks whether a target kind can be specified without the picker.
   * @param type Kind to test; None always returns false. */
  static bool IsDirectCaptureSupported(CaptureSourceType type);

  /** @brief Selects exactly one source in system UI; only one picker may be pending.
   * @param options Allowed types and picker owner.
   * @param callback (true, source) on selection, (true, nullptr) on user cancellation,
   * (false, nullptr) on invalid input, denial, unsupported operation or failure. */
  static void SelectSource(
      const CaptureSourceSelectionOptions& options,
      std::function<void(bool, std::shared_ptr<CaptureSource>)> callback);

  /** @brief Captures a new frame, optionally cropping it, without saving a file.
   * @param source Target retained until completion; null or busy sources fail.
   * @param options Physical-pixel crop and cursor policy.
   * @param callback (true, independent image) on success, (false, nullptr) on failure.
   * A returned image remains usable after the source and native frame are released. */
  static void Capture(std::shared_ptr<CaptureSource> source,
                      const ScreenCaptureOptions& options,
                      std::function<void(bool, std::shared_ptr<Image>)> callback);

 private:
  ScreenCapturer() = delete;
};
}  // namespace nativeapi
