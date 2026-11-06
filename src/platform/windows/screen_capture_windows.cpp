#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <roapi.h>
#include <shobjidl.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wincodec.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

#include "../../display.h"
#include "../../foundation/dispatcher.h"
#include "../../image.h"
#include "../../screen_capture_backend.h"
#include "../../window.h"

namespace nativeapi::detail {
namespace {
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::Windows::Foundation::Metadata::ApiInformation;

struct Apartment {
  HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
  ~Apartment() { if (SUCCEEDED(result)) RoUninitialize(); }
};
struct MediaFoundation {
  HRESULT result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  ~MediaFoundation() { if (SUCCEEDED(result)) MFShutdown(); }
};
std::string Failure(const hresult_error& error) { return to_string(error.message()); }

float Half(uint16_t value) {
  uint32_t sign = uint32_t(value & 0x8000) << 16;
  int exponent = (value >> 10) & 31;
  uint32_t fraction = value & 1023;
  uint32_t bits;
  if (!exponent) {
    if (!fraction) bits = sign;
    else {
      exponent = -14;
      while (!(fraction & 1024)) { fraction <<= 1; --exponent; }
      bits = sign | (uint32_t(exponent + 127) << 23) | ((fraction & 1023) << 13);
    }
  } else bits = sign | (uint32_t(exponent == 31 ? 255 : exponent + 112) << 23) | (fraction << 13);
  float result;
  std::memcpy(&result, &bits, sizeof(result));
  return std::isfinite(result) ? std::max(0.0f, result) : 0.0f;
}
uint8_t Srgb(float value) {
  value = value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
  return uint8_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 255));
}

class WindowsStream : public CaptureStream {
 public:
  explicit WindowsStream(GraphicsCaptureItem item, std::shared_ptr<std::atomic<bool>> closed)
      : item_(std::move(item)), closed_(std::move(closed)) {}
  bool Start(const ScreenCaptureOptions& options, std::string& error) {
    try {
      include_cursor_ = options.include_cursor;
      D3D_FEATURE_LEVEL level;
      HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
          D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
          device_.put(), &level, context_.put());
      if (FAILED(hr)) check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
          D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
          device_.put(), &level, context_.put()));
      auto dxgi = device_.as<IDXGIDevice>();
      com_ptr<IInspectable> inspectable;
      check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
      native_device_ = inspectable.as<IDirect3DDevice>();
      size_ = item_.Size();
      if (size_.Width <= 0 || size_.Height <= 0) { error = "Capture target has no image"; return false; }
      pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(native_device_,
          DirectXPixelFormat::R16G16B16A16Float, 2, size_);
      session_ = pool_.CreateCaptureSession(item_);
      session_.IsCursorCaptureEnabled(options.include_cursor);
      session_.StartCapture();
      return true;
    } catch (const hresult_error& e) { error = Failure(e); return false; }
  }
  std::shared_ptr<CaptureFrame> NextFrame(int timeout, std::string& error) override {
    try {
      auto deadline = CaptureClock::now() + std::chrono::milliseconds(timeout);
      do {
        if (closed_->load()) { error = "The capture target was closed"; return nullptr; }
        auto sample = pool_.TryGetNextFrame();
        if (!sample) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
        auto size = sample.ContentSize();
        if (size.Width <= 0 || size.Height <= 0 || size.Width > 32768 || size.Height > 32768) {
          sample.Close(); error = "Invalid capture dimensions"; return nullptr;
        }
        auto access = sample.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> texture;
        check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
        D3D11_TEXTURE2D_DESC description;
        texture->GetDesc(&description);
        bool resize = size.Width != size_.Width || size.Height != size_.Height;
        if (UINT(size.Width) > description.Width || UINT(size.Height) > description.Height) {
          sample.Close();
          sample = nullptr;
          access = nullptr;
          texture = nullptr;
          if (resize) {
            size_ = size;
            Resize();
          }
          continue;
        }
        description.Width = size.Width;
        description.Height = size.Height;
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        description.MiscFlags = 0;
        com_ptr<ID3D11Texture2D> staging;
        check_hresult(device_->CreateTexture2D(&description, nullptr, staging.put()));
        D3D11_BOX box{0, 0, 0, UINT(size.Width), UINT(size.Height), 1};
        context_->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, texture.get(), 0, &box);
        sample.Close();
        sample = nullptr;
        access = nullptr;
        texture = nullptr;
        if (resize) {
          size_ = size;
          Resize();
        }
        auto frame = std::make_shared<CaptureFrame>();
        frame->width = size.Width;
        frame->height = size.Height;
        frame->time = CaptureClock::now();
        frame->pixels.resize(size_t(size.Width) * size.Height * 4);
        D3D11_MAPPED_SUBRESOURCE mapped;
        check_hresult(context_->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
        // WGC float surfaces are linear scRGB. Preserve SDR and tone-map HDR
        // highlights with a luminance-based extended Reinhard shoulder.
        for (int y = 0; y < size.Height; ++y) {
          auto row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch);
          for (int x = 0; x < size.Width; ++x) {
            float r = Half(row[x * 4]), g = Half(row[x * 4 + 1]), b = Half(row[x * 4 + 2]);
            float peak = std::max({r, g, b});
            if (peak > 1.0f) {
              float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
              float mapped_luminance = luminance * (1 + luminance / (12.5f * 12.5f)) / (1 + luminance);
              float factor = luminance > 0 ? mapped_luminance / luminance : 1;
              r *= factor; g *= factor; b *= factor;
            }
            size_t offset = (size_t(y) * size.Width + x) * 4;
            frame->pixels[offset] = Srgb(b);
            frame->pixels[offset + 1] = Srgb(g);
            frame->pixels[offset + 2] = Srgb(r);
            frame->pixels[offset + 3] = 255;
          }
        }
        context_->Unmap(staging.get(), 0);
        return frame;
      } while (CaptureClock::now() < deadline);
    } catch (const hresult_error& e) { error = Failure(e); }
    return nullptr;
  }
  bool Stop(std::string& error) override {
    try {
      if (session_) { session_.Close(); session_ = nullptr; }
      if (pool_) { pool_.Close(); pool_ = nullptr; }
      return true;
    } catch (const hresult_error& e) { error = Failure(e); return false; }
  }
  ~WindowsStream() override { std::string ignored; Stop(ignored); }
 private:
  void Resize() {
    // Recreate alone can leave a static target with no new damage/frame after
    // the oversized transition frame was discarded. Restart consumption of
    // the same authorized item so its complete new image is delivered.
    session_.Close();
    session_ = nullptr;
    pool_.Close();
    pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(native_device_,
        DirectXPixelFormat::R16G16B16A16Float, 2, size_);
    session_ = pool_.CreateCaptureSession(item_);
    session_.IsCursorCaptureEnabled(include_cursor_);
    session_.StartCapture();
  }
  bool include_cursor_ = false;
  Apartment apartment_;
  GraphicsCaptureItem item_{nullptr};
  std::shared_ptr<std::atomic<bool>> closed_;
  com_ptr<ID3D11Device> device_;
  com_ptr<ID3D11DeviceContext> context_;
  IDirect3DDevice native_device_{nullptr};
  Direct3D11CaptureFramePool pool_{nullptr};
  GraphicsCaptureSession session_{nullptr};
  winrt::Windows::Graphics::SizeInt32 size_{};
};

class WindowsTarget : public CaptureTarget {
 public:
  GraphicsCaptureItem item{nullptr};
  CaptureSourceType kind = CaptureSourceType::Unknown;
  std::shared_ptr<std::atomic<bool>> closed = std::make_shared<std::atomic<bool>>(false);
  event_token token{};
  std::shared_ptr<Window> window;
  std::shared_ptr<Display> display;
  void Observe() { token = item.Closed([alive = closed](auto&&, auto&&) { alive->store(true); }); }
  ~WindowsTarget() override { try { if (item) item.Closed(token); } catch (...) {} }
  CaptureSourceType Type() const override { return closed->load() ? CaptureSourceType::None : kind; }
  std::string Name() const override {
    if (closed->load()) return {};
    if (window) return window->GetTitle();
    if (display) return display->GetName();
    return to_string(item.DisplayName());
  }
  std::unique_ptr<CaptureStream> Open(const ScreenCaptureOptions& options, double,
      const std::atomic<bool>& cancel, std::string& error) override {
    if (closed->load()) { error = "Capture source is unavailable"; return nullptr; }
    if (cancel) return nullptr;
    auto stream = std::make_unique<WindowsStream>(item, closed);
    if (!stream->Start(options, error)) return nullptr;
    return stream;
  }
};

class WindowsEncoder : public CaptureEncoder {
 public:
  Apartment apartment;
  MediaFoundation media;
  com_ptr<IMFSinkWriter> writer;
  DWORD index = 0;
  double rate = 30;
  std::shared_ptr<CaptureFrame> last;
  double last_time = -1;
  bool Write(const CaptureFrame& frame, double seconds, double duration, std::string& error) {
    try {
      DWORD length = DWORD(size_t(frame.width) * frame.height * 3 / 2);
      com_ptr<IMFMediaBuffer> buffer;
      check_hresult(MFCreateMemoryBuffer(length, buffer.put()));
      BYTE* bytes = nullptr;
      check_hresult(buffer->Lock(&bytes, nullptr, nullptr));
      auto clamp = [](double n) { return BYTE(std::clamp(std::lround(n), 0l, 255l)); };
      for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
          const auto* p = frame.pixels.data() + (size_t(y) * frame.width + x) * 4;
          bytes[size_t(y) * frame.width + x] = clamp(16 + .182586 * p[2] + .614231 * p[1] + .062007 * p[0]);
        }
      }
      auto uv = bytes + size_t(frame.width) * frame.height;
      for (int y = 0; y < frame.height; y += 2) for (int x = 0; x < frame.width; x += 2) {
        double r = 0, g = 0, b = 0;
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
          const auto* p = frame.pixels.data() + (size_t(y + dy) * frame.width + x + dx) * 4;
          r += p[2] / 4.0; g += p[1] / 4.0; b += p[0] / 4.0;
        }
        size_t offset = size_t(y / 2) * frame.width + x;
        uv[offset] = clamp(128 - .100644 * r - .338572 * g + .439216 * b);
        uv[offset + 1] = clamp(128 + .439216 * r - .398942 * g - .040274 * b);
      }
      check_hresult(buffer->Unlock());
      check_hresult(buffer->SetCurrentLength(length));
      com_ptr<IMFSample> sample;
      check_hresult(MFCreateSample(sample.put()));
      check_hresult(sample->AddBuffer(buffer.get()));
      check_hresult(sample->SetSampleTime(LONGLONG(std::llround(seconds * 10000000))));
      check_hresult(sample->SetSampleDuration(std::max<LONGLONG>(1, LONGLONG(std::llround(duration * 10000000)))));
      check_hresult(writer->WriteSample(index, sample.get()));
      return true;
    } catch (const hresult_error& e) { error = Failure(e); return false; }
  }
  bool Append(const CaptureFrame& frame, double seconds, std::string& error) override {
    // Hold one sample so its duration is the actual next timestamp, not a guessed FPS.
    if (last && !Write(*last, last_time, seconds - last_time, error)) return false;
    last = std::make_shared<CaptureFrame>(frame);
    last_time = seconds;
    return true;
  }
  bool Finish(double end, std::string& error) override {
    if (!last) return false;
    // Some H.264 MFTs replace the final input sample's duration with one
    // nominal frame period. A timestamped tail sample preserves a static
    // scene through Stop even with those encoders (without duplicating every
    // missing frame). Intermediate durations follow the next timestamp.
    double tail = end - 1.0 / rate;
    if (tail > last_time + 0.0001) {
      if (!Write(*last, last_time, tail - last_time, error) ||
          !Write(*last, tail, end - tail, error)) return false;
    } else if (!Write(*last, last_time, end - last_time, error)) return false;
    HRESULT hr = writer->Finalize();
    if (FAILED(hr)) { error = Failure(hresult_error(hr)); return false; }
    writer = nullptr;
    return true;
  }
};
}  // namespace

bool CaptureBackendSupported() {
  try {
    Apartment apartment;
    return GraphicsCaptureSession::IsSupported() && ApiInformation::IsPropertyPresent(
        L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled");
  } catch (...) { return false; }
}
bool CapturePickerSupported() { return CaptureBackendSupported(); }
bool CaptureEncodingSupported() {
  Apartment apartment;
  MediaFoundation media;
  if (FAILED(media.result)) return false;
  MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_H264};
  IMFActivate** encoders = nullptr;
  UINT32 count = 0;
  HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_ALL, nullptr, &output, &encoders, &count);
  for (UINT32 i = 0; i < count; ++i) encoders[i]->Release();
  CoTaskMemFree(encoders);
  return SUCCEEDED(hr) && count > 0;
}
std::shared_ptr<CaptureTarget> CaptureDisplayTarget(std::shared_ptr<Display> display) {
  if (!display || !CaptureBackendSupported()) return nullptr;
  Apartment apartment;
  auto monitor = static_cast<HMONITOR>(display->GetNativeObject());
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) return nullptr;
  auto target = std::make_shared<WindowsTarget>();
  auto factory = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
  check_hresult(factory->CreateForMonitor(monitor, guid_of<GraphicsCaptureItem>(), put_abi(target->item)));
  target->kind = CaptureSourceType::Display;
  target->display = std::move(display);
  target->Observe();
  return target;
}
std::shared_ptr<CaptureTarget> CaptureWindowTarget(std::shared_ptr<Window> window) {
  if (!window || !CaptureBackendSupported()) return nullptr;
  Apartment apartment;
  auto hwnd = static_cast<HWND>(window->GetNativeObject());
  if (!IsWindow(hwnd)) return nullptr;
  auto target = std::make_shared<WindowsTarget>();
  auto factory = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
  check_hresult(factory->CreateForWindow(hwnd, guid_of<GraphicsCaptureItem>(), put_abi(target->item)));
  target->kind = CaptureSourceType::Window;
  target->window = std::move(window);
  target->Observe();
  return target;
}
void SelectCaptureTarget(const CaptureSourceSelectionOptions& options,
    std::function<void(bool, std::shared_ptr<CaptureTarget>)> callback) {
  if (!options.allow_displays || !options.allow_windows || !options.parent_window) {
    callback(false, nullptr); return;
  }
  // Keep COM initialized until the asynchronous picker completes.
  const DWORD owner_thread = GetCurrentThreadId();
  auto apartment = std::shared_ptr<Apartment>(new Apartment(), [owner_thread](Apartment* value) {
    if (GetCurrentThreadId() == owner_thread) delete value;
    else {
      // COM initialization must be balanced on its originating thread. If
      // the UI dispatcher is gone, the OS reclaims this at process teardown.
      RunOnMainThread([value] { delete value; });
    }
  });
  auto hwnd = static_cast<HWND>(options.parent_window->GetNativeObject());
  if (!IsWindow(hwnd)) { callback(false, nullptr); return; }
  GraphicsCapturePicker picker;
  check_hresult(picker.as<IInitializeWithWindow>()->Initialize(hwnd));
  auto operation = picker.PickSingleItemAsync();
  operation.Completed([callback, parent = options.parent_window, apartment](auto operation, auto status) {
    std::shared_ptr<CaptureTarget> result;
    bool ok = false;
    try {
      if (status == winrt::Windows::Foundation::AsyncStatus::Completed &&
          IsWindow(static_cast<HWND>(parent->GetNativeObject()))) {
        auto item = operation.GetResults();
        if (item) {
          auto target = std::make_shared<WindowsTarget>();
          target->item = item;
          target->Observe();
          result = target;
        }
        ok = true;
      }
    } catch (...) {}
    // Release the originating apartment on the UI thread as well.
    RunOnMainThread([callback, result, ok, apartment] { callback(ok, result); });
  });
}
std::shared_ptr<Image> CaptureFrameImage(const CaptureFrame& frame) {
  Apartment apartment;
  com_ptr<IWICImagingFactory> factory;
  check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(factory.put())));
  com_ptr<IStream> stream;
  check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()));
  com_ptr<IWICBitmapEncoder> encoder;
  check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
  check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
  com_ptr<IWICBitmapFrameEncode> output;
  check_hresult(encoder->CreateNewFrame(output.put(), nullptr));
  check_hresult(output->Initialize(nullptr));
  check_hresult(output->SetSize(frame.width, frame.height));
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  check_hresult(output->SetPixelFormat(&format));
  if (format != GUID_WICPixelFormat32bppBGRA) return nullptr;
  check_hresult(output->WritePixels(frame.height, frame.width * 4, UINT(frame.pixels.size()),
      const_cast<BYTE*>(frame.pixels.data())));
  check_hresult(output->Commit());
  check_hresult(encoder->Commit());
  HGLOBAL memory = nullptr;
  check_hresult(GetHGlobalFromStream(stream.get(), &memory));
  STATSTG stat{};
  check_hresult(stream->Stat(&stat, STATFLAG_NONAME));
  auto bytes = static_cast<const uint8_t*>(GlobalLock(memory));
  if (!bytes) return nullptr;
  struct Unlock { HGLOBAL memory; ~Unlock() { GlobalUnlock(memory); } } unlock{memory};
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string base64;
  size_t length = size_t(stat.cbSize.QuadPart);
  base64.reserve((length + 2) / 3 * 4);
  for (size_t i = 0; i < length; i += 3) {
    unsigned v = unsigned(bytes[i]) << 16;
    if (i + 1 < length) v |= unsigned(bytes[i + 1]) << 8;
    if (i + 2 < length) v |= bytes[i + 2];
    base64 += alphabet[v >> 18]; base64 += alphabet[(v >> 12) & 63];
    base64 += i + 1 < length ? alphabet[(v >> 6) & 63] : '=';
    base64 += i + 2 < length ? alphabet[v & 63] : '=';
  }
  return Image::FromBase64(base64);
}
std::unique_ptr<CaptureEncoder> CreateCaptureEncoder(const std::string& path, int width, int height,
    const ScreenRecordingOptions& options, std::string& error) {
  try {
    auto encoder = std::make_unique<WindowsEncoder>();
    check_hresult(encoder->media.result);
    encoder->rate = options.frame_rate;
    com_ptr<IMFAttributes> attributes;
    check_hresult(MFCreateAttributes(attributes.put(), 2));
    check_hresult(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
    check_hresult(MFCreateSinkWriterFromURL(to_hstring(path).c_str(), nullptr, attributes.get(), encoder->writer.put()));
    com_ptr<IMFMediaType> output;
    check_hresult(MFCreateMediaType(output.put()));
    check_hresult(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    check_hresult(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
    check_hresult(output->SetUINT32(MF_MT_AVG_BITRATE, options.video_bitrate ? UINT32(options.video_bitrate) :
        UINT32(std::clamp(double(width) * height * options.frame_rate * .15, 1000000.0, 50000000.0))));
    check_hresult(output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    check_hresult(MFSetAttributeSize(output.get(), MF_MT_FRAME_SIZE, width, height));
    check_hresult(MFSetAttributeRatio(output.get(), MF_MT_FRAME_RATE, UINT32(std::lround(options.frame_rate * 1000)), 1000));
    check_hresult(MFSetAttributeRatio(output.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    check_hresult(output->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709));
    check_hresult(output->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709));
    check_hresult(output->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_sRGB));
    check_hresult(output->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235));
    check_hresult(encoder->writer->AddStream(output.get(), &encoder->index));
    com_ptr<IMFMediaType> input;
    check_hresult(MFCreateMediaType(input.put()));
    check_hresult(output->CopyAllItems(input.get()));
    check_hresult(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    check_hresult(encoder->writer->SetInputMediaType(encoder->index, input.get(), nullptr));
    check_hresult(encoder->writer->BeginWriting());
    return encoder;
  } catch (const hresult_error& e) { error = Failure(e); return nullptr; }
}
}  // namespace nativeapi::detail
