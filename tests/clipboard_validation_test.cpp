#include <cstdlib>
#include <iostream>
#include "../src/clipboard_impl.h"

int main() {
  using nativeapi::clipboard_internal::ValidData;
  using nativeapi::clipboard_internal::ValidString;
  auto check = [](bool value) {
    if (!value)
      std::abort();
  };
  check(ValidString(""));
  check(ValidString("\xe5\x89\xaa\xe8\xb4\xb4\xe6\x9d\xbf \xf0\x9f\x98\x80 \xc3\xa9"));
  check(!ValidString(std::string("a\0b", 3)));
  for (const auto& value : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x80"})
    check(!ValidString(value));
  nativeapi::ClipboardData data;
  check(ValidData(data));
  data.text = "";
  check(ValidData(data));
  data.html = std::string("\0", 1);
  check(!ValidData(data));
  data.html.reset();
  data.file_paths = {"relative/file"};
  check(!ValidData(data));
#ifdef _WIN32
  data.file_paths = {"C:\\does-not-exist\\file.txt", "C:\\directory"};
#else
  data.file_paths = {"/does-not-exist/file.txt", "/directory"};
#endif
  check(ValidData(data));
  std::cout << "clipboard validation passed\n";
}
