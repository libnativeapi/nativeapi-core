#import <Cocoa/Cocoa.h>

#include "../../clipboard_impl.h"
#include "drag_drop_utils_macos.h"
#include "string_utils_macos.h"

namespace nativeapi {
namespace {

bool BoardHasImage(NSPasteboard* board) {
  return [board availableTypeFromArray:[NSImage imageTypes]] != nil;
}

bool Has(NSPasteboard* board, NSPasteboardType type) {
  return [board availableTypeFromArray:@[ type ]] != nil;
}

}  // namespace

struct Clipboard::Impl::Platform {
  explicit Platform(Impl* impl) : impl(impl) {}
  Impl* impl;
  NSTimer* timer = nil;
  NSInteger revision = 0;
};

Clipboard::Impl::Impl(Clipboard* owner)
    : owner(owner), platform(std::make_unique<Platform>(this)) {}
Clipboard::Impl::~Impl() {
  Stop();
}
bool Clipboard::IsSupported() {
  return true;
}
bool Clipboard::IsChangeMonitoringSupported() {
  return true;
}

void Clipboard::Impl::Read(unsigned mask, std::function<void(bool, ClipboardData)> callback) {
  const NSInteger revision = [NSPasteboard generalPasteboard].changeCount;
  RunOnMainThread([mask, revision, callback = std::move(callback)] {
    @autoreleasepool {
      ClipboardData data;
      bool ok = true;
      @try {
        NSPasteboard* board = [NSPasteboard generalPasteboard];

        if ((mask & Bit(ClipboardReadFormat::Text)) && Has(board, NSPasteboardTypeString)) {
          NSString* text = [board stringForType:NSPasteboardTypeString];
          if (text && text.UTF8String)
            data.text = std::string(text.UTF8String,
                                    [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
          else
            ok = false;
        }
        if ((mask & Bit(ClipboardReadFormat::Html)) && Has(board, NSPasteboardTypeHTML)) {
          NSString* html = [board stringForType:NSPasteboardTypeHTML];
          if (html && html.UTF8String)
            data.html = std::string(html.UTF8String,
                                    [html lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
          else
            ok = false;
        }
        if ((mask & Bit(ClipboardReadFormat::Rtf)) && Has(board, NSPasteboardTypeRTF)) {
          NSString* rtf = [board stringForType:NSPasteboardTypeRTF];
          if (rtf && rtf.UTF8String)
            data.rtf = std::string(rtf.UTF8String,
                                   [rtf lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
          else
            ok = false;
        }
        if ((mask & Bit(ClipboardReadFormat::Image)) && BoardHasImage(board)) {
          NSImage* image = [[NSImage alloc] initWithPasteboard:board];
          NSBitmapImageRep* rep = [NSBitmapImageRep imageRepWithData:[image TIFFRepresentation]];
          NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
          if (png)
            data.image = Image::FromBase64(ToStdString([png base64EncodedStringWithOptions:0]));
          if (!data.image)
            ok = false;
          ReleaseIfManual(image);
        }
        if (mask & Bit(ClipboardReadFormat::FilePaths)) {
          NSArray* urls = [board readObjectsForClasses:@[ [NSURL class] ]
                                               options:@{
                                                 NSPasteboardURLReadingFileURLsOnlyKey : @YES
                                               }];
          for (NSURL* url in urls)
            if (url.isFileURL && (!url.host.length ||
                                  [url.host caseInsensitiveCompare:@"localhost"] == NSOrderedSame))
              data.file_paths.push_back(ToStdString(url.path));
        }
        ok = ok && revision == board.changeCount && clipboard_internal::ValidData(data);
      } @catch (NSException*) {
        ok = false;
      }
      callback(ok, ok ? std::move(data) : ClipboardData{});
    }
  });
}

bool Clipboard::Impl::Write(const ClipboardData& data) {
  @autoreleasepool {
    @try {
      // Prepare all native objects before clearing the old clipboard.
      NSMutableArray* items = [NSMutableArray array];
      if (data.text || data.html || data.rtf || data.image) {
        NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
        bool ok = true;
        if (data.text)
          ok = [item setString:ToNSString(*data.text) forType:NSPasteboardTypeString] && ok;
        if (data.html)
          ok = [item setString:ToNSString(*data.html) forType:NSPasteboardTypeHTML] && ok;
        if (data.rtf)
          ok = [item setString:ToNSString(*data.rtf) forType:NSPasteboardTypeRTF] && ok;
        if (data.image) {
          auto bytes = clipboard_internal::DecodeBase64(data.image->ToBase64());
          if (bytes.empty())
            ok = false;
          else
            ok = [item setData:[NSData dataWithBytes:bytes.data() length:bytes.size()]
                       forType:NSPasteboardTypePNG] &&
                 ok;
        }
        if (ok)
          [items addObject:item];
        ReleaseIfManual(item);
        if (!ok)
          return false;
      }
      for (const auto& path : data.file_paths) {
        NSURL* url = [NSURL fileURLWithPath:ToNSString(path)];
        if (!url)
          return false;
        // NSURL writes a separate native item for each file.
        [items addObject:url];
      }
      NSPasteboard* board = [NSPasteboard generalPasteboard];
      [board clearContents];
      return items.count == 0 || [board writeObjects:items];
    } @catch (NSException*) {
      return false;
    }
  }
}

void Clipboard::Impl::ReadCustomFormatBase64(
    const std::string& format,
    std::function<void(bool, std::optional<std::string>)> callback) {
  const NSInteger revision = [NSPasteboard generalPasteboard].changeCount;
  RunOnMainThread([format, revision, callback = std::move(callback)] {
    @autoreleasepool {
      std::optional<std::string> result;
      bool ok = true;
      @try {
        NSPasteboard* board = [NSPasteboard generalPasteboard];
        NSPasteboardType type = ToNSString(format);
        if (Has(board, type)) {
          NSData* data = [board dataForType:type];
          if (data) {
            result = clipboard_internal::Base64(static_cast<const unsigned char*>(data.bytes),
                                                data.length);
          } else {
            ok = false;
          }
        }
        ok = ok && revision == board.changeCount;
      } @catch (NSException*) {
        ok = false;
      }
      callback(ok, ok ? std::move(result) : std::nullopt);
    }
  });
}

bool Clipboard::Impl::WriteCustomFormatBase64(const std::string& format,
                                              const std::string& base64_data) {
  @autoreleasepool {
    @try {
      auto bytes = clipboard_internal::DecodeBase64(base64_data);
      if (bytes.empty() && !base64_data.empty())
        return false;
      NSPasteboard* board = [NSPasteboard generalPasteboard];
      NSPasteboardType type = ToNSString(format);
      NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
      [board clearContents];
      return [board setData:data forType:type] == YES;
    } @catch (NSException*) {
      return false;
    }
  }
}

int64_t Clipboard::Impl::GetChangeCount() const {
  return static_cast<int64_t>([NSPasteboard generalPasteboard].changeCount);
}

bool Clipboard::Impl::IsConcealed() const {
  @autoreleasepool {
    NSPasteboard* board = [NSPasteboard generalPasteboard];
    NSArray<NSPasteboardType>* markers = @[
      @"org.nspasteboard.ConcealedType",
      @"org.nspasteboard.AutoGeneratedType",
      @"com.agilebits.onepassword",
    ];
    return [board availableTypeFromArray:markers] != nil;
  }
}

bool Clipboard::Impl::HasFormat(ClipboardReadFormat format) const {
  @autoreleasepool {
    NSPasteboard* board = [NSPasteboard generalPasteboard];
    switch (format) {
      case ClipboardReadFormat::Text:
        return Has(board, NSPasteboardTypeString);
      case ClipboardReadFormat::Html:
        return Has(board, NSPasteboardTypeHTML);
      case ClipboardReadFormat::Rtf:
        return Has(board, NSPasteboardTypeRTF);
      case ClipboardReadFormat::Image:
        return BoardHasImage(board);
      case ClipboardReadFormat::FilePaths:
        return Has(board, NSPasteboardTypeFileURL);
      default:
        return false;
    }
  }
}

bool Clipboard::Impl::HasCustomFormat(const std::string& format) const {
  @autoreleasepool {
    NSPasteboard* board = [NSPasteboard generalPasteboard];
    return Has(board, ToNSString(format));
  }
}

bool Clipboard::Impl::Start() {
  if (platform->timer)
    return true;
  platform->revision = [NSPasteboard generalPasteboard].changeCount;
  auto* state = platform.get();
  platform->timer = RetainIfManual([NSTimer
      timerWithTimeInterval:0.1
                    repeats:YES
                      block:^(NSTimer*) {
                        const NSInteger revision = [NSPasteboard generalPasteboard].changeCount;
                        if (state->revision != revision) {
                          state->revision = revision;
                          state->impl->Changed();
                        }
                      }]);
  [[NSRunLoop mainRunLoop] addTimer:platform->timer forMode:NSRunLoopCommonModes];
  return platform->timer != nil;
}

void Clipboard::Impl::Stop() {
  if (platform->timer) {
    [platform->timer invalidate];
    ReleaseIfManual(platform->timer);
    platform->timer = nil;
  }
  monitoring = false;
}

}  // namespace nativeapi
