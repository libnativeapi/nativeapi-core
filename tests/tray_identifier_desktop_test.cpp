#include "nativeapi.h"
#include "../src/capi/tray_icon_c.h"
#include "../src/capi/string_utils_c.h"
#include <iostream>
#include <string>
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif

int main() {
#ifdef __APPLE__
  @autoreleasepool {
  [NSApplication sharedApplication];
#endif
  const std::string identifier = "org.nativeapi.TrayIdentifierTest.\xE5\x90\x8C\xE6\xAD\xA5";
  nativeapi::TrayIcon icon(identifier);
  int failures = 0;
  auto check = [&](bool ok, const char* name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) ++failures;
  };
  check(icon.GetIdentifier() == identifier, "explicit UTF-8 identifier round trip");
  const auto numeric_id = icon.GetId();
#ifdef __APPLE__
  NSStatusItem* item = (__bridge NSStatusItem*)icon.GetNativeObject();
  check(std::string(item.autosaveName.UTF8String) == identifier, "native autosaveName set");
  item.visible = NO;
#endif
  icon.SetTitle("Identifier test");
  icon.SetVisible(false);
  check(icon.GetIdentifier() == identifier, "visibility and title preserve identifier");
  check(icon.GetId() == numeric_id, "numeric identity unchanged");
  const auto handle = native_tray_icon_create_with_identifier(identifier.c_str());
  check(handle != 0, "C ABI constructor");
  char* name = native_tray_icon_get_identifier(handle);
  check(name && identifier == name, "C ABI UTF-8 string round trip");
  free_c_str(name);
  native_tray_icon_free(handle);
  std::cout << (failures ? "FAILURES\n" : "ALL PASS\n");
  return failures ? 1 : 0;
#ifdef __APPLE__
  }
#endif
}
