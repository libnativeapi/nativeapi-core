// Explicit desktop integration, no synthetic input; only captures our own window.
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include "../src/foundation/dispatcher.h"
#include "../src/image.h"
#include "../src/screen_recorder.h"
#include "../src/window.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace nativeapi;
using Microsoft::WRL::ComPtr;
static COLORREF color = RGB(255, 0, 0);
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  if (message == WM_PAINT) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT rect; GetClientRect(window, &rect);
    auto brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    EndPaint(window, &paint);
    return 0;
  }
  return DefWindowProcW(window, message, wp, lp);
}
static bool Wait(const std::function<bool()>& done, int seconds = 25) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (!done() && std::chrono::steady_clock::now() < end) RunMainThreadLoopFor(10);
  return done();
}
static void Require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
static void Check(HRESULT result, const char* message) {
  Require(SUCCEEDED(result), std::string(message) + " HRESULT=" + std::to_string(result));
}

static void Decode(const std::filesystem::path& path) {
  Check(MFStartup(MF_VERSION), "MF startup");
  struct Shutdown { ~Shutdown() { MFShutdown(); } } shutdown;
  ComPtr<IMFAttributes> attributes;
  Check(MFCreateAttributes(&attributes, 1), "reader attributes");
  Check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE), "video processing");
  ComPtr<IMFSourceReader> reader;
  Check(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader), "open saved video");
  PROPVARIANT duration;
  PropVariantInit(&duration);
  Check(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration), "duration");
  auto seconds = duration.uhVal.QuadPart / 10000000.0;
  PropVariantClear(&duration);
  Require(seconds >= 1.4 && seconds < 8, "static video duration: " + std::to_string(seconds));
  ComPtr<IMFMediaType> type;
  Check(MFCreateMediaType(&type), "decoder media type");
  Check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "video type");
  Check(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "RGB output");
  Check(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get()), "decoder format");
  ComPtr<IMFMediaType> actual;
  Check(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual), "actual format");
  UINT32 width = 0, height = 0;
  Check(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height), "dimensions");
  Require(width > 0 && height > 0 && !(width % 2) && !(height % 2), "even output dimensions");
  bool red = false, blue = false;
  int frames = 0;
  LONGLONG previous = -1;
  for (;;) {
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> sample;
    Check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample), "decode frame");
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
      ComPtr<IMFMediaType> changed;
      Check(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &changed), "changed media type");
      UINT32 w = 0, h = 0;
      Check(MFGetAttributeSize(changed.Get(), MF_MT_FRAME_SIZE, &w, &h), "changed dimensions");
      // The decoder may pad its output to macroblock/stride alignment on the
      // first sample. Establish decoded geometry there, then require it to
      // remain fixed throughout the resized source's frames.
      if (!frames) { width = w; height = h; }
      else Require(w == width && h == height, "fixed output dimensions");
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    if (!sample) continue;
    Require(timestamp >= previous, "monotonic video timestamps");
    previous = timestamp;
    ComPtr<IMFMediaBuffer> buffer;
    Check(sample->ConvertToContiguousBuffer(&buffer), "pixel buffer");
    BYTE* pixels = nullptr; DWORD length = 0;
    Check(buffer->Lock(&pixels, nullptr, &length), "lock pixels");
    // Center pixel is insensitive to top-down/bottom-up RGB orientation.
    size_t offset = (size_t(height / 2) * width + width / 2) * 4;
    if (offset + 4 <= length) {
      red |= pixels[offset + 2] > 180 && pixels[offset] < 60;
      blue |= pixels[offset] > 180 && pixels[offset + 2] < 60;
    }
    buffer->Unlock();
    ++frames;
  }
  Require(red && blue && frames >= 2, "decoded red and blue frames; frames=" + std::to_string(frames));
  std::cout << "Decoded " << frames << " frames, " << width << 'x' << height << ", " << seconds << " seconds\n";
}

int main() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  struct Uninitialize { HRESULT result; ~Uninitialize() { if (SUCCEEDED(result)) CoUninitialize(); } } uninit{com};
  SetMainThread();
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  if (!session || !ScreenCapturer::IsSupported() || !ScreenRecorder::IsSupported()) {
    std::cout << "SKIP: a logged-on desktop and supported capture/encoder are required\n";
    return 77;
  }
  WNDCLASSW cls{};
  cls.lpfnWndProc = WindowProc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"NativeAPICaptureSmoke";
  RegisterClassW(&cls);
  HWND native = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"nativeapi capture verification",
      WS_OVERLAPPEDWINDOW, 150, 150, 320, 240, nullptr, nullptr, cls.hInstance, nullptr);
  if (!native) return 1;
  struct CloseWindow { HWND window; ~CloseWindow() { DestroyWindow(window); } } closer{native};
  ShowWindow(native, SW_SHOWNOACTIVATE);
  UpdateWindow(native);
  try {
    auto directory = std::filesystem::temp_directory_path() /
        ("nativeapi-capture-smoke-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directory(directory);
    auto png = directory / "screenshot.png";
    auto mp4 = directory / "recording.mp4";
    std::cout << "ARTIFACTS " << directory.u8string() << std::endl;
    auto window = std::make_shared<Window>(native);
    auto source = CaptureSource::FromWindow(window);
    Require(source != nullptr, "create window source");
    bool done = false, screenshot_ok = false;
    ScreenCaptureOptions crop;
    crop.crop_enabled = true; crop.crop = {30, 60, 64, 64};
    std::shared_ptr<Image> screenshot;
    ScreenCapturer::Capture(source, crop, [&](bool ok, std::shared_ptr<Image> image) {
      screenshot = std::move(image);
      if (ok && screenshot && screenshot->GetSize().width == 64 && screenshot->GetSize().height == 64) {
        auto bitmap = static_cast<Gdiplus::Bitmap*>(screenshot->GetNativeObject());
        Gdiplus::Color pixel;
        screenshot_ok = bitmap && bitmap->GetPixel(32, 32, &pixel) == Gdiplus::Ok &&
            pixel.GetR() > 200 && pixel.GetB() < 40 && screenshot->SaveToFile(png.u8string());
      }
      done = true;
    });
    Require(Wait([&] { return done; }) && screenshot_ok, "screenshot crop and red pixel");
    auto recorder = std::make_shared<ScreenRecorder>();
    bool started = false, completed = false, failed = false;
    std::string failure;
    recorder->AddListener<ScreenRecordingStartedEvent>([&](const auto&) { started = true; });
    recorder->AddListener<ScreenRecordingCompletedEvent>([&](const auto&) { completed = true; });
    recorder->AddListener<ScreenRecordingFailedEvent>([&](const auto& event) { failed = true; failure = event.GetMessage(); });
    Require(recorder->Start(source, mp4.u8string(), {}), "accept recording");
    Require(Wait([&] { return started || failed; }) && started, "start recording: " + failure);
    auto start = std::chrono::steady_clock::now();
    bool changed = false;
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1600)) {
      if (!changed && std::chrono::steady_clock::now() - start > std::chrono::milliseconds(500)) {
        std::cout << "Changing to blue and resizing\n";
        color = RGB(0, 0, 255);
        SetWindowPos(native, nullptr, 0, 0, 480, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(native, nullptr, FALSE); UpdateWindow(native);
        changed = true;
      }
      RunMainThreadLoopFor(10);
    }
    Require(recorder->Stop(), "accept stop: " + failure);
    Require(Wait([&] { return completed || failed; }) && completed, "finalize recording: " + failure);
    source.reset(); recorder.reset(); window.reset();
    // The image must still own its original red pixels after capture/recording ended.
    Gdiplus::Color pixel;
    auto bitmap = static_cast<Gdiplus::Bitmap*>(screenshot->GetNativeObject());
    Require(bitmap->GetPixel(32, 32, &pixel) == Gdiplus::Ok && pixel.GetR() > 200, "independent screenshot storage");
    Decode(mp4);
    std::cout << "PASS: Windows real window screenshot, crop, independent PNG, H.264 decode, resize, static duration\n"
              << "ARTIFACTS " << directory.u8string() << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
