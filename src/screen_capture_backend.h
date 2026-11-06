#pragma once

// Private platform seam; deliberately absent from API_HEADERS.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "screen_recorder.h"

namespace nativeapi::detail {
using CaptureClock = std::chrono::steady_clock;

// Owned, tightly packed, top-down SDR BGRA pixels. No borrowed native surfaces.
struct CaptureFrame {
  int width = 0;
  int height = 0;
  CaptureClock::time_point time;
  std::vector<uint8_t> pixels;
};

class CaptureStream {
 public:
  virtual ~CaptureStream() = default;
  // Called from one worker. nullptr + empty error is a normal idle timeout.
  virtual std::shared_ptr<CaptureFrame> NextFrame(int timeout_ms, std::string& error) = 0;
  virtual bool Stop(std::string& error) = 0;
};

class CaptureTarget {
 public:
  virtual ~CaptureTarget() = default;
  virtual CaptureSourceType Type() const = 0;
  virtual std::string Name() const = 0;
  // Worker-only; must honor cancellation and bound native startup waits.
  // Cancellation returns nullptr with an empty error; independent failures
  // keep their diagnostics even when cancellation is concurrently requested.
  virtual std::unique_ptr<CaptureStream> Open(const ScreenCaptureOptions& options,
      double frame_rate, const std::atomic<bool>& cancel, std::string& error) = 0;
};

class CaptureEncoder {
 public:
  virtual ~CaptureEncoder() = default;
  virtual bool Append(const CaptureFrame& frame, double seconds, std::string& error) = 0;
  // Extends the final sample through end_seconds, including a static trailing scene.
  virtual bool Finish(double end_seconds, std::string& error) = 0;
};

bool CaptureBackendSupported();
bool CapturePickerSupported();
bool CaptureEncodingSupported();
std::shared_ptr<CaptureTarget> CaptureDisplayTarget(std::shared_ptr<Display> display);
std::shared_ptr<CaptureTarget> CaptureWindowTarget(std::shared_ptr<Window> window);
void SelectCaptureTarget(const CaptureSourceSelectionOptions& options,
                        std::function<void(bool, std::shared_ptr<CaptureTarget>)> callback);
std::shared_ptr<Image> CaptureFrameImage(const CaptureFrame& frame);
std::unique_ptr<CaptureEncoder> CreateCaptureEncoder(const std::string& temporary_path,
    int width, int height, const ScreenRecordingOptions& options, std::string& error);

// The shared source state is separate from platform resources, and outlives tasks.
struct CaptureAccess {
  static std::shared_ptr<CaptureSource> Wrap(std::shared_ptr<CaptureTarget> target);
  static std::shared_ptr<CaptureTarget> Acquire(const std::shared_ptr<CaptureSource>& source);
  static void Release(const std::shared_ptr<CaptureSource>& source);
};
class CaptureLease {
 public:
  explicit CaptureLease(std::shared_ptr<CaptureSource> source);
  ~CaptureLease();
  CaptureLease(const CaptureLease&) = delete;
  CaptureLease& operator=(const CaptureLease&) = delete;
  std::shared_ptr<CaptureTarget> target;
 private:
  std::shared_ptr<CaptureSource> source_;
};

bool ValidCaptureOptions(const ScreenCaptureOptions& options);
bool ValidRecordingOptions(const ScreenRecordingOptions& options);
bool ValidRecordingPath(const std::string& path);
bool CropCaptureFrame(const CaptureFrame& input, const ScreenCaptureOptions& options,
                      CaptureFrame& output);
bool FitCaptureFrame(const CaptureFrame& input, int width, int height, CaptureFrame& output);

// Exclusively owns a temporary file; publication never overwrites an existing entry.
class CaptureOutputFile {
 public:
  ~CaptureOutputFile();
  bool Create(const std::string& destination, std::string& error);
  bool Publish(std::string& error);
  bool Discard(std::string& error);
  const std::string& TemporaryPath() const { return temporary_; }
 private:
  std::string destination_;
  std::string temporary_;
  std::string directory_;
};
}  // namespace nativeapi::detail
