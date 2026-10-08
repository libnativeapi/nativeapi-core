#include <iostream>
#include "../../src/clipboard.h"
#include "../../src/foundation/dispatcher.h"

int main(int argc, char** argv) {
  auto& clipboard = nativeapi::Clipboard::GetInstance();
  if (!nativeapi::Clipboard::IsSupported())
    return 1;
  // Passing an argument copies it; otherwise inspect the existing clipboard.
  if (argc > 1 && !clipboard.WriteText(argv[1]))
    return 1;
  bool done = false;
  bool success = false;
  clipboard.Read([&](bool ok, nativeapi::ClipboardData data) {
    success = ok;
    if (ok)
      std::cout << "text=" << bool(data.text) << " html=" << bool(data.html)
                << " image=" << bool(data.image) << " files=" << data.file_paths.size() << '\n';
    done = true;
  });
  while (!done)
    nativeapi::RunMainThreadLoopFor(10);
  return success ? 0 : 1;
}
