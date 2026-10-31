#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>

#include "../src/foundation/dispatcher.h"
#include "../src/image.h"
#include "../src/screen_recorder.h"
#include "../src/window.h"
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace nativeapi;
static bool Wait(const std::function<bool()>& done, int seconds = 25) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (!done() && std::chrono::steady_clock::now() < end) RunMainThreadLoopFor(10);
  return done();
}

int main(int argc, char** argv) {
  @autoreleasepool {
    if (!CGPreflightScreenCaptureAccess()) {
      std::cout << "SKIP: screen recording permission is required for this executable/host\n";
      return 77;
    }
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    SetMainThread();
    if (!ScreenCapturer::IsSupported() || !ScreenRecorder::IsSupported()) {
      std::cout << "SKIP: capture/encoder backend unavailable\n"; return 77;
    }
    auto directory = std::filesystem::temp_directory_path() /
        ("nativeapi-capture-smoke-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    std::string png = (directory / "screenshot.png").string();
    std::string mp4 = (directory / "recording.mp4").string();
    NSWindow* native = [[NSWindow alloc] initWithContentRect:NSMakeRect(150, 150, 240, 180)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    native.title = @"nativeapi capture verification";
    native.backgroundColor = NSColor.redColor;
    native.releasedWhenClosed = NO;
    [native orderFrontRegardless];
    auto window = std::make_shared<Window>((__bridge void*)native);
    auto source = CaptureSource::FromWindow(window);
    bool done = false, screenshot_ok = false;
    ScreenCaptureOptions crop;
    crop.crop_enabled = true; crop.crop = {16, 50, 64, 64};
    ScreenCapturer::Capture(source, crop, [&](bool ok, std::shared_ptr<Image> image) {
      if (ok && image && image->GetSize().width == 64 && image->GetSize().height == 64) {
        NSImage* nsImage = (__bridge NSImage*)image->GetNativeObject();
        NSBitmapImageRep* rep = [NSBitmapImageRep imageRepWithData:nsImage.TIFFRepresentation];
        NSColor* color = [[rep colorAtX:32 y:32] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
        screenshot_ok = color.redComponent > .8 && color.blueComponent < .2 && image->SaveToFile(png);
      }
      done = true;
    });
    if (!Wait([&] { return done; }) || !screenshot_ok) {
      std::cerr << "FAIL: screenshot/crop/pixel verification\n"; [native close]; return 1;
    }
    auto recorder = std::make_shared<ScreenRecorder>();
    bool started = false, completed = false, failed = false;
    std::string failure;
    recorder->AddListener<ScreenRecordingStartedEvent>([&](const auto&) { started = true; });
    recorder->AddListener<ScreenRecordingCompletedEvent>([&](const auto&) { completed = true; });
    recorder->AddListener<ScreenRecordingFailedEvent>([&](const auto& event) { failed = true; failure = event.GetMessage(); });
    if (!recorder->Start(source, mp4, {}) || !Wait([&] { return started || failed; }) || failed) {
      std::cerr << "FAIL: start recording: " << failure << '\n'; [native close]; return 1;
    }
    auto recording_start = std::chrono::steady_clock::now();
    bool changed = false;
    while (std::chrono::steady_clock::now() - recording_start < std::chrono::milliseconds(1600)) {
      if (!changed && std::chrono::steady_clock::now() - recording_start > std::chrono::milliseconds(500)) {
        native.backgroundColor = NSColor.blueColor;
        [native setContentSize:NSMakeSize(360, 240)];
        [native display];
        changed = true;
      }
      RunMainThreadLoopFor(10);
    }
    if (!recorder->Stop() || !Wait([&] { return completed || failed; }) || !completed) {
      std::cerr << "FAIL: finalize recording: " << failure << '\n'; [native close]; return 1;
    }
    [native close];
    source.reset(); window.reset(); recorder.reset();
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:
        [NSString stringWithUTF8String:mp4.c_str()]] options:nil];
    NSError* error = nil;
    AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
    AVAssetTrack* track = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
    if (!reader || !track || CMTimeGetSeconds(asset.duration) < 1.4) {
      std::cerr << "FAIL: readable video/duration\n"; return 1;
    }
    AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track
        outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)}];
    [reader addOutput:output];
    if (![reader startReading]) { std::cerr << "FAIL: decoder startup\n"; return 1; }
    bool red = false, blue = false;
    int count = 0;
    size_t initial_width = 0, initial_height = 0;
    while (CMSampleBufferRef sample = [output copyNextSampleBuffer]) {
      auto pixels = CMSampleBufferGetImageBuffer(sample);
      size_t w = CVPixelBufferGetWidth(pixels), h = CVPixelBufferGetHeight(pixels);
      if (!count) { initial_width = w; initial_height = h; }
      if (w != initial_width || h != initial_height) { std::cerr << "FAIL: changing encoded dimensions\n"; return 1; }
      CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
      auto p = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pixels)) +
          (h / 2) * CVPixelBufferGetBytesPerRow(pixels) + (w / 2) * 4;
      red |= p[2] > 180 && p[0] < 60;
      blue |= p[0] > 180 && p[2] < 60;
      CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
      CFRelease(sample); ++count;
    }
    if (reader.status != AVAssetReaderStatusCompleted || !red || !blue) {
      std::cerr << "FAIL: decoded content (red=" << red << ", blue=" << blue << ", frames=" << count << ")\n";
      return 1;
    }
    std::cout << "PASS: macOS real window screenshot, crop, independent PNG, H.264 decode, resize, static duration\n"
              << "ARTIFACTS " << directory.string() << '\n';
    return 0;
  }
}
