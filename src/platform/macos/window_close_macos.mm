#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
extern const void* kWindowIdKey;
#include "../../window_close_dispatch.h"

namespace {
const void* kPolicy = &kPolicy;
const void* kPerformInstalled = &kPerformInstalled;
const void* kButtonCloseInstalled = &kButtonCloseInstalled;
const void* kCloseInstalled = &kCloseInstalled;
struct ObjcRef {
  id value;
  explicit ObjcRef(id object) : value(object) {
#if !__has_feature(objc_arc)
    [value retain];
#endif
  }
  ~ObjcRef() {
#if !__has_feature(objc_arc)
    [value release];
#endif
  }
};
struct Policy : std::enable_shared_from_this<Policy> {
  NSHashTable* target;
  id observer = nil;
  nativeapi::WindowId id;
  std::shared_ptr<nativeapi::detail::WindowCloseState> state;
  bool bypass = false;
  explicit Policy(NSWindow* window, nativeapi::WindowId identity) : id(identity) {
    target = [[NSHashTable alloc] initWithOptions:NSPointerFunctionsWeakMemory capacity:1];
    [target addObject:window];
  }
  ~Policy() {
    if (state)
      state->Invalidate();
    if (observer)
      [[NSNotificationCenter defaultCenter] removeObserver:observer];
#if !__has_feature(objc_arc)
    [observer release];
    [target release];
#endif
  }
  NSWindow* Window() const { return [target anyObject]; }
  bool Valid() const;
  bool Request(std::function<void(NSWindow*)> action) {
    std::weak_ptr<Policy> weak = shared_from_this();
    return state->Request([weak, action] {
      auto policy = weak.lock();
      if (!policy || !policy->Valid())
        return;
      ObjcRef target(policy->Window());
      const bool previous = policy->bypass;
      policy->bypass = true;
      @try {
        action((NSWindow*)target.value);
      } @finally {
        policy->bypass = previous;
      }
    });
  }
};
}  // namespace

@interface NativeApiWindowCloseBox : NSObject {
 @public
  std::shared_ptr<Policy> policy;
}
@end
@implementation NativeApiWindowCloseBox
@end

namespace {
bool Policy::Valid() const {
  NSWindow* window = Window();
  auto* box = window ? (NativeApiWindowCloseBox*)objc_getAssociatedObject(window, kPolicy) : nil;
  auto current = window ? (NSNumber*)objc_getAssociatedObject(window, kWindowIdKey) : nil;
  return window && current && [current unsignedLongLongValue] == id && state->IsAlive() && box &&
         box->policy.get() == this;
}
void InstallHooks(NSWindow* window) {
  Class cls = object_getClass(window);
  if (!objc_getAssociatedObject(cls, kPerformInstalled)) {
    SEL selector = @selector(performClose:);
    Method method = class_getInstanceMethod(cls, selector);
    auto original = reinterpret_cast<void (*)(id, SEL, id)>(method_getImplementation(method));
    IMP replacement = imp_implementationWithBlock(^(NSWindow* instance, id sender) {
      auto* box = (NativeApiWindowCloseBox*)objc_getAssociatedObject(instance, kPolicy);
      auto policy = box ? box->policy : nullptr;
      if (!policy || !policy->Valid() || policy->bypass) {
        original(instance, selector, sender);
        return;
      }
      try {
        if (!policy->state->HasObservers() && !policy->state->IsPending()) {
          original(instance, selector, sender);
          return;
        }
        ObjcRef keep_target(instance);
        auto keep_sender = std::make_shared<ObjcRef>(sender);
        (void)policy->Request([original, selector, keep_sender](NSWindow* target) {
          original(target, selector, keep_sender->value);
        });
      } catch (...) {
      }  // A failed confirmation cannot silently close.
    });
    if (!class_addMethod(cls, selector, replacement, method_getTypeEncoding(method)))
      class_replaceMethod(cls, selector, replacement, method_getTypeEncoding(method));
    objc_setAssociatedObject(cls, kPerformInstalled, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  // The title bar's close button does not go through performClose: on every
  // macOS: on macOS 26 it sends the private -__close, which calls -close
  // directly, the path that cannot be cancelled. Confirm it the same way as
  // performClose:; where the selector does not exist there is nothing to hook.
  SEL button_close = NSSelectorFromString(@"__close");
  if (!objc_getAssociatedObject(cls, kButtonCloseInstalled) &&
      class_getInstanceMethod(cls, button_close)) {
    Method method = class_getInstanceMethod(cls, button_close);
    auto original = reinterpret_cast<void (*)(id, SEL)>(method_getImplementation(method));
    IMP replacement = imp_implementationWithBlock(^(NSWindow* instance) {
      auto* box = (NativeApiWindowCloseBox*)objc_getAssociatedObject(instance, kPolicy);
      auto policy = box ? box->policy : nullptr;
      if (!policy || !policy->Valid() || policy->bypass) {
        original(instance, button_close);
        return;
      }
      try {
        if (!policy->state->HasObservers() && !policy->state->IsPending()) {
          original(instance, button_close);
          return;
        }
        ObjcRef keep_target(instance);
        (void)policy->Request(
            [original, button_close](NSWindow* target) { original(target, button_close); });
      } catch (...) {
      }  // A failed confirmation cannot silently close.
    });
    if (!class_addMethod(cls, button_close, replacement, method_getTypeEncoding(method)))
      class_replaceMethod(cls, button_close, replacement, method_getTypeEncoding(method));
    objc_setAssociatedObject(cls, kButtonCloseInstalled, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  if (!objc_getAssociatedObject(cls, kCloseInstalled)) {
    SEL selector = @selector(close);
    Method method = class_getInstanceMethod(cls, selector);
    auto original = reinterpret_cast<void (*)(id, SEL)>(method_getImplementation(method));
    IMP replacement = imp_implementationWithBlock(^(NSWindow* instance) {
      auto* box = (NativeApiWindowCloseBox*)objc_getAssociatedObject(instance, kPolicy);
      auto policy = box ? box->policy : nullptr;
      if (!policy || policy->bypass || !policy->state->IsAlive()) {
        original(instance, selector);
        return;
      }
      ObjcRef keep_target(instance);
      // Direct close is the host's force path (including app termination).
      try {
        policy->state->NotifyRequired();
      } catch (...) {
      }  // Forced native close must continue even if notification fails.
      const bool previous = policy->bypass;
      policy->bypass = true;
      @try {
        original(instance, selector);
      } @finally {
        policy->bypass = previous;
      }
    });
    if (!class_addMethod(cls, selector, replacement, method_getTypeEncoding(method)))
      class_replaceMethod(cls, selector, replacement, method_getTypeEncoding(method));
    objc_setAssociatedObject(cls, kCloseInstalled, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
}
std::shared_ptr<Policy> GetPolicy(void* native, nativeapi::WindowId id) {
  if (!native || ![NSThread isMainThread])
    return nullptr;
  NSWindow* window = (__bridge NSWindow*)native;
  auto current = (NSNumber*)objc_getAssociatedObject(window, kWindowIdKey);
  if (!current || [current unsignedLongLongValue] != id)
    return nullptr;
  auto* existing = (NativeApiWindowCloseBox*)objc_getAssociatedObject(window, kPolicy);
  if (existing) {
    if (existing->policy->id == id) {
      if (!existing->policy->Valid())
        return nullptr;
      InstallHooks(window);
      return existing->policy;
    }
    existing->policy->state->Invalidate();
    objc_setAssociatedObject(window, kPolicy, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  auto policy = std::make_shared<Policy>(window, id);
  std::weak_ptr<Policy> weak = policy;
  policy->state = nativeapi::detail::WindowCloseState::Create(
      id,
      [weak](auto work) {
        auto policy = weak.lock();
        if (!policy)
          return false;
        if ([NSThread isMainThread]) {
          work();
          return true;
        }
        return nativeapi::RunOnMainThread(std::move(work));
      },
      [weak] {
        auto policy = weak.lock();
        return policy && policy->Valid();
      });
  auto* box = [[NativeApiWindowCloseBox alloc] init];
  box->policy = policy;
  objc_setAssociatedObject(window, kPolicy, box, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
#if !__has_feature(objc_arc)
  [box release];
#endif
  auto observer =
      [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillCloseNotification
                                                        object:window
                                                         queue:nil
                                                    usingBlock:^(NSNotification*) {
                                                      if (auto policy = weak.lock())
                                                        policy->state->Invalidate();
                                                    }];
  policy->observer = observer;
#if !__has_feature(objc_arc)
  [observer retain];
#endif
  InstallHooks(window);
  return policy;
}
}  // namespace

namespace nativeapi::detail {
std::shared_ptr<WindowCloseState> GetNativeWindowCloseState(void* window, WindowId id) {
  auto policy = GetPolicy(window, id);
  return policy ? policy->state : nullptr;
}
bool RequestNativeWindowClose(void* window, WindowId id) {
  auto policy = GetPolicy(window, id);
  return policy && policy->Request([](NSWindow* target) { [target performClose:nil]; });
}
bool IsNativeWindowCloseSupported() {
  return true;
}
void RefreshNativeWindowCloseHooks(void* window) {
  if (window && objc_getAssociatedObject((__bridge NSWindow*)window, kPolicy))
    InstallHooks((__bridge NSWindow*)window);
}
}  // namespace nativeapi::detail

namespace nativeapi {
bool Window::DispatchWindowTask(std::function<void()> task) {
  if ([NSThread isMainThread]) {
    task();
    return true;
  }
  return RunOnMainThread(std::move(task));
}
}  // namespace nativeapi
