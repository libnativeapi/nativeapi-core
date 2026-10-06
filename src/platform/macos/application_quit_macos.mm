#include "application_quit_macos.h"
#import <objc/message.h>
#import <objc/runtime.h>
#include <cstring>
#include <string>
#include "../../application.h"
#include "../../application_quit_dispatch.h"
#include "../../foundation/dispatcher.h"

namespace {
using Dispatch = nativeapi::detail::ApplicationQuitDispatch;
struct ObjectRef {
  id value;
  explicit ObjectRef(id object) : value(object) {
#if !__has_feature(objc_arc)
    [value retain];
#endif
  }
  ~ObjectRef() {
#if !__has_feature(objc_arc)
    [value release];
#endif
  }
};
struct WeakObject {
  NSHashTable* objects = [[NSHashTable alloc] initWithOptions:NSPointerFunctionsWeakMemory
                                                     capacity:1];
  explicit WeakObject(id object) {
    if (object)
      [objects addObject:object];
  }
  id Get() const { return [objects anyObject]; }
  void Reset(id object) {
    [objects removeAllObjects];
    if (object)
      [objects addObject:object];
  }
  ~WeakObject() {
#if !__has_feature(objc_arc)
    [objects release];
#endif
  }
};
struct Session {
  WeakObject host;
  std::shared_ptr<nativeapi::EventRequest> request;
  bool asking = true, decided = false, approved = false, waiting_host = false;
  explicit Session(id target) : host(target) {}
};
struct FlutterBridge;
struct Ticket {
  std::weak_ptr<Session> session;
  std::weak_ptr<FlutterBridge> bridge;
  std::shared_ptr<ObjectRef> sender;
  bool required = false, has_session = false, completed = false;
};
}  // namespace
@interface NativeApiQuitTicket : NSObject {
 @public
  std::shared_ptr<Ticket> ticket;
}
@end
@implementation NativeApiQuitTicket
@end

namespace nativeapi::detail {
struct MacApplicationQuitPolicy::Impl {
  Application* app;
  MacApplicationQuitPolicy* owner;
  std::shared_ptr<Session> session;
  std::shared_ptr<Session> authorized;
  int entering = 0, forced = 0;
  bool announced_termination = false;
  Impl(Application* value, MacApplicationQuitPolicy* parent) : app(value), owner(parent) {}
  bool Interested() const {
    return app && (app->GetListenerCount<ApplicationEvent>() ||
                   app->GetListenerCount<ApplicationQuitRequestedEvent>());
  }
  bool Current(const std::shared_ptr<Session>& target) const {
    return app && target && session == target && NSApp.delegate == target->host.Get() &&
           target->request && Dispatch::CurrentRequest() == target->request;
  }
  void End(const std::shared_ptr<Session>& target) {
    if (!target)
      return;
    Dispatch::ReleaseFromHost(target->request);
    if (session == target)
      session.reset();
  }
  NSApplicationTerminateReply Continue(
      const std::shared_ptr<Session>& target,
      const std::function<NSApplicationTerminateReply()>& original) {
    owner->ObserveHost(target->host.Get());
    target->waiting_host = false;
    ++entering;
    NSApplicationTerminateReply reply;
    @try {
      reply = original();
    } @finally {
      --entering;
    }
    if (reply == NSTerminateNow) {
      announced_termination = true;
      End(target);
    } else if (reply == NSTerminateLater) {
      target->waiting_host = true;
    } else if (!target->waiting_host) {
      End(target);  // An ordinary host cancellation, without an async bridge.
    }
    return reply;
  }
  void Resume(const std::shared_ptr<Session>& target) {
    if (!Current(target)) {
      End(target);
      return;
    }
    if (!target->approved)
      return;
    auto previous = authorized;
    authorized = target;
    @try {
      [NSApp terminate:nil];
    } @finally {
      authorized = previous;
    }
  }
  void FlutterResult(const std::shared_ptr<Ticket>& ticket, id response);
  void FlutterTerminate(const std::shared_ptr<Ticket>& ticket, id handler, id original);
  void Required() {
    session.reset();
    authorized.reset();
    announced_termination = true;
    Dispatch::NotifyRequired();
  }
};
}  // namespace nativeapi::detail

namespace {
using Policy = nativeapi::detail::MacApplicationQuitPolicy;
const void* kBridge = &kBridge;
const void* kReplyPolicy = &kReplyPolicy;
struct FlutterBridge : std::enable_shared_from_this<FlutterBridge> {
  std::weak_ptr<Policy> policy;
  WeakObject host;
  WeakObject handler;
  std::shared_ptr<ObjectRef> original;
  std::function<std::shared_ptr<Ticket>(id, NSInteger)> make_ticket;
  std::function<void(const std::shared_ptr<Ticket>&, id)> result;
  std::function<void(const std::shared_ptr<Ticket>&, id, id)> terminate;
  FlutterBridge(id target, id coordinator) : host(target), handler(coordinator) {}
};
}  // namespace
@interface NativeApiFlutterQuitBridge : NSObject {
 @public
  std::shared_ptr<FlutterBridge> bridge;
}
@end
@implementation NativeApiFlutterQuitBridge
@end

namespace {
using Result = void (^)(id);
using Terminator = void (^)(id);
id HandlerFor(id host) {
  Class flutter = NSClassFromString(@"FlutterAppDelegate");
  SEL getter = NSSelectorFromString(@"terminationHandler");
  if (!flutter || ![host isKindOfClass:flutter] || ![host respondsToSelector:getter])
    return nil;
  return reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(host, getter);
}
bool ShouldTerminate(id handler) {
  return reinterpret_cast<BOOL (*)(id, SEL)>(objc_msgSend)(handler, @selector(shouldTerminate));
}
void SetShouldTerminate(id handler, BOOL value) {
  // The engine exposes a readonly property and writes this BOOL directly.
  // Its layout and encoding are validated before installing the adapter.
  Ivar flag = class_getInstanceVariable(object_getClass(handler), "_shouldTerminate");
  std::memcpy(static_cast<unsigned char*>((__bridge void*)handler) + ivar_getOffset(flag), &value,
              sizeof(value));
}
void InstallFlutterBridge(const std::shared_ptr<FlutterBridge>& bridge) {
  id handler = bridge->handler.Get();
  Class cls = object_getClass(handler);
  SEL selector = NSSelectorFromString(@"requestApplicationTermination:exitType:result:");
  static const void* installed = &installed;
  if (!objc_getAssociatedObject(cls, installed)) {
    Method method = class_getInstanceMethod(cls, selector);
    auto original = reinterpret_cast<void (*)(id, SEL, id, NSInteger, Result)>(
        method_getImplementation(method));
    IMP replacement =
        imp_implementationWithBlock(^(id instance, id sender, NSInteger type, Result callback) {
          auto* box = (NativeApiFlutterQuitBridge*)objc_getAssociatedObject(instance, kBridge);
          auto active = box ? box->bridge : nullptr;
          std::shared_ptr<Ticket> ticket;
          try {
            if (active && NSApp.delegate == active->host.Get() &&
                ![sender isKindOfClass:[NativeApiQuitTicket class]])
              ticket = active->make_ticket(sender, type);
          } catch (...) {
            // Failure to observe must not suppress the engine's required exit.
          }
          if (!ticket) {
            original(instance, selector, sender, type, callback);
            return;
          }
          auto* token = [[NativeApiQuitTicket alloc] init];
          token->ticket = ticket;
          Result reply = ^(id value) {
            if (auto live = ticket->bridge.lock())
              live->result(ticket, value);
            if (callback)
              callback(value);
          };
          original(instance, selector, token, type, reply);
#if !__has_feature(objc_arc)
          [token release];
#endif
        });
    if (!class_addMethod(cls, selector, replacement, method_getTypeEncoding(method)))
      class_replaceMethod(cls, selector, replacement, method_getTypeEncoding(method));
    objc_setAssociatedObject(cls, installed, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  auto* box = [[NativeApiFlutterQuitBridge alloc] init];
  box->bridge = bridge;
  objc_setAssociatedObject(handler, kBridge, box, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
#if !__has_feature(objc_arc)
  [box release];
#endif
  // The runtime capability is checked before this replacement. The original
  // callback and its sender are preserved; only our tagged callback gains a
  // matching approval scope. Each Flutter reply captures its own token, so an
  // old engine response cannot borrow a newer attempt's approval.
  auto weak_handler = std::make_shared<WeakObject>(handler);
  Terminator wrapped = ^(id sender) {
    id handler = weak_handler->Get();
    auto* box = (NativeApiFlutterQuitBridge*)objc_getAssociatedObject(handler, kBridge);
    auto live = box ? box->bridge : nullptr;
    if ([sender isKindOfClass:[NativeApiQuitTicket class]]) {
      auto ticket = ((NativeApiQuitTicket*)sender)->ticket;
      if (live)
        live->terminate(ticket, handler, live->original->value);
    } else if (live) {
      ((Terminator)live->original->value)(sender);
    }
  };
  // Flutter's callback ivar is strong under ARC, even when core is MRC.
  id block = [wrapped copy];
  object_setIvarWithStrongDefault(handler, class_getInstanceVariable(cls, "_terminator"), block);
#if !__has_feature(objc_arc)
  [block release];
#endif
}
}  // namespace

namespace nativeapi::detail {
MacApplicationQuitPolicy::MacApplicationQuitPolicy(Application* app)
    : impl_(std::make_unique<Impl>(app, this)) {}
std::shared_ptr<MacApplicationQuitPolicy> MacApplicationQuitPolicy::Create(Application* app) {
  return std::shared_ptr<MacApplicationQuitPolicy>(new MacApplicationQuitPolicy(app));
}
MacApplicationQuitPolicy::~MacApplicationQuitPolicy() = default;
void MacApplicationQuitPolicy::Shutdown() {
  impl_->app = nullptr;
  impl_->session.reset();
  impl_->authorized.reset();
}
void MacApplicationQuitPolicy::HostChanged() {
  if (impl_->session && NSApp.delegate != impl_->session->host.Get())
    impl_->End(impl_->session);
  ObserveHost(NSApp.delegate);
}
void MacApplicationQuitPolicy::Exiting() {
  if (!impl_->app)
    return;
  if (!impl_->announced_termination)
    impl_->Required();
  else if (impl_->session)
    impl_->End(impl_->session);
}
void MacApplicationQuitPolicy::ReplyFromHost(bool allowed) {
  if (!impl_->app)
    return;
  auto target = impl_->session;
  if (impl_->Current(target) && target->waiting_host) {
    if (allowed)
      impl_->announced_termination = true;
    impl_->End(target);
  } else if (allowed && !impl_->announced_termination) {
    impl_->Required();
  }
}
void MacApplicationQuitPolicy::ObserveHost(id host) {
  if (!impl_->app || !host)
    return;
  static const void* reply_installed = &reply_installed;
  Class application_class = object_getClass(NSApp);
  if (!objc_getAssociatedObject(application_class, reply_installed)) {
    SEL reply_selector = @selector(replyToApplicationShouldTerminate:);
    Method method = class_getInstanceMethod(application_class, reply_selector);
    auto original = reinterpret_cast<void (*)(id, SEL, BOOL)>(method_getImplementation(method));
    IMP replacement = imp_implementationWithBlock(^(NSApplication* instance, BOOL allowed) {
      auto* box = (NativeApiFlutterQuitBridge*)objc_getAssociatedObject(instance, kReplyPolicy);
      try {
        if (box)
          if (auto policy = box->bridge->policy.lock())
            policy->ReplyFromHost(allowed);
      } catch (...) { /* The original mandatory host reply must still run. */
      }
      original(instance, reply_selector, allowed);
    });
    if (!class_addMethod(application_class, reply_selector, replacement,
                         method_getTypeEncoding(method)))
      class_replaceMethod(application_class, reply_selector, replacement,
                          method_getTypeEncoding(method));
    objc_setAssociatedObject(application_class, reply_installed, @YES,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  auto* reply_box = [[NativeApiFlutterQuitBridge alloc] init];
  reply_box->bridge = std::make_shared<FlutterBridge>(host, nil);
  reply_box->bridge->policy = shared_from_this();
  objc_setAssociatedObject(NSApp, kReplyPolicy, reply_box, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
#if !__has_feature(objc_arc)
  [reply_box release];
#endif
  id handler = HandlerFor(host);
  if (!handler)
    return;
  if (auto* old = (NativeApiFlutterQuitBridge*)objc_getAssociatedObject(handler, kBridge)) {
    old->bridge->policy = shared_from_this();
    old->bridge->host.Reset(host);
    return;
  }
  Class cls = object_getClass(handler);
  SEL selector = NSSelectorFromString(@"requestApplicationTermination:exitType:result:");
  Method method = class_getInstanceMethod(cls, selector);
  Ivar callback = class_getInstanceVariable(cls, "_terminator");
  Ivar flag = class_getInstanceVariable(cls, "_shouldTerminate");
  auto matches = [](Method target, int argument, const char* expected) {
    if (!target)
      return false;
    char encoding[128] = {};
    if (argument < 0)
      method_getReturnType(target, encoding, sizeof(encoding));
    else
      method_getArgumentType(target, argument, encoding, sizeof(encoding));
    return std::strcmp(encoding, expected) == 0;
  };
  Method getter = class_getInstanceMethod(cls, @selector(shouldTerminate));
  if (!method || method_getNumberOfArguments(method) != 5 || !matches(method, -1, @encode(void)) ||
      !matches(method, 0, @encode(id)) || !matches(method, 1, @encode(SEL)) ||
      !matches(method, 2, @encode(id)) || !matches(method, 3, @encode(NSInteger)) ||
      !matches(method, 4, "@?") || !getter || method_getNumberOfArguments(getter) != 2 ||
      !matches(getter, -1, @encode(BOOL)) || !callback || !flag ||
      std::string(ivar_getTypeEncoding(callback)) != "@?" ||
      std::string(ivar_getTypeEncoding(flag)) != @encode(BOOL) ||
      ![handler respondsToSelector:@selector(shouldTerminate)])
    return;
  id original = object_getIvar(handler, callback);
  if (!original)
    return;
  auto bridge = std::make_shared<FlutterBridge>(host, handler);
  bridge->policy = shared_from_this();
  bridge->original = std::make_shared<ObjectRef>(original);
  std::weak_ptr<FlutterBridge> weak_bridge = bridge;
  bridge->make_ticket = [weak_bridge](id sender, NSInteger type) -> std::shared_ptr<Ticket> {
    auto live = weak_bridge.lock();
    auto policy = live ? live->policy.lock() : nullptr;
    if (!policy || !policy->impl_->app || (!policy->impl_->Interested() && !policy->impl_->session))
      return nullptr;
    auto ticket = std::make_shared<Ticket>();
    ticket->bridge = weak_bridge;
    ticket->sender = std::make_shared<ObjectRef>(sender);
    ticket->required = type != 0;  // Verified FlutterAppExitType runtime protocol.
    if (policy->impl_->session && policy->impl_->session->approved) {
      ticket->has_session = true;
      ticket->session = policy->impl_->session;
      policy->impl_->session->waiting_host = true;
    }
    return ticket;
  };
  bridge->result = [weak_bridge](const auto& ticket, id response) {
    if (auto live = weak_bridge.lock())
      if (auto policy = live->policy.lock())
        policy->impl_->FlutterResult(ticket, response);
  };
  bridge->terminate = [weak_bridge](const auto& ticket, id coordinator, id original) {
    auto live = weak_bridge.lock();
    auto policy = live ? live->policy.lock() : nullptr;
    if (policy)
      policy->impl_->FlutterTerminate(ticket, coordinator, original);
    else if (!ticket->has_session)
      ((Terminator)original)(ticket->sender->value);
  };
  InstallFlutterBridge(bridge);
}
NSApplicationTerminateReply MacApplicationQuitPolicy::Ask(
    id host,
    NSApplication*,
    std::function<NSApplicationTerminateReply()> original,
    bool approved) try {
  if (!impl_->app || impl_->forced)
    return original();
  if (impl_->entering)
    return NSTerminateCancel;
  if (impl_->authorized && impl_->Current(impl_->authorized))
    return impl_->Continue(impl_->authorized, original);
  if (approved) {
    auto request = Dispatch::CurrentRequest();
    if (!request)
      return original();
    auto target = std::make_shared<Session>(host);
    target->request = request;
    target->asking = false;
    target->approved = true;
    impl_->session = target;
    impl_->announced_termination = false;
    Dispatch::HoldForHost(request);
    return impl_->Continue(target, original);
  }
  if (!impl_->Interested() && !impl_->session && !Dispatch::CurrentRequest())
    return original();
  HostChanged();
  if (Dispatch::CurrentRequest())
    return NSTerminateCancel;
  auto target = std::make_shared<Session>(host);
  impl_->session = target;
  impl_->announced_termination = false;
  std::weak_ptr<Policy> weak = shared_from_this();
  std::weak_ptr<Session> weak_target = target;
  Dispatch::RequestFromNative([weak, weak_target](bool accepted) {
    auto policy = weak.lock();
    auto live = weak_target.lock();
    if (!policy || !live || policy->impl_->session != live)
      return;
    live->decided = true;
    live->approved = accepted;
    if (!accepted) {
      policy->impl_->End(live);
      return;
    }
    if (live->asking)
      return;
    try {
      if (!RunOnMainThread([weak, weak_target] {
            if (auto policy = weak.lock())
              if (auto live = weak_target.lock())
                policy->impl_->Resume(live);
          }))
        policy->impl_->End(live);
    } catch (...) {
      policy->impl_->End(live);
    }
  });
  target->asking = false;
  target->request = Dispatch::CurrentRequest();
  if (target->decided && target->approved && impl_->Current(target))
    return impl_->Continue(target, original);
  if (!target->request)
    impl_->End(target);
  return NSTerminateCancel;
} catch (...) {
  try {
    impl_->End(impl_->session);
  } catch (...) {
  }
  return NSTerminateCancel;
}
void MacApplicationQuitPolicy::Impl::FlutterResult(const std::shared_ptr<Ticket>& ticket,
                                                   id response) {
  if (ticket->completed)
    return;
  ticket->completed = true;
  if ([response isKindOfClass:[NSDictionary class]] && [response[@"response"] isEqual:@"cancel"])
    if (auto target = ticket->session.lock())
      End(target);
}
void MacApplicationQuitPolicy::Impl::FlutterTerminate(const std::shared_ptr<Ticket>& ticket,
                                                      id handler,
                                                      id original) {
  if (ticket->completed)
    return;
  ticket->completed = true;
  if (!app) {
    if (!ticket->has_session)
      ((Terminator)original)(ticket->sender->value);
    return;
  }
  auto target = ticket->session.lock();
  if (!ticket->required && ticket->has_session && !Current(target)) {
    if (!session || !session->waiting_host)
      SetShouldTerminate(handler, NO);
    return;
  }
  auto previous = authorized;
  const BOOL old_flag = ShouldTerminate(handler);
  SetShouldTerminate(handler, YES);
  if (ticket->required) {
    Required();
    ++forced;
  } else if (target)
    authorized = target;
  @try {
    ((Terminator)original)(ticket->sender->value);
  } @finally {
    authorized = previous;
    if (ticket->required)
      --forced;
    SetShouldTerminate(handler, old_flag);
  }
}
}  // namespace nativeapi::detail
