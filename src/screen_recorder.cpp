#include "screen_recorder.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include "foundation/dispatcher.h"
#include "screen_capture_backend.h"

namespace nativeapi {
namespace {
using detail::CaptureClock;
struct RecordingSession {
  std::mutex mutex;
  ScreenRecordingState state = ScreenRecordingState::Idle;
  std::atomic<bool> cancel{false};
  std::atomic<bool> stop{false};
  bool destroyed = false;
  double duration = 0;
  std::string path;
  CaptureClock::time_point stopped_at;
  std::function<void(ScreenRecordingState, std::string)> notify;
};

void Notify(const std::shared_ptr<RecordingSession>& session, ScreenRecordingState state,
            const std::string& message = {}) {
  auto notify = session->notify;
  RunOnMainThread([notify, state, message] { notify(state, message); });
}

void Record(const std::shared_ptr<RecordingSession>& session,
            std::shared_ptr<detail::CaptureLease> lease, ScreenRecordingOptions options) {
  detail::CaptureOutputFile output;
  std::unique_ptr<detail::CaptureStream> stream;
  std::unique_ptr<detail::CaptureEncoder> encoder;
  std::string error;
  bool have_frame = false;
  CaptureClock::time_point first_time;
  double last_time = -1;
  int width = 0, height = 0;
  auto finish_state = ScreenRecordingState::Failed;
  try {
    if (!session->cancel && output.Create(session->path, error))
      stream = lease->target->Open(options.capture, options.frame_rate, session->cancel, error);
    if (!stream && error.empty() && !session->cancel) error = "Cannot open the capture stream";
    auto deadline = CaptureClock::now() + std::chrono::seconds(15);
    while (stream && error.empty() && !session->cancel && !session->stop) {
      auto frame = stream->NextFrame(100, error);
      if (!frame) {
        if (!have_frame && CaptureClock::now() >= deadline) error = "Timed out waiting for the first frame";
        continue;
      }
      double seconds = have_frame ? std::chrono::duration<double>(frame->time - first_time).count() : 0;
      if (have_frame && seconds < last_time + 1.0 / options.frame_rate) continue;
      detail::CaptureFrame cropped, fitted;
      if (!detail::CropCaptureFrame(*frame, options.capture, cropped)) {
        error = "Capture crop is outside the current source image"; break;
      }
      if (!encoder) {
        width = (cropped.width + 1) & ~1;
        height = (cropped.height + 1) & ~1;
        encoder = detail::CreateCaptureEncoder(output.TemporaryPath(), width, height, options, error);
        if (!encoder) { if (error.empty()) error = "Cannot create an H.264 encoder"; break; }
      }
      if (!detail::FitCaptureFrame(cropped, width, height, fitted) ||
          !encoder->Append(fitted, seconds, error)) {
        if (error.empty()) error = "Cannot encode a video frame";
        break;
      }
      bool started = false;
      {
        std::lock_guard<std::mutex> lock(session->mutex);
        if (session->cancel) break;
        if (!have_frame) {
          first_time = frame->time;
          session->state = ScreenRecordingState::Recording;
          started = true;
        }
        have_frame = true;
        last_time = seconds;
        session->duration = seconds;
      }
      if (started) Notify(session, ScreenRecordingState::Recording);
    }
    if (stream) {
      std::string stop_error;
      if (!stream->Stop(stop_error)) {
        if (error.empty()) error = stop_error.empty() ? "Cannot stop capture" : stop_error;
      }
      stream.reset();
    }
    if (session->cancel && error.empty()) {
      // Only the backend's explicit cancellation path has no error. Do not
      // hide an independent encoder/write failure racing with Cancel().
      encoder.reset();
      finish_state = output.Discard(error) ? ScreenRecordingState::Cancelled : ScreenRecordingState::Failed;
    } else if (error.empty() && have_frame && session->stop) {
      double duration;
      {
        std::lock_guard<std::mutex> lock(session->mutex);
        duration = std::max(last_time + 0.001,
            std::chrono::duration<double>(session->stopped_at - first_time).count());
      }
      if (encoder->Finish(duration, error)) {
        encoder.reset();
        // The publication and destruction decisions share this lock. A dead
        // recorder cannot publish, and already published files are never removed.
        std::lock_guard<std::mutex> lock(session->mutex);
        if (!session->destroyed && !session->cancel && output.Publish(error)) {
          session->duration = duration;
          finish_state = ScreenRecordingState::Completed;
          session->state = finish_state;
        }
      }
    }
  } catch (const std::exception& e) { error = e.what(); }
  catch (...) { error = "Unexpected screen recording failure"; }
  if (stream) { std::string ignored; stream->Stop(ignored); }
  encoder.reset();
  std::string cleanup_error;
  if (!output.Discard(cleanup_error)) {
    if (!error.empty()) error += "; ";
    error += cleanup_error;
    finish_state = ScreenRecordingState::Failed;
  }
  lease.reset();
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroyed) return;
    session->state = finish_state;
  }
  if (finish_state == ScreenRecordingState::Failed && error.empty()) error = "Screen recording failed";
  Notify(session, finish_state, error);
}
}  // namespace

class ScreenRecorder::Impl {
 public:
  std::shared_ptr<RecordingSession> session = std::make_shared<RecordingSession>();
};
ScreenRecorder::ScreenRecorder() : pimpl_(std::make_unique<Impl>()) {
  std::function<void(ScreenRecordingState, std::string)> notify =
      [this](ScreenRecordingState state, std::string message) {
        switch (state) {
          case ScreenRecordingState::Recording: Emit<ScreenRecordingStartedEvent>(); break;
          case ScreenRecordingState::Completed:
            Emit<ScreenRecordingCompletedEvent>(GetFilePath(), GetDuration()); break;
          case ScreenRecordingState::Cancelled: Emit<ScreenRecordingCancelledEvent>(); break;
          case ScreenRecordingState::Failed: Emit<ScreenRecordingFailedEvent>(message); break;
          default: break;
        }
      };
  pimpl_->session->notify = CreateGuardedCallback(std::move(notify));
}
ScreenRecorder::~ScreenRecorder() {
  ShutdownEmitter();
  auto session = pimpl_->session;
  std::lock_guard<std::mutex> lock(session->mutex);
  session->destroyed = true;
  session->cancel = true;
}
bool ScreenRecorder::IsSupported() {
  try { return ScreenCapturer::IsSupported() && detail::CaptureEncodingSupported(); }
  catch (...) { return false; }
}
bool ScreenRecorder::Start(std::shared_ptr<CaptureSource> source, const std::string& path,
                           const ScreenRecordingOptions& options) {
  try {
    if (!IsSupported() || !detail::ValidRecordingPath(path) ||
        !detail::ValidRecordingOptions(options)) return false;
    auto session = pimpl_->session;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->state != ScreenRecordingState::Idle) return false;
    auto lease = std::make_shared<detail::CaptureLease>(std::move(source));
    if (!lease->target) return false;
    session->path = path;
    session->state = ScreenRecordingState::Starting;
    try { std::thread(Record, session, std::move(lease), options).detach(); }
    catch (...) { session->state = ScreenRecordingState::Idle; session->path.clear(); return false; }
    return true;
  } catch (...) { return false; }
}
bool ScreenRecorder::Stop() {
  auto session = pimpl_->session;
  std::lock_guard<std::mutex> lock(session->mutex);
  if (session->state != ScreenRecordingState::Recording) return false;
  session->state = ScreenRecordingState::Stopping;
  session->stopped_at = CaptureClock::now();
  session->stop = true;
  return true;
}
bool ScreenRecorder::Cancel() {
  auto session = pimpl_->session;
  std::lock_guard<std::mutex> lock(session->mutex);
  if (session->state != ScreenRecordingState::Starting &&
      session->state != ScreenRecordingState::Recording) return false;
  session->state = ScreenRecordingState::Stopping;
  session->cancel = true;
  return true;
}
ScreenRecordingState ScreenRecorder::GetState() const {
  std::lock_guard<std::mutex> lock(pimpl_->session->mutex);
  return pimpl_->session->state;
}
bool ScreenRecorder::IsActive() const {
  auto state = GetState();
  return state == ScreenRecordingState::Starting || state == ScreenRecordingState::Recording ||
      state == ScreenRecordingState::Stopping;
}
double ScreenRecorder::GetDuration() const {
  std::lock_guard<std::mutex> lock(pimpl_->session->mutex);
  return pimpl_->session->duration;
}
std::string ScreenRecorder::GetFilePath() const {
  std::lock_guard<std::mutex> lock(pimpl_->session->mutex);
  return pimpl_->session->path;
}
}  // namespace nativeapi
