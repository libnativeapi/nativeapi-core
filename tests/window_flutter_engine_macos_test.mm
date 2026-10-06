// Actual Flutter/Dart/nativeapi binding integration. Suppress window ordering
// only in this fixture process; no windows become visible and no input is sent.
#import <Cocoa/Cocoa.h>
#import <FlutterMacOS/FlutterMacOS.h>
#import <objc/runtime.h>
#include <iostream>

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc != 2)
      return 1;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    Method order = class_getInstanceMethod(NSWindow.class, @selector(orderWindow:relativeTo:));
    IMP original_order = method_setImplementation(
        order, imp_implementationWithBlock(^(NSWindow*, NSWindowOrderingMode, NSInteger){
               }));
    Method key = class_getInstanceMethod(NSWindow.class, @selector(makeKeyWindow));
    IMP original_key = method_setImplementation(key, imp_implementationWithBlock(^(NSWindow*){
                                                     }));
    auto* assets = [NSBundle bundleWithPath:[NSString stringWithUTF8String:argv[1]]];
    auto* project = [[FlutterDartProject alloc] initWithPrecompiledDartBundle:assets];
    auto* engine = [[FlutterEngine alloc] initWithName:@"nativeapi-window-close-test"
                                               project:project
                                allowHeadlessExecution:YES];
    __block bool finished = false;
    __block int checks = 0, failures = 0;
    auto* channel = [FlutterMethodChannel methodChannelWithName:@"nativeapi/test-window-close"
                                                binaryMessenger:engine.binaryMessenger];
    [channel setMethodCallHandler:^(FlutterMethodCall* call, FlutterResult result) {
      if ([call.method isEqualToString:@"ready"])
        result(nil);
      else if ([call.method isEqualToString:@"perform"]) {
        auto* target =
            (__bridge NSWindow*)(void*)(uintptr_t)[call.arguments[@"handle"] unsignedLongLongValue];
        if (![NSApp.windows containsObject:target]) {
          result([FlutterError errorWithCode:@"invalid-window"
                                     message:@"target is not owned by this fixture"
                                     details:nil]);
          return;
        }
        __block bool closed = false;
        auto* observer =
            [NSNotificationCenter.defaultCenter addObserverForName:NSWindowWillCloseNotification
                                                            object:target
                                                             queue:nil
                                                        usingBlock:^(NSNotification*) {
                                                          closed = true;
                                                        }];
        if ([call.arguments[@"force"] boolValue])
          [target close];
        else
          [target performClose:nil];
        [NSNotificationCenter.defaultCenter removeObserver:observer];
        result(@(closed));
      } else if ([call.method isEqualToString:@"check"]) {
        ++checks;
        bool hidden = true;
        for (NSWindow* window in NSApp.windows)
          hidden &= !window.visible;
        bool ok = [call.arguments[@"ok"] boolValue] && hidden;
        failures += !ok;
        std::cout << (ok ? "PASS " : "FAIL ") << [call.arguments[@"label"] UTF8String] << std::endl;
        result(nil);
      } else if ([call.method isEqualToString:@"complete"]) {
        result(nil);
        // Let the last Dart method reply and queued native releases finish
        // before destroying the engine that owns their platform channel.
        dispatch_async(dispatch_get_main_queue(), ^{
          finished = true;
        });
      } else if ([call.method isEqualToString:@"failed"]) {
        ++failures;
        finished = true;
        std::cerr << "FAIL Dart binding: " << [call.arguments UTF8String] << std::endl;
        result(nil);
      } else
        result(FlutterMethodNotImplemented);
    }];
    if (![engine runWithEntrypoint:nil])
      return 2;
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:20];
    while (!finished && deadline.timeIntervalSinceNow > 0)
      [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode
                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    if (!finished || checks < 19)
      ++failures;
    [channel setMethodCallHandler:nil];
    [engine shutDownEngine];
    method_setImplementation(order, original_order);
    method_setImplementation(key, original_key);
    return failures ? 3 : 0;
  }
}
