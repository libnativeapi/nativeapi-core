#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "foundation/event.h"
#include "foundation/event_emitter.h"
#include "screen_capturer.h"

namespace nativeapi {
/** @brief State of one recording attempt; terminal recorders cannot restart. */
enum class ScreenRecordingState {
  /** No request has been accepted. */
  Idle,
  /** Starting capture and the encoder. */
  Starting,
  /** At least one frame has been accepted by the encoder. */
  Recording,
  /** Finalizing output or discarding a cancelled recording. */
  Stopping,
  /** The finished file has been published. */
  Completed,
  /** Cancellation and cleanup completed. */
  Cancelled,
  /** Capture, encoding, publication or cleanup failed. */
  Failed,
};

/** @brief Configuration for silent SDR H.264 video in an MP4 file. */
struct ScreenRecordingOptions {
  /** Cursor and source-local physical-pixel crop configuration. */
  ScreenCaptureOptions capture;
  /** Finite maximum sampling rate in frames per second, in [0.1, 240]. */
  double frame_rate = 30.0;
  /** Target bits per second, at most UINT32_MAX; zero selects an encoder default. */
  size_t video_bitrate = 0;
};

/** @brief Base of recording notifications, emitted by the individual recorder. */
class ScreenRecordingEvent : public Event {
 public:
  std::string GetTypeName() const override { return "ScreenRecordingEvent"; }
};
/** @brief The first video frame has been accepted. */
class ScreenRecordingStartedEvent : public ScreenRecordingEvent {
 public:
  std::string GetTypeName() const override { return "ScreenRecordingStartedEvent"; }
};
/** @brief Encoding, file closing and non-overwriting publication succeeded. */
class ScreenRecordingCompletedEvent : public ScreenRecordingEvent {
 public:
  ScreenRecordingCompletedEvent(const std::string& file_path, double duration)
      : file_path_(file_path), duration_(duration) {}
  /** @brief Gets the finalized absolute file path. */
  std::string GetFilePath() const { return file_path_; }
  /** @brief Gets the media duration in seconds, excluding authorization time. */
  double GetDuration() const { return duration_; }
  std::string GetTypeName() const override { return "ScreenRecordingCompletedEvent"; }
 private:
  std::string file_path_;
  double duration_;
};
/** @brief The recording was discarded and temporary output cleaned up. */
class ScreenRecordingCancelledEvent : public ScreenRecordingEvent {
 public:
  std::string GetTypeName() const override { return "ScreenRecordingCancelledEvent"; }
};
/** @brief A recording failed; diagnostics are not stable machine-readable codes. */
class ScreenRecordingFailedEvent : public ScreenRecordingEvent {
 public:
  explicit ScreenRecordingFailedEvent(const std::string& message) : message_(message) {}
  /** @brief Gets a human-readable diagnostic, including cleanup failure when relevant. */
  std::string GetMessage() const { return message_; }
  std::string GetTypeName() const override { return "ScreenRecordingFailedEvent"; }
 private:
  std::string message_;
};

/**
 * @brief Records one source as silent SDR H.264/MP4 using a native capture backend.
 *
 * Calls use the host UI thread. Start/Stop/Cancel report acceptance, not completion.
 * Keep the recorder alive until a terminal event. Destruction discards unfinished
 * output and suppresses further events; it never implicitly saves. No listener is
 * required for recording to progress. Output dimensions are fixed by the first frame;
 * resized content is fitted with black bars. Requested crops must remain in bounds.
 *
 * @note Platform availability:
 * - macOS: ⚠️ macOS 14+ ScreenCaptureKit and AVAssetWriter; permission required
 * - Windows: ⚠️ Windows 10 2004+ Graphics Capture and Media Foundation H.264 encoder
 * - Linux: ❌ Not implemented - Start returns false
 * - Android: ❌ Not implemented - Start returns false
 * - iOS: ❌ Not implemented - Start returns false
 * - OpenHarmony: ❌ Not implemented - Start returns false
 *
 * @code
 * auto recorder = std::make_shared<ScreenRecorder>();
 * auto source = CaptureSource::FromDisplay(DisplayManager::GetInstance().GetPrimary());
 * bool accepted = recorder->Start(source, "/tmp/recording.mp4", {});
 * // Retain recorder; after StartedEvent call Stop and await a terminal event.
 * @endcode
 */
class ScreenRecorder : public EventEmitter<ScreenRecordingEvent> {
 public:
  /** @brief Checks for capture and H.264/MP4 encoding support, without prompting. */
  static bool IsSupported();
  ScreenRecorder();
  ~ScreenRecorder() override;
  ScreenRecorder(const ScreenRecorder&) = delete;
  ScreenRecorder& operator=(const ScreenRecorder&) = delete;
  ScreenRecorder(ScreenRecorder&&) = delete;
  ScreenRecorder& operator=(ScreenRecorder&&) = delete;

  /** @brief Starts one recording asynchronously from Idle.
   * @param source Target retained until termination.
   * @param file_path Absolute UTF-8 local .mp4 path; existing entries are never replaced.
   * @param options Sampling, bitrate, crop and cursor configuration.
   * @return false on synchronous rejection; state remains Idle and no event is emitted.
   * true means Starting; later failures are reported by ScreenRecordingFailedEvent. */
  bool Start(std::shared_ptr<CaptureSource> source, const std::string& file_path,
             const ScreenRecordingOptions& options);
  /** @brief Stops a Recording session and asynchronously publishes the finished file.
   * @return false outside Recording. Await CompletedEvent or FailedEvent after true. */
  bool Stop();
  /** @brief Discards a Starting or Recording session asynchronously.
   * @return false in other states, including Stopping. Await CancelledEvent or FailedEvent. */
  bool Cancel();
  /** @brief Gets the current recording state. */
  ScreenRecordingState GetState() const;
  /** @brief Checks for Starting, Recording or Stopping. */
  bool IsActive() const;
  /** @brief Gets media duration in seconds; zero before the first frame.
   * On failure/cancellation this is diagnostic, not a promise of a saved file. */
  double GetDuration() const;
  /** @brief Gets the requested absolute path, empty before acceptance.
   * Only Completed guarantees that a finalized file was published there. */
  std::string GetFilePath() const;

 private:
  class Impl;
  std::unique_ptr<Impl> pimpl_;
};
}  // namespace nativeapi
