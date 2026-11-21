#import <UIKit/UIKit.h>
#include <dispatch/dispatch.h>
#include "../../clipboard_impl.h"

namespace nativeapi {
namespace {
// Mobile bindings can enter from a Dart/JS worker; UIKit always runs on the UI thread.
void OnUI(const std::function<void()>& work) {
  if ([NSThread isMainThread])
    work();
  else
    dispatch_sync(dispatch_get_main_queue(), ^{
      work();
    });
}
template <typename T>
T Retain(T value) {
#if !__has_feature(objc_arc)
  [value retain];
#endif
  return value;
}
template <typename T>
void Release(T value) {
#if !__has_feature(objc_arc)
  [value release];
#endif
}
NSString* NativeString(const std::string& value) {
  return [NSString stringWithUTF8String:value.c_str()];
}
std::string Text(NSString* value) {
  const char* bytes = value.UTF8String;
  return bytes ? std::string(bytes, [value lengthOfBytesUsingEncoding:NSUTF8StringEncoding]) : "";
}
}  // namespace
struct Clipboard::Impl::Platform {
  struct Watch {
    Impl* impl;
    NSInteger revision;
    bool active = true;
  };
  std::shared_ptr<Watch> watch;
  id changed = nil;
  id activated = nil;
};
Clipboard::Impl::Impl(Clipboard* owner) : owner(owner), platform(std::make_unique<Platform>()) {}
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
  RunOnMainThread([mask, callback = std::move(callback)] {
    @autoreleasepool {
      ClipboardData data;
      bool ok = true;
      @try {
        UIPasteboard* board = UIPasteboard.generalPasteboard;
        const NSInteger revision = board.changeCount;
        if ((mask & Bit(ClipboardReadFormat::Text)) && board.hasStrings) {
          NSString* text = board.string;
          if (text) {
            if (text.UTF8String)
              data.text = Text(text);
            else
              ok = false;
          } else {
            ok = false;
          }
        }
        if ((mask & Bit(ClipboardReadFormat::Html)) &&
            [board containsPasteboardTypes:@[ @"public.html" ]]) {
          NSData* html = [board dataForPasteboardType:@"public.html"];
          NSString* text =
              html ? [[NSString alloc] initWithData:html encoding:NSUTF8StringEncoding] : nil;
          if (text && text.UTF8String)
            data.html = Text(text);
          else
            ok = false;
          Release(text);
        }
        if ((mask & Bit(ClipboardReadFormat::Image)) && board.hasImages) {
          NSData* png = UIImagePNGRepresentation(board.image);
          if (png)
            data.image = Image::FromBase64(Text([png base64EncodedStringWithOptions:0]));
          if (!data.image)
            ok = false;
        }
        if (mask & Bit(ClipboardReadFormat::FilePaths)) {
          NSArray<NSURL*>* urls = board.URLs;
          if (board.hasURLs && !urls)
            ok = false;
          for (NSURL* url in urls) {
            if (url.isFileURL && (!url.host.length ||
                                  [url.host caseInsensitiveCompare:@"localhost"] == NSOrderedSame))
              data.file_paths.push_back(Text(url.path));
          }
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
  bool result = false;
  OnUI([&] {
    @autoreleasepool {
      @try {
        NSMutableArray* items = [NSMutableArray array];
        NSMutableDictionary* item = [NSMutableDictionary dictionary];
        if (data.text)
          item[@"public.utf8-plain-text"] = NativeString(*data.text);
        if (data.html)
          item[@"public.html"] = [NativeString(*data.html) dataUsingEncoding:NSUTF8StringEncoding];
        if (data.image) {
          auto bytes = clipboard_internal::DecodeBase64(data.image->ToBase64());
          if (bytes.empty())
            return;
          item[@"public.png"] = [NSData dataWithBytes:bytes.data() length:bytes.size()];
        }
        if (item.count)
          [items addObject:item];
        for (const auto& path : data.file_paths)
          [items addObject:@{@"public.url" : [NSURL fileURLWithPath:NativeString(path)]}];
        UIPasteboard.generalPasteboard.items = items;
        result = true;
      } @catch (NSException*) {
        result = false;
      }
    }
  });
  return result;
}
void Clipboard::Impl::ReadCustomFormatBase64(
    const std::string& format,
    std::function<void(bool, std::optional<std::string>)> callback) {
  RunOnMainThread([format, callback = std::move(callback)] {
    @autoreleasepool {
      UIPasteboard* board = [UIPasteboard generalPasteboard];
      NSString* type = [NSString stringWithUTF8String:format.c_str()];
      NSData* data = type ? [board dataForPasteboardType:type] : nil;
      if (!data) {
        callback(true, std::nullopt);
        return;
      }
      callback(true, clipboard_internal::Base64(static_cast<const unsigned char*>(data.bytes),
                                                data.length));
    }
  });
}

bool Clipboard::Impl::WriteCustomFormatBase64(const std::string& format,
                                              const std::string& base64_data) {
  @autoreleasepool {
    auto bytes = clipboard_internal::DecodeBase64(base64_data);
    if (bytes.empty() && !base64_data.empty())
      return false;
    NSString* type = [NSString stringWithUTF8String:format.c_str()];
    if (!type)
      return false;
    NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    [[UIPasteboard generalPasteboard] setData:data forPasteboardType:type];
    return true;
  }
}

int64_t Clipboard::Impl::GetChangeCount() const {
  return static_cast<int64_t>([UIPasteboard generalPasteboard].changeCount);
}

bool Clipboard::Impl::IsConcealed() const {
  return false;
}

bool Clipboard::Impl::HasFormat(ClipboardReadFormat format) const {
  @autoreleasepool {
    UIPasteboard* board = [UIPasteboard generalPasteboard];
    switch (format) {
      case ClipboardReadFormat::Text:
        return board.hasStrings == YES;
      case ClipboardReadFormat::Html:
        return [board containsPasteboardTypes:@[ @"public.html" ]] == YES;
      case ClipboardReadFormat::Rtf:
        return [board containsPasteboardTypes:@[ @"public.rtf" ]] == YES;
      case ClipboardReadFormat::Image:
        return board.hasImages == YES;
      case ClipboardReadFormat::FilePaths:
        return board.hasURLs == YES;
      default:
        return false;
    }
  }
}

bool Clipboard::Impl::HasCustomFormat(const std::string& format) const {
  @autoreleasepool {
    NSString* type = [NSString stringWithUTF8String:format.c_str()];
    return type != nil && [[UIPasteboard generalPasteboard] containsPasteboardTypes:@[ type ]] == YES;
  }
}

bool Clipboard::Impl::Start() {
  bool result = false;
  OnUI([&] {
    if (platform->changed) {
      result = true;
      return;
    }
    auto state = std::make_shared<Platform::Watch>(
        Platform::Watch{this, UIPasteboard.generalPasteboard.changeCount});
    platform->watch = state;
    auto check = ^(NSNotification*) {
      if (!state->active)
        return;
      const NSInteger revision = UIPasteboard.generalPasteboard.changeCount;
      if (revision != state->revision) {
        state->revision = revision;
        state->impl->Changed();
      }
    };
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    platform->changed = Retain([center addObserverForName:UIPasteboardChangedNotification
                                                   object:nil
                                                    queue:NSOperationQueue.mainQueue
                                               usingBlock:check]);
    platform->activated = Retain([center addObserverForName:UIApplicationDidBecomeActiveNotification
                                                     object:nil
                                                      queue:NSOperationQueue.mainQueue
                                                 usingBlock:check]);
    result = platform->changed && platform->activated;
  });
  return result;
}
void Clipboard::Impl::Stop() {
  OnUI([&] {
    if (platform->watch)
      platform->watch->active = false;
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    if (platform->changed)
      [center removeObserver:platform->changed];
    if (platform->activated)
      [center removeObserver:platform->activated];
    Release(platform->changed);
    Release(platform->activated);
    platform->changed = nil;
    platform->activated = nil;
    platform->watch.reset();
  });
  monitoring = false;
}
}  // namespace nativeapi
