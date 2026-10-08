#import <Cocoa/Cocoa.h>
#include <spawn.h>
#include <sys/wait.h>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include "../src/capi/clipboard_c.h"
#include "../src/capi/common_c.h"
#include "../src/capi/image_c.h"
#include "../src/clipboard.h"
#include "../src/foundation/dispatcher.h"
#include "../src/image.h"
extern char** environ;

namespace {
void Check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
void PumpUntil(const std::function<bool()>& done) {
  auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done() && std::chrono::steady_clock::now() < until)
    nativeapi::RunMainThreadLoopFor(10);
  Check(done(), "callback timeout");
}
struct Restore {
  NSMutableArray<NSPasteboardItem*>* items = [NSMutableArray array];
  Restore() {
    for (NSPasteboardItem* item in [NSPasteboard generalPasteboard].pasteboardItems) {
      NSPasteboardItem* copy = [NSPasteboardItem new];
      for (NSString* type in item.types) {
        NSData* data = [item dataForType:type];
        Check(data != nil, "could not snapshot pasteboard");
        [copy setData:[data copy] forType:type];
      }
      [items addObject:copy];
    }
  }
  ~Restore() {
    NSPasteboard* board = [NSPasteboard generalPasteboard];
    [board clearContents];
    if (items.count)
      [board writeObjects:items];
  }
};
}  // namespace
int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      Restore restore;
      if (argc > 1) {
        pid_t child;
        Check(posix_spawnp(&child, argv[1], nullptr, nullptr, argv + 1, environ) == 0,
              "spawn test");
        int status;
        Check(waitpid(child, &status, 0) == child, "wait test");
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
      }
      auto& board = nativeapi::Clipboard::GetInstance();
      Check(board.Clear(), "clear");
      native_string_list_t malformed{};
      malformed.count = 1;
      Check(!native_clipboard_write_file_paths(malformed), "null C list rejected");
      malformed.count = -1;
      Check(!native_clipboard_write_file_paths(malformed), "negative C list rejected");
      Check(!native_clipboard_write_image(0xdeadbeef), "stale C image rejected");
      bool done = false;
      board.Read([&](bool ok, nativeapi::ClipboardData data) {
        Check(ok && !data.text && !data.html && !data.image && data.file_paths.empty(),
              "empty packet");
        done = true;
      });
      Check(!done, "read must be deferred");
      PumpUntil([&] { return done; });
      NSPasteboard* native = [NSPasteboard generalPasteboard];
      [native clearContents];
      [native setData:[NSData data] forType:@"org.nativeapi.test.unknown"];
      done = false;
      board.Read([&](bool ok, nativeapi::ClipboardData data) {
        Check(ok && !data.text && !data.html && !data.image && data.file_paths.empty(),
              "unknown native data ignored");
        done = true;
      });
      PumpUntil([&] { return done; });
      unichar embedded[] = {'a', 0, 'b'};
      [native clearContents];
      [native setString:[NSString stringWithCharacters:embedded length:3]
                forType:NSPasteboardTypeString];
      done = false;
      board.ReadText([&](bool ok, auto text) {
        Check(!ok && !text, "native NUL text rejected");
        done = true;
      });
      PumpUntil([&] { return done; });
      Check(board.WriteText(""), "empty text write");
      done = false;
      board.ReadText([&](bool ok, auto text) {
        Check(ok && text && text->empty(), "empty differs from absent");
        done = true;
      });
      PumpUntil([&] { return done; });
      nativeapi::ClipboardData input;
      input.text = u8"剪贴板 😀";
      input.html = u8"<b>剪贴板 😀</b>";
      input.file_paths = {"/tmp/剪贴板-file", "/tmp/directory"};
      input.image = nativeapi::Image::FromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC"
                                                 "0lEQVR42mNgAAIAAAUAAen63NgAAAAASUVORK5CYII=");
      Check(input.image != nullptr && board.Write(input), "multi-format publish");
      done = false;
      nativeapi::ClipboardData output;
      board.Read([&](bool ok, auto data) {
        Check(ok, "multi-format read");
        output = std::move(data);
        done = true;
      });
      PumpUntil([&] { return done; });
      Check(output.text == input.text && output.html == input.html &&
                output.file_paths == input.file_paths && output.image,
            "roundtrip");
      input = {};
      Check(output.image->GetSize().width == 1, "independent image");
      int completed = 0;
      for (int i = 0; i < 3; ++i)
        board.ReadText([&](bool ok, auto text) {
          Check(ok && text, "parallel reads");
          ++completed;
        });
      PumpUntil([&] { return completed == 3; });
      done = false;
      board.Read([&](bool ok, auto data) {
        Check(!ok && !data.text && !data.image && data.file_paths.empty(),
              "change must fail without partial data");
        done = true;
      });
      Check(board.WriteText("replacement"), "replace");
      PumpUntil([&] { return done; });
      done = false;
      board.Read([&](bool ok, auto data) {
        Check(ok && data.text == "replacement" && !data.html && !data.image &&
                  data.file_paths.empty(),
              "shortcut replaces all formats");
        done = true;
      });
      PumpUntil([&] { return done; });
      Check(!board.WriteText(std::string("bad\0text", 8)) && !board.WriteText("\xc0\xaf"),
            "UTF8/NUL rejected");
      Check(!board.WriteFilePaths({"relative"}), "relative path rejected");
      // C ABI lease remains alive after its callback returns; retain gives an independent image
      // handle.
      Check(board.WriteImage(output.image), "write image");
      struct Result {
        native_event_delivery_t lease = 0;
        native_image_t image = 0;
        bool done = false;
        int releases = 0;
      } result;
      native_clipboard_read_image(
          +[](bool ok, native_image_t image, native_event_delivery_t delivery, void* ptr) {
            auto& value = *static_cast<Result*>(ptr);
            Check(ok && image, "C image read");
            value.lease = delivery;
            value.image = image;
            value.done = true;
          },
          &result, +[](void* ptr) { ++static_cast<Result*>(ptr)->releases; });
      PumpUntil([&] { return result.done; });
      nativeapi::RunMainThreadLoopFor(10);
      Check(result.releases == 0 && native_event_delivery_is_active(result.lease),
            "lease keeps callback alive");
      auto retained = native_handle_retain(result.image);
      Check(retained != 0 && native_event_delivery_complete(result.lease, true),
            "lease completion");
      Check(!native_event_delivery_complete(result.lease, true), "completion exactly once");
      PumpUntil([&] { return result.releases == 1; });
      Check(native_image_get_size(retained).width == 1, "retained image survives ack");
      native_image_free(retained);
      int changes = 0;
      auto listener =
          board.AddListener<nativeapi::ClipboardChangedEvent>([&](const auto&) { ++changes; });
      Check(board.IsMonitoring(), "lazy monitoring starts");
      nativeapi::RunMainThreadLoopFor(300);
      Check(changes == 0, "no initial event");
      Check(board.Clear(), "clear monitored");
      PumpUntil([&] { return changes > 0; });
      board.RemoveListener(listener);
      Check(!board.IsMonitoring(), "last listener stops monitoring");
      listener =
          board.AddListener<nativeapi::ClipboardChangedEvent>([&](const auto&) { ++changes; });
      int previous = changes;
      Check(board.WriteText("re-subscribed"), "write after subscribe");
      PumpUntil([&] { return changes > previous; });
      board.RemoveListener(listener);
      std::cout << "clipboard macOS roundtrip, lease and monitoring passed\n";
      return 0;
    } catch (const std::exception& error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
}
