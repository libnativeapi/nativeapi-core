// Shared contracts use a deterministic backend; desktop smoke tests exercise codecs.
#include "../src/screen_capture_backend.h"
#include "../src/foundation/dispatcher.h"
#include "../src/image.h"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (0)
using namespace nativeapi;
using namespace nativeapi::detail;
namespace {
std::atomic<bool> fail_open{false}, wait_open{false}, fail_encode{false};
std::atomic<bool> wait_encode{false}, entered_encode{false};
std::atomic<int> opened{0}, stopped{0};
class FakeStream : public CaptureStream {
 public:
  bool first = true;
  std::shared_ptr<CaptureFrame> NextFrame(int, std::string&) override {
    if (!first) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); return nullptr; }
    first = false;
    auto frame = std::make_shared<CaptureFrame>();
    frame->width = 3; frame->height = 3; frame->time = CaptureClock::now();
    frame->pixels.assign(36, 255);
    return frame;
  }
  bool Stop(std::string&) override { ++stopped; return true; }
};
class FakeTarget : public CaptureTarget {
 public:
  CaptureSourceType Type() const override { return CaptureSourceType::Display; }
  std::string Name() const override { return "Test display"; }
  std::unique_ptr<CaptureStream> Open(const ScreenCaptureOptions&, double,
      const std::atomic<bool>& cancel, std::string& error) override {
    ++opened;
    while (wait_open && !cancel) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (fail_open) { error = "Test startup failure"; return nullptr; }
    if (cancel) return nullptr;
    return std::make_unique<FakeStream>();
  }
};
class FakeEncoder : public CaptureEncoder {
 public:
  explicit FakeEncoder(const std::string& path) : output(std::filesystem::u8path(path), std::ios::binary) {}
  std::ofstream output;
  bool Append(const CaptureFrame&, double, std::string& error) override {
    entered_encode = true;
    while (wait_encode) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (fail_encode) { error = "Test encode failure"; return false; }
    output << "frame"; return bool(output);
  }
  bool Finish(double, std::string&) override { output << "finished"; output.close(); return true; }
};
template <typename F> void Until(F predicate) {
  auto deadline = CaptureClock::now() + std::chrono::seconds(5);
  while (!predicate() && CaptureClock::now() < deadline) RunMainThreadLoopFor(5);
  CHECK(predicate());
}
std::shared_ptr<CaptureSource> Source() { return CaptureAccess::Wrap(std::make_shared<FakeTarget>()); }
}  // namespace

namespace nativeapi::detail {
bool CaptureBackendSupported() { return true; }
bool CapturePickerSupported() { return true; }
bool CaptureEncodingSupported() { return true; }
std::shared_ptr<CaptureTarget> CaptureDisplayTarget(std::shared_ptr<Display>) { return nullptr; }
std::shared_ptr<CaptureTarget> CaptureWindowTarget(std::shared_ptr<Window>) { return nullptr; }
void SelectCaptureTarget(const CaptureSourceSelectionOptions&,
    std::function<void(bool, std::shared_ptr<CaptureTarget>)> callback) { callback(true, nullptr); }
std::shared_ptr<Image> CaptureFrameImage(const CaptureFrame&) { return nullptr; }
std::unique_ptr<CaptureEncoder> CreateCaptureEncoder(const std::string& path, int, int,
    const ScreenRecordingOptions&, std::string&) { return std::make_unique<FakeEncoder>(path); }
}  // namespace nativeapi::detail

int main() {
  try {
    SetMainThread();
    auto directory = std::filesystem::temp_directory_path() /
        ("nativeapi-capture-test-" + std::to_string(CaptureClock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    auto path = [&](const char* name) { return (directory / name).u8string(); };
    ScreenCaptureOptions crop;
    crop.crop_enabled = true;
    crop.crop = {0, 0, 2, 2};
    CHECK(ValidCaptureOptions(crop));
    crop.crop.width = .5; CHECK(!ValidCaptureOptions(crop));
    crop.crop.width = NAN; CHECK(!ValidCaptureOptions(crop));
    crop.crop = {0, 0, 4, 2};
    CaptureFrame input, output;
    input.width = 3; input.height = 3; input.pixels.assign(36, 255);
    CHECK(!CropCaptureFrame(input, crop, output));
    CHECK(FitCaptureFrame(input, 4, 4, output));
    CHECK(output.pixels[0] == 255 && output.pixels[12] == 0 && output.pixels[15] == 255);
    CHECK(!ValidRecordingPath("relative.mp4"));
    CHECK(!ValidRecordingPath(path("video.png")));
    CHECK(!ValidRecordingPath(path("video.mp4") + std::string(1, '\0')));

    { CaptureOutputFile file; std::string error;
      CHECK(file.Create(path("race.mp4"), error));
      std::ofstream(std::filesystem::u8path(file.TemporaryPath())) << "new";
      std::ofstream(std::filesystem::u8path(path("race.mp4"))) << "original";
      CHECK(!file.Publish(error)); CHECK(file.Discard(error));
      std::ifstream saved(std::filesystem::u8path(path("race.mp4"))); std::string text; saved >> text;
      CHECK(text == "original");
    }

    bool selected = false, inline_call = true;
    ScreenCapturer::SelectSource({}, [&](bool ok, auto source) {
      CHECK(!inline_call && ok && !source); selected = true;
    });
    inline_call = false; Until([&] { return selected; });
    auto source = Source();
    auto lease = std::make_shared<CaptureLease>(source);
    CHECK(lease->target);
    bool rejected = false;
    ScreenCapturer::Capture(source, {}, [&](bool ok, auto image) { CHECK(!ok && !image); rejected = true; });
    Until([&] { return rejected; });
    lease.reset();

    { ScreenRecorder recorder;
      CHECK(!recorder.Start(source, "relative.mp4", {}));
      CHECK(recorder.GetState() == ScreenRecordingState::Idle);
      CHECK(!recorder.Stop() && !recorder.Cancel());
      int starts = 0, completes = 0, failures = 0;
      recorder.AddListener<ScreenRecordingStartedEvent>([&](const auto&) { ++starts; });
      recorder.AddListener<ScreenRecordingCompletedEvent>([&](const auto& event) {
        ++completes; CHECK(event.GetFilePath() == path("success.mp4")); CHECK(event.GetDuration() > 0);
      });
      recorder.AddListener<ScreenRecordingFailedEvent>([&](const auto&) { ++failures; });
      CHECK(recorder.Start(source, path("success.mp4"), {}));
      CHECK(!recorder.Start(source, path("again.mp4"), {}));
      Until([&] { return starts == 1; });
      CHECK(recorder.IsActive());
      CHECK(recorder.Stop()); CHECK(!recorder.Cancel() && !recorder.Stop());
      Until([&] { return completes == 1 || failures; });
      CHECK(completes == 1 && failures == 0 && !recorder.IsActive());
      CHECK(std::filesystem::exists(std::filesystem::u8path(path("success.mp4"))));
      CHECK(!recorder.Start(source, path("again.mp4"), {}));
    }
    { ScreenRecorder recorder; int cancels = 0;
      wait_open = true;
      recorder.AddListener<ScreenRecordingCancelledEvent>([&](const auto&) { ++cancels; });
      CHECK(recorder.Start(source, path("cancel.mp4"), {}));
      CHECK(!recorder.Stop()); CHECK(recorder.Cancel());
      Until([&] { return cancels == 1; }); wait_open = false;
      CHECK(!std::filesystem::exists(std::filesystem::u8path(path("cancel.mp4"))));
    }
    { ScreenRecorder recorder; int failures = 0;
      fail_encode = true;
      recorder.AddListener<ScreenRecordingFailedEvent>([&](const auto& event) {
        CHECK(!event.GetMessage().empty()); ++failures;
      });
      CHECK(recorder.Start(source, path("fail.mp4"), {}));
      Until([&] { return failures == 1; }); fail_encode = false;
      CHECK(recorder.GetState() == ScreenRecordingState::Failed);
      CHECK(!std::filesystem::exists(std::filesystem::u8path(path("fail.mp4"))));
    }
    { ScreenRecorder recorder; int failures = 0, cancels = 0;
      fail_encode = true; wait_encode = true; entered_encode = false;
      recorder.AddListener<ScreenRecordingFailedEvent>([&](const auto& event) {
        CHECK(event.GetMessage() == "Test encode failure"); ++failures;
      });
      recorder.AddListener<ScreenRecordingCancelledEvent>([&](const auto&) { ++cancels; });
      CHECK(recorder.Start(source, path("cancel-with-encode-error.mp4"), {}));
      Until([&] { return entered_encode.load(); });
      CHECK(recorder.Cancel());
      wait_encode = false;
      Until([&] { return failures == 1; });
      fail_encode = false;
      CHECK(cancels == 0 && recorder.GetState() == ScreenRecordingState::Failed);
      CHECK(!std::filesystem::exists(std::filesystem::u8path(path("cancel-with-encode-error.mp4"))));
    }
    { wait_open = true;
      auto recorder = std::make_unique<ScreenRecorder>();
      CHECK(recorder->Start(source, path("destroy.mp4"), {}));
      recorder.reset(); wait_open = false;
      Until([&] { CaptureLease next(source); return next.target != nullptr; });
      CHECK(!std::filesystem::exists(std::filesystem::u8path(path("destroy.mp4"))));
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory))
      CHECK(entry.path().filename().string().find(".nativeapi-recording-") != 0);
    std::filesystem::remove_all(directory);
    std::cout << "PASS: screen capture shared contracts\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
