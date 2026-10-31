#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <VideoToolbox/VideoToolbox.h>

// Static-archive consumers (notably Cargo) do not inherit CMake's link flags.
// Attach this dependency to the capture object itself: unused capture code
// must not make older systems load ScreenCaptureKit. With an older deployment
// target its availability-annotated references make this a weak load command.
asm(".linker_option \"-framework\", \"ScreenCaptureKit\"");

#include <condition_variable>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

#include "../../display.h"
#include "../../foundation/dispatcher.h"
#include "../../image.h"
#include "../../screen_capture_backend.h"
#include "../../window.h"
#include "string_utils_macos.h"

namespace nativeapi::detail {
namespace {
// Works in both ARC consumers and the core's ordinary Objective-C++ build.
template <typename T> class Obj {
 public:
  Obj(T value = nil) : value_(value) {
#if !__has_feature(objc_arc)
    [value_ retain];
#endif
  }
  Obj(const Obj& other) : Obj(other.get()) {}
  Obj& operator=(const Obj& other) {
    if (this != &other) {
#if !__has_feature(objc_arc)
      [other.value_ retain];
      [value_ release];
#endif
      value_ = other.value_;
    }
    return *this;
  }
  ~Obj() {
#if !__has_feature(objc_arc)
    [value_ release];
#endif
  }
  T get() const { return value_; }
 private:
  T value_;
};
template <typename T> T Autorelease(T value) {
#if !__has_feature(objc_arc)
  return [value autorelease];
#else
  return value;
#endif
}

struct Completion {
  std::mutex mutex;
  std::condition_variable changed;
  bool done = false;
  std::string error;
  Obj<SCShareableContent*> content;
  void Complete(NSError* failure) {
    std::lock_guard<std::mutex> lock(mutex);
    error = ToStdString(failure.localizedDescription);
    done = true;
    changed.notify_all();
  }
  bool Wait(const std::atomic<bool>& cancel, std::string& failure) {
    std::unique_lock<std::mutex> lock(mutex);
    auto deadline = CaptureClock::now() + std::chrono::seconds(15);
    while (!done && !cancel && CaptureClock::now() < deadline)
      changed.wait_for(lock, std::chrono::milliseconds(50));
    if (done && !error.empty()) { failure = error; return false; }
    if (cancel) { failure.clear(); return false; }
    if (!done) { failure = "ScreenCaptureKit operation timed out"; return false; }
    failure = error;
    return error.empty();
  }
};

struct FrameMailbox {
  std::mutex mutex;
  std::condition_variable changed;
  std::shared_ptr<CaptureFrame> latest;
  std::string error;
  bool stopped = false;
  bool resizing = false;
  Obj<SCStreamConfiguration*> configuration;
};
}  // namespace
}  // namespace nativeapi::detail

@interface NativeAPICaptureOutput : NSObject <SCStreamOutput, SCStreamDelegate> {
 @public
  std::shared_ptr<nativeapi::detail::FrameMailbox> mailbox;
}
@end

@implementation NativeAPICaptureOutput
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
  std::lock_guard<std::mutex> lock(mailbox->mutex);
  if (!mailbox->stopped) {
    mailbox->error = nativeapi::ToStdString(error.localizedDescription);
    if (mailbox->error.empty()) mailbox->error = "Screen capture stopped unexpectedly";
  }
  mailbox->changed.notify_all();
}
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sample
       ofType:(SCStreamOutputType)type {
  using namespace nativeapi::detail;
  @autoreleasepool {
    if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample)) return;
    try {
      NSArray* attachments = (__bridge NSArray*)CMSampleBufferGetSampleAttachmentsArray(sample, false);
      NSDictionary* info = attachments.firstObject;
      NSNumber* status = info[SCStreamFrameInfoStatus];
      if (!status || status.integerValue != SCFrameStatusComplete) return;
      CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sample);
      if (!pixels || CVPixelBufferGetPixelFormatType(pixels) != kCVPixelFormatType_32BGRA) return;
      int width = int(CVPixelBufferGetWidth(pixels)), height = int(CVPixelBufferGetHeight(pixels));
      if (width <= 0 || height <= 0 || width > 32768 || height > 32768) return;
      {
        std::lock_guard<std::mutex> lock(mailbox->mutex);
        if (mailbox->stopped || mailbox->resizing) return;
      }
      // Keep capture at source resolution after a window/display size change.
      CGRect screen = CGRectZero;
      NSDictionary* screenValue = info[SCStreamFrameInfoScreenRect];
      double scale = [info[SCStreamFrameInfoScaleFactor] doubleValue];
      if (screenValue && CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)screenValue, &screen)
          && scale > 0) {
        int next_width = int(std::ceil(screen.size.width * scale));
        int next_height = int(std::ceil(screen.size.height * scale));
        if (next_width > 0 && next_height > 0 && next_width <= 32768 && next_height <= 32768 &&
            (next_width != width || next_height != height)) {
          auto state = mailbox;
          { std::lock_guard<std::mutex> lock(state->mutex); state->resizing = true; }
          SCStreamConfiguration* config = state->configuration.get();
          config.width = next_width;
          config.height = next_height;
          [stream updateConfiguration:config completionHandler:^(NSError* error) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->resizing = false;
            if (error) state->error = nativeapi::ToStdString(error.localizedDescription);
            state->changed.notify_all();
          }];
          return;
        }
      }
      auto frame = std::make_shared<CaptureFrame>();
      frame->width = width;
      frame->height = height;
      frame->time = CaptureClock::now();
      frame->pixels.resize(size_t(width) * height * 4);
      if (CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
      auto bytes = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pixels));
      size_t stride = CVPixelBufferGetBytesPerRow(pixels);
      for (int y = 0; y < height; ++y)
        std::memcpy(frame->pixels.data() + size_t(y) * width * 4, bytes + size_t(y) * stride, size_t(width) * 4);
      CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
      std::lock_guard<std::mutex> lock(mailbox->mutex);
      if (!mailbox->stopped) mailbox->latest = std::move(frame);
      mailbox->changed.notify_all();
    } catch (...) {
      std::lock_guard<std::mutex> lock(mailbox->mutex);
      mailbox->error = "Cannot copy the captured frame";
      mailbox->changed.notify_all();
    }
  }
}
@end

namespace nativeapi::detail {
namespace {
class MacStream : public CaptureStream {
 public:
  explicit MacStream(SCContentFilter* filter, const ScreenCaptureOptions& options, double fps)
      : mailbox_(std::make_shared<FrameMailbox>()) {
    @autoreleasepool {
      auto config = Autorelease([[SCStreamConfiguration alloc] init]);
      config.width = size_t(std::ceil(filter.contentRect.size.width * filter.pointPixelScale));
      config.height = size_t(std::ceil(filter.contentRect.size.height * filter.pointPixelScale));
      config.pixelFormat = kCVPixelFormatType_32BGRA;
      config.colorSpaceName = kCGColorSpaceSRGB;
      config.showsCursor = options.include_cursor;
      config.ignoreShadowsSingleWindow = YES;
      config.queueDepth = 3;
      config.minimumFrameInterval = CMTimeMakeWithSeconds(1.0 / fps, 1000000);
      if (@available(macOS 15.0, *)) config.captureDynamicRange = SCCaptureDynamicRangeSDR;
      mailbox_->configuration = Obj<SCStreamConfiguration*>(config);
      output_ = Obj<NativeAPICaptureOutput*>(Autorelease([[NativeAPICaptureOutput alloc] init]));
      output_.get()->mailbox = mailbox_;
      stream_ = Obj<SCStream*>(Autorelease([[SCStream alloc] initWithFilter:filter
          configuration:config delegate:output_.get()]));
      queue_ = dispatch_queue_create("org.libnativeapi.screen-capture", DISPATCH_QUEUE_SERIAL);
    }
  }
  bool Start(const std::atomic<bool>& cancel, std::string& error) {
    @autoreleasepool {
      NSError* failure = nil;
      if (![stream_.get() addStreamOutput:output_.get() type:SCStreamOutputTypeScreen
          sampleHandlerQueue:queue_ error:&failure]) {
        error = ToStdString(failure.localizedDescription); return false;
      }
      auto completion = std::make_shared<Completion>();
      [stream_.get() startCaptureWithCompletionHandler:^(NSError* failure) { completion->Complete(failure); }];
      return completion->Wait(cancel, error);
    }
  }
  std::shared_ptr<CaptureFrame> NextFrame(int timeout, std::string& error) override {
    std::unique_lock<std::mutex> lock(mailbox_->mutex);
    mailbox_->changed.wait_for(lock, std::chrono::milliseconds(timeout), [&] {
      return mailbox_->latest || !mailbox_->error.empty() || mailbox_->stopped;
    });
    error = mailbox_->error;
    return error.empty() ? std::exchange(mailbox_->latest, nullptr) : nullptr;
  }
  bool Stop(std::string& error) override {
    @autoreleasepool {
      { std::lock_guard<std::mutex> lock(mailbox_->mutex);
        if (mailbox_->stopped) return true;
        mailbox_->stopped = true;
        mailbox_->latest.reset();
      }
      auto completion = std::make_shared<Completion>();
      [stream_.get() stopCaptureWithCompletionHandler:^(NSError* failure) { completion->Complete(failure); }];
      std::atomic<bool> never_cancel{false};
      return completion->Wait(never_cancel, error);
    }
  }
  ~MacStream() override {
    std::string ignored;
    Stop(ignored);
#if !OS_OBJECT_USE_OBJC
    if (queue_) dispatch_release(queue_);
#endif
  }
 private:
  std::shared_ptr<FrameMailbox> mailbox_;
  Obj<NativeAPICaptureOutput*> output_;
  Obj<SCStream*> stream_;
  dispatch_queue_t queue_ = nullptr;
};

class MacTarget : public CaptureTarget {
 public:
  CaptureSourceType kind = CaptureSourceType::None;
  uint32_t identity = 0;
  std::shared_ptr<Display> display;
  std::shared_ptr<Window> window;
  Obj<SCContentFilter*> selected;
  std::shared_ptr<void> picker_lifetime;
  CaptureSourceType Type() const override {
    if (display && !CGDisplayIsActive(identity)) return CaptureSourceType::None;
    if (window && !window->GetNativeObject()) return CaptureSourceType::None;
    return kind;
  }
  std::string Name() const override {
    if (Type() == CaptureSourceType::None) return {};
    if (display) return display->GetName();
    if (window) return window->GetTitle();
    if (@available(macOS 15.2, *)) {
      if (kind == CaptureSourceType::Window)
        return ToStdString(selected.get().includedWindows.firstObject.title);
    }
    return {};
  }
  std::unique_ptr<CaptureStream> Open(const ScreenCaptureOptions& options, double fps,
      const std::atomic<bool>& cancel, std::string& error) override {
    @autoreleasepool {
      Obj<SCContentFilter*> filter = selected;
      if (!filter.get()) {
        auto completion = std::make_shared<Completion>();
        [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:NO
            completionHandler:^(SCShareableContent* content, NSError* failure) {
              completion->content = Obj<SCShareableContent*>(content);
              completion->Complete(failure);
            }];
        if (!completion->Wait(cancel, error)) return nullptr;
        if (kind == CaptureSourceType::Display) {
          for (SCDisplay* item in completion->content.get().displays)
            if (item.displayID == identity) filter = Obj<SCContentFilter*>(Autorelease(
                [[SCContentFilter alloc] initWithDisplay:item excludingWindows:@[]]));
        } else {
          for (SCWindow* item in completion->content.get().windows)
            if (item.windowID == identity) filter = Obj<SCContentFilter*>(Autorelease(
                [[SCContentFilter alloc] initWithDesktopIndependentWindow:item]));
        }
      }
      if (!filter.get()) { error = "The capture target no longer exists"; return nullptr; }
      auto stream = std::make_unique<MacStream>(filter.get(), options, fps);
      if (!stream->Start(cancel, error)) return nullptr;
      return stream;
    }
  }
};

class MacEncoder : public CaptureEncoder {
 public:
  Obj<AVAssetWriter*> writer;
  Obj<AVAssetWriterInput*> input;
  Obj<AVAssetWriterInputPixelBufferAdaptor*> adaptor;
  std::shared_ptr<CaptureFrame> last;
  double last_time = -1;
  bool finished = false;
  bool Append(const CaptureFrame& frame, double seconds, std::string& error) override {
    @autoreleasepool {
      auto deadline = CaptureClock::now() + std::chrono::seconds(10);
      while (!input.get().readyForMoreMediaData && writer.get().status == AVAssetWriterStatusWriting &&
          CaptureClock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
      if (!input.get().readyForMoreMediaData) {
        error = "Video encoder stalled: " + ToStdString(writer.get().error.localizedDescription); return false;
      }
      CVPixelBufferRef buffer = nullptr;
      if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, adaptor.get().pixelBufferPool,
          &buffer) != kCVReturnSuccess) { error = "Cannot allocate encoder pixels"; return false; }
      CVPixelBufferLockBaseAddress(buffer, 0);
      auto bytes = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
      auto stride = CVPixelBufferGetBytesPerRow(buffer);
      for (int y = 0; y < frame.height; ++y)
        std::memcpy(bytes + size_t(y) * stride,
            frame.pixels.data() + size_t(y) * frame.width * 4, size_t(frame.width) * 4);
      CVPixelBufferUnlockBaseAddress(buffer, 0);
      bool ok = [adaptor.get() appendPixelBuffer:buffer withPresentationTime:CMTimeMakeWithSeconds(seconds, 1000000)];
      CVPixelBufferRelease(buffer);
      if (!ok) { error = ToStdString(writer.get().error.localizedDescription); return false; }
      last = std::make_shared<CaptureFrame>(frame);
      last_time = seconds;
      return true;
    }
  }
  bool Finish(double end, std::string& error) override {
    @autoreleasepool {
      // A trailing sample makes static scenes cover the full requested duration.
      if (last && end > last_time + 0.002 && !Append(*last, end - 0.001, error)) return false;
      [writer.get() endSessionAtSourceTime:CMTimeMakeWithSeconds(end, 1000000)];
      [input.get() markAsFinished];
      auto completion = std::make_shared<Completion>();
      Obj<AVAssetWriter*> retained = writer;
      [writer.get() finishWritingWithCompletionHandler:^{ completion->Complete(retained.get().error); }];
      std::atomic<bool> never_cancel{false};
      finished = completion->Wait(never_cancel, error) && writer.get().status == AVAssetWriterStatusCompleted;
      return finished;
    }
  }
  ~MacEncoder() override {
    @autoreleasepool { if (!finished) [writer.get() cancelWriting]; }
  }
};
}  // namespace
}  // namespace nativeapi::detail

@interface NativeAPICapturePicker : NSObject <SCContentSharingPickerObserver> {
 @public
  std::function<void(bool, SCContentFilter*)> completion;
}
@end
@implementation NativeAPICapturePicker
- (void)contentSharingPicker:(SCContentSharingPicker*)picker didCancelForStream:(SCStream*)stream {
  if (!stream && completion) completion(true, nil);
}
- (void)contentSharingPicker:(SCContentSharingPicker*)picker didUpdateWithFilter:(SCContentFilter*)filter
                  forStream:(SCStream*)stream {
  if (!stream && completion) completion(true, filter);
}
- (void)contentSharingPickerStartDidFailWithError:(NSError*)error {
  if (completion) completion(false, nil);
}
@end

namespace nativeapi::detail {
namespace {
std::atomic<int> picker_users{0};
bool picker_was_active = false;
struct PickerLifetime {
  PickerLifetime() {
    if (picker_users.fetch_add(1) == 0) picker_was_active = SCContentSharingPicker.sharedPicker.active;
    SCContentSharingPicker.sharedPicker.active = YES;
  }
  ~PickerLifetime() {
    if (picker_users.fetch_sub(1) == 1) RunOnMainThread([] {
      if (picker_users.load() == 0) SCContentSharingPicker.sharedPicker.active = picker_was_active;
    });
  }
};
Obj<NativeAPICapturePicker*> pending_picker;
}  // namespace

bool CaptureBackendSupported() { if (@available(macOS 14.0, *)) return true; return false; }
bool CapturePickerSupported() { return CaptureBackendSupported(); }
bool CaptureEncodingSupported() {
  @autoreleasepool {
    if (!CaptureBackendSupported()) return false;
    CFArrayRef encoders = nullptr;
    if (VTCopyVideoEncoderList(nullptr, &encoders) != noErr || !encoders) return false;
    bool supported = false;
    for (NSDictionary* encoder in (__bridge NSArray*)encoders)
      if ([encoder[(__bridge NSString*)kVTVideoEncoderList_CodecType] unsignedIntValue] == kCMVideoCodecType_H264)
        supported = true;
    CFRelease(encoders);
    return supported;
  }
}
std::shared_ptr<CaptureTarget> CaptureDisplayTarget(std::shared_ptr<Display> display) {
  if (!CaptureBackendSupported() || !display) return nullptr;
  @autoreleasepool {
    NSScreen* screen = (__bridge NSScreen*)display->GetNativeObject();
    uint32_t identity = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    if (!identity || !CGDisplayIsActive(identity)) return nullptr;
    auto target = std::make_shared<MacTarget>();
    target->kind = CaptureSourceType::Display;
    target->identity = identity;
    target->display = std::move(display);
    return target;
  }
}
std::shared_ptr<CaptureTarget> CaptureWindowTarget(std::shared_ptr<Window> window) {
  if (!CaptureBackendSupported() || !window) return nullptr;
  @autoreleasepool {
    NSWindow* native = (__bridge NSWindow*)window->GetNativeObject();
    if (!native || native.windowNumber <= 0) return nullptr;
    auto target = std::make_shared<MacTarget>();
    target->kind = CaptureSourceType::Window;
    target->identity = uint32_t(native.windowNumber);
    target->window = std::move(window);
    return target;
  }
}
void SelectCaptureTarget(const CaptureSourceSelectionOptions& options,
    std::function<void(bool, std::shared_ptr<CaptureTarget>)> callback) {
  if (!CapturePickerSupported()) { callback(false, nullptr); return; }
  @autoreleasepool {
    auto lifetime = std::make_shared<PickerLifetime>();
    SCContentSharingPicker* picker = SCContentSharingPicker.sharedPicker;
    Obj<SCContentSharingPickerConfiguration*> old(picker.defaultConfiguration);
    auto configuration = Autorelease([[SCContentSharingPickerConfiguration alloc] init]);
    configuration.allowedPickerModes = (options.allow_displays ? SCContentSharingPickerModeSingleDisplay : 0) |
        (options.allow_windows ? SCContentSharingPickerModeSingleWindow : 0);
    configuration.allowsChangingSelectedContent = NO;
    picker.defaultConfiguration = configuration;
    pending_picker = Obj<NativeAPICapturePicker*>(Autorelease([[NativeAPICapturePicker alloc] init]));
    pending_picker.get()->completion = [callback, lifetime, old](bool ok, SCContentFilter* filter) {
      Obj<SCContentFilter*> retained(filter);
      RunOnMainThread([callback, lifetime, old, ok, retained] {
        SCContentSharingPicker* picker = SCContentSharingPicker.sharedPicker;
        [picker removeObserver:pending_picker.get()];
        picker.defaultConfiguration = old.get();
        pending_picker = Obj<NativeAPICapturePicker*>();
        std::shared_ptr<MacTarget> target;
        if (retained.get()) {
          target = std::make_shared<MacTarget>();
          target->kind = retained.get().style == SCShareableContentStyleDisplay ?
              CaptureSourceType::Display : CaptureSourceType::Window;
          target->selected = retained;
          target->picker_lifetime = lifetime;
        }
        callback(ok, target);
      });
    };
    [picker addObserver:pending_picker.get()];
    [picker present];
  }
}
std::shared_ptr<Image> CaptureFrameImage(const CaptureFrame& frame) {
  @autoreleasepool {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(const_cast<uint8_t*>(frame.pixels.data()),
        frame.width, frame.height, 8, size_t(frame.width) * 4, space,
        kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
    CGColorSpaceRelease(space);
    if (!context) return nullptr;
    CGImageRef image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    if (!image) return nullptr;
    auto representation = Autorelease([[NSBitmapImageRep alloc] initWithCGImage:image]);
    CGImageRelease(image);
    NSData* png = [representation representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return png ? Image::FromBase64(ToStdString([png base64EncodedStringWithOptions:0])) : nullptr;
  }
}
std::unique_ptr<CaptureEncoder> CreateCaptureEncoder(const std::string& path, int width, int height,
    const ScreenRecordingOptions& options, std::string& error) {
  @autoreleasepool {
    auto encoder = std::make_unique<MacEncoder>();
    NSError* failure = nil;
    encoder->writer = Obj<AVAssetWriter*>(Autorelease([[AVAssetWriter alloc]
        initWithURL:[NSURL fileURLWithPath:ToNSString(path)] fileType:AVFileTypeMPEG4 error:&failure]));
    if (!encoder->writer.get()) { error = ToStdString(failure.localizedDescription); return nullptr; }
    NSMutableDictionary* compression = [NSMutableDictionary dictionary];
    if (options.video_bitrate) compression[AVVideoAverageBitRateKey] = @(options.video_bitrate);
    compression[AVVideoExpectedSourceFrameRateKey] = @(options.frame_rate);
    NSDictionary* settings = @{ AVVideoCodecKey: AVVideoCodecTypeH264, AVVideoWidthKey: @(width),
        AVVideoHeightKey: @(height), AVVideoCompressionPropertiesKey: compression,
        AVVideoColorPropertiesKey: @{ AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
          AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
          AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2 } };
    if (![encoder->writer.get() canApplyOutputSettings:settings forMediaType:AVMediaTypeVideo]) {
      error = "H.264 encoding settings are unsupported"; return nullptr;
    }
    encoder->input = Obj<AVAssetWriterInput*>([AVAssetWriterInput assetWriterInputWithMediaType:
        AVMediaTypeVideo outputSettings:settings]);
    encoder->input.get().expectsMediaDataInRealTime = YES;
    encoder->adaptor = Obj<AVAssetWriterInputPixelBufferAdaptor*>(
        [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:
            encoder->input.get() sourcePixelBufferAttributes:@{
              (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
              (id)kCVPixelBufferWidthKey: @(width), (id)kCVPixelBufferHeightKey: @(height),
              (id)kCVPixelBufferIOSurfacePropertiesKey: @{} }]);
    if (![encoder->writer.get() canAddInput:encoder->input.get()]) {
      error = "Cannot attach video encoder"; return nullptr;
    }
    [encoder->writer.get() addInput:encoder->input.get()];
    if (![encoder->writer.get() startWriting]) {
      error = ToStdString(encoder->writer.get().error.localizedDescription); return nullptr;
    }
    [encoder->writer.get() startSessionAtSourceTime:kCMTimeZero];
    return encoder;
  }
}
}  // namespace nativeapi::detail
