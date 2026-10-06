#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#include <fcntl.h>
#import <objc/runtime.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

#include "../../application.h"
#include "../../application_quit_dispatch.h"
#include "../../foundation/dispatcher.h"
#include "../../menu.h"
#include "../../window_manager.h"
#include "application_quit_macos.h"

@interface NativeApplicationDelegate : NSObject <NSApplicationDelegate> {
 @public
  std::shared_ptr<nativeapi::detail::MacApplicationQuitPolicy> quitPolicy;
}
@property(nonatomic, assign) nativeapi::Application* app;
// Set by Application::Quit() before it hands termination to AppKit, which
// already emitted ApplicationQuitRequestedEvent and knows the exit code.
@property(nonatomic, assign) BOOL quitRequested;
@property(nonatomic, assign) int exitCode;
@property(nonatomic, assign) BOOL usesNotifications;
@property(nonatomic, copy) void (^delegateChanged)(void);
@end

@implementation NativeApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  // Emit application started event
  nativeapi::ApplicationStartedEvent event;
  if (!self.usesNotifications)
    self.app->Emit(event);
}

- (void)applicationWillTerminate:(NSNotification*)notification {
  // AppKit is about to exit() the process: Run() will not return to emit this.
  try {
    if (quitPolicy)
      quitPolicy->Exiting();
    nativeapi::ApplicationExitingEvent event(
        nativeapi::detail::ApplicationQuitDispatch::ExitCode());
    if (!self.usesNotifications)
      self.app->Emit(event);
  } catch (...) {
    // A terminal observer cannot stop an already required native exit.
  }
}

- (void)applicationDidBecomeActive:(NSNotification*)notification {
  // Emit application activated event
  nativeapi::ApplicationActivatedEvent event;
  if (!self.usesNotifications)
    self.app->Emit(event);
}

- (void)applicationDidResignActive:(NSNotification*)notification {
  // Emit application deactivated event
  nativeapi::ApplicationDeactivatedEvent event;
  if (!self.usesNotifications)
    self.app->Emit(event);
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  BOOL approved = self.quitRequested;
  self.quitRequested = NO;
  return quitPolicy ? quitPolicy->Ask(
                          self, sender, [] { return NSTerminateNow; }, approved)
                    : NSTerminateNow;
}

@end

// Observe the host's quit decision without replacing its delegate object or
// runtime class. Flutter uses both its class identity and its original reply to
// deliver AppLifecycleListener.onExitRequested asynchronously.
static const void* kApplicationQuitObserverKey = &kApplicationQuitObserverKey;
static thread_local Class quit_continuation_class = Nil;
static bool NativeApiInheritedQuitHook(Class candidate) {
  for (Class ancestor = class_getSuperclass(quit_continuation_class); ancestor;
       ancestor = class_getSuperclass(ancestor))
    if (ancestor == candidate)
      return true;
  return false;
}
static void NativeApiObserveHostQuit(id<NSApplicationDelegate> host,
                                     NativeApplicationDelegate* observer) {
  static const void* installed_key = &installed_key;
  Class cls = object_getClass(host);
  if (!objc_getAssociatedObject(cls, installed_key)) {
    SEL selector = @selector(applicationShouldTerminate:);
    Method method = class_getInstanceMethod(cls, selector);
    auto original =
        method ? reinterpret_cast<NSApplicationTerminateReply (*)(id, SEL, NSApplication*)>(
                     method_getImplementation(method))
               : nullptr;
    IMP replacement =
        imp_implementationWithBlock(^NSApplicationTerminateReply(id instance, NSApplication* app) {
          NativeApplicationDelegate* active =
              objc_getAssociatedObject(instance, kApplicationQuitObserverKey);
          if (!active || !active.app || [NSApp delegate] != instance)
            return original ? original(instance, selector, app) : NSTerminateNow;
          // Forward a superclass hook in the same original host call, while
          // keeping genuine reentrant terminate: calls in the shared policy.
          if (NativeApiInheritedQuitHook(cls))
            return original ? original(instance, selector, app) : NSTerminateNow;
          BOOL approved = active.quitRequested;
          active.quitRequested = NO;
          auto continuation = [original, instance, selector, app, cls] {
            Class previous = quit_continuation_class;
            quit_continuation_class = cls;
            @try {
              return original ? original(instance, selector, app) : NSTerminateNow;
            } @finally {
              quit_continuation_class = previous;
            }
          };
          return active->quitPolicy ? active->quitPolicy->Ask(instance, app, continuation, approved)
                                    : continuation();
        });
    const std::string encoding = std::string(@encode(NSApplicationTerminateReply)) + "@:@";
    const char* types = method ? method_getTypeEncoding(method) : encoding.c_str();
    if (!class_addMethod(cls, selector, replacement, types))
      class_replaceMethod(cls, selector, replacement, types);
    objc_setAssociatedObject(cls, installed_key, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  objc_setAssociatedObject(host, kApplicationQuitObserverKey, observer,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static const void* kApplicationDelegateObserverKey = &kApplicationDelegateObserverKey;
static void NativeApiObserveDelegateChanges(NSApplication* app,
                                            NativeApplicationDelegate* observer) {
  static const void* installed_key = &installed_key;
  Class cls = object_getClass(app);
  if (!objc_getAssociatedObject(cls, installed_key)) {
    SEL selector = @selector(setDelegate:);
    Method method = class_getInstanceMethod(cls, selector);
    auto original = reinterpret_cast<void (*)(id, SEL, id)>(method_getImplementation(method));
    IMP replacement = imp_implementationWithBlock(^(NSApplication* instance, id delegate) {
      original(instance, selector, delegate);
      NativeApplicationDelegate* active =
          objc_getAssociatedObject(instance, kApplicationDelegateObserverKey);
      if (active.delegateChanged)
        active.delegateChanged();
    });
    if (!class_addMethod(cls, selector, replacement, method_getTypeEncoding(method)))
      class_replaceMethod(cls, selector, replacement, method_getTypeEncoding(method));
    objc_setAssociatedObject(cls, installed_key, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  objc_setAssociatedObject(app, kApplicationDelegateObserverKey, observer,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

namespace nativeapi {

class Application::Impl {
 public:
  Impl(Application* app) : app_(app), delegate_(nullptr) {}
  ~Impl() {
#if !__has_feature(objc_arc)
    [notification_observers_ release];
    [delegate_hosts_ release];
#endif
  }

  bool Initialize() {
    if (delegate_)
      return true;
    // Ensure we're on the main thread
    if (![NSThread isMainThread]) {
      return false;
    }

    // Get or create NSApplication instance
    NSApplication* ns_app = [NSApplication sharedApplication];
    if (!ns_app) {
      return false;
    }

    // A host (notably Flutter) owns its activation policy and delegate. Only a
    // standalone application needs nativeapi's defaults.
    if (!ns_app.delegate)
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // Create and set delegate
    delegate_ = [[NativeApplicationDelegate alloc] init];
    delegate_.app = app_;
    delegate_->quitPolicy = detail::MacApplicationQuitPolicy::Create(app_);
    if (!ns_app.delegate)
      [ns_app setDelegate:delegate_];

    return true;
  }

  int Run() {
    if (!Initialize())
      return -1;
    // Start the main event loop; Quit() stops it.
    [NSApp run];

    return app_->exit_code_;
  }

  int Run(std::shared_ptr<Window> window) {
    if (!Initialize())
      return -1;
    if (!window) {
      return -1;
    }

    // Set the window as primary window
    app_->SetPrimaryWindow(window);

    // Show the window
    window->Show();
    window->Focus();

    // Start the main event loop; Quit() stops it.
    [NSApp run];

    return app_->exit_code_;
  }

  void Quit(int exit_code) {
    if (!delegate_ && !Initialize())
      return;
    if (app_->running_) {
      // Our Run() owns the loop: stop it so Run() returns the exit code.
      // -stop: only takes effect after the loop handles one more event, so
      // post one in case the queue is empty.
      [NSApp stop:nil];
      NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                         location:NSZeroPoint
                                    modifierFlags:0
                                        timestamp:0
                                     windowNumber:0
                                          context:nil
                                          subtype:0
                                            data1:0
                                            data2:0];
      [NSApp postEvent:wake atStart:YES];
      return;
    }

    // Someone else runs the loop (a Flutter runner, a host pumping it by
    // hand): end the process the way AppKit does. -terminate: exits with
    // status 0; the exit code only reaches ApplicationExitingEvent.
    delegate_.quitRequested = YES;
    delegate_.exitCode = exit_code;
    [NSApp terminate:nil];
    // A host may cancel or defer. A later user request is a fresh request.
    delegate_.quitRequested = NO;
  }

  bool SetIcon(const std::string& icon_path) {
    if (icon_path.empty()) {
      return false;
    }

    NSString* path = [NSString stringWithUTF8String:icon_path.c_str()];
    NSImage* icon = [[NSImage alloc] initWithContentsOfFile:path];

    if (!icon) {
      return false;
    }

    [NSApp setApplicationIconImage:icon];
    return true;
  }

  bool SetDockIconVisible(bool visible) {
    if (visible) {
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    } else {
      [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    }
    return true;
  }

  bool SetProgressBar(double progress) {
    NSDockTile* dock_tile = [NSApp dockTile];

    if (progress < 0) {
      // Hand the tile back to AppKit so it shows the plain application icon.
      dock_tile.contentView = nil;
      dock_icon_view_ = nil;
      dock_progress_ = nil;
      [dock_tile display];
      return true;
    }

    if (!dock_progress_) {
      dock_icon_view_ = [[NSImageView alloc] init];
      dock_tile.contentView = dock_icon_view_;

      NSRect frame = NSMakeRect(0, 0, dock_tile.size.width, 15.0);
      dock_progress_ = [[NSProgressIndicator alloc] initWithFrame:frame];
      dock_progress_.style = NSProgressIndicatorStyleBar;
      dock_progress_.minValue = 0;
      dock_progress_.maxValue = 1;
      [dock_icon_view_ addSubview:dock_progress_];
    }
    // Re-read the icon every time so a later SetIcon() shows through.
    dock_icon_view_.image = [NSApp applicationIconImage];

    if (progress > 1) {
      dock_progress_.indeterminate = YES;
      dock_progress_.doubleValue = 1;
      [dock_progress_ startAnimation:nil];
    } else {
      [dock_progress_ stopAnimation:nil];
      dock_progress_.indeterminate = NO;
      dock_progress_.doubleValue = progress;
    }
    [dock_tile display];
    return true;
  }

  bool SetBadgeLabel(const std::string& label) {
    NSDockTile* dock_tile = [NSApp dockTile];
    dock_tile.badgeLabel = label.empty() ? nil : [NSString stringWithUTF8String:label.c_str()];
    return true;
  }

  bool SetBrightness(Brightness brightness) {
    if (@available(macOS 10.14, *)) {
      NSAppearance* appearance = nil;
      switch (brightness) {
        case Brightness::Light:
          appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
          break;
        case Brightness::Dark:
          appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
          break;
        case Brightness::System:
        default:
          break;  // nil = inherit from the system
      }
      NSApp.appearance = appearance;
      return true;
    }
    // Dark mode does not exist before 10.14, so only "light" and "system" are
    // satisfiable and they are already in effect.
    return brightness != Brightness::Dark;
  }

  bool SetMenuBar(std::shared_ptr<Menu> menu) {
    if (!menu) {
      return false;
    }

    // Get the native menu handle
    NSMenu* ns_menu = (__bridge NSMenu*)(menu->GetNativeObject());
    if (!ns_menu) {
      return false;
    }

    // Set the application menu
    [NSApp setMainMenu:ns_menu];

    return true;
  }

  void StartEventMonitoring() {
    if (event_monitoring_ || (!delegate_ && !Initialize()))
      return;
    event_monitoring_ = true;
    if (!delegate_hosts_)
      delegate_hosts_ = [[NSHashTable alloc] initWithOptions:NSPointerFunctionsWeakMemory
                                                    capacity:1];
    delegate_.delegateChanged = ^{
      delegate_->quitPolicy->HostChanged();
      MonitorCurrentHost();
    };
    delegate_->quitPolicy->ObserveHost(NSApp.delegate);
    NativeApiObserveDelegateChanges(NSApp, delegate_);
    MonitorCurrentHost();
  }

  void MonitorCurrentHost() {
    id<NSApplicationDelegate> host = [NSApp delegate];
    if (!host || host == delegate_)
      return;
    if (![delegate_hosts_ containsObject:host]) {
      [delegate_hosts_ addObject:host];
      NativeApiObserveHostQuit(host, delegate_);
      delegate_->quitPolicy->ObserveHost(host);
    }
    if (!event_monitoring_ || notification_observers_)
      return;
    notification_observers_ = [[NSMutableArray alloc] init];
    delegate_.usesNotifications = YES;
    auto* app = app_;
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    for (NSNotificationName name in @[
           NSApplicationDidFinishLaunchingNotification, NSApplicationDidBecomeActiveNotification,
           NSApplicationDidResignActiveNotification, NSApplicationWillTerminateNotification
         ]) {
      id token = [center
          addObserverForName:name
                      object:NSApp
                       queue:nil
                  usingBlock:^(NSNotification* note) {
                    if ([note.name isEqualToString:NSApplicationDidFinishLaunchingNotification])
                      app->Emit<ApplicationStartedEvent>();
                    else if ([note.name isEqualToString:NSApplicationDidBecomeActiveNotification])
                      app->Emit<ApplicationActivatedEvent>();
                    else if ([note.name isEqualToString:NSApplicationDidResignActiveNotification])
                      app->Emit<ApplicationDeactivatedEvent>();
                    else {
                      try {
                        delegate_->quitPolicy->Exiting();
                        app->Emit<ApplicationExitingEvent>(app->exit_code_);
                      } catch (...) {
                        // Preserve the host's mandatory termination notification.
                      }
                    }
                  }];
      [notification_observers_ addObject:token];
    }
  }

  void StopEventMonitoring() {
    event_monitoring_ = false;
    for (id token in notification_observers_)
      [NSNotificationCenter.defaultCenter removeObserver:token];
#if !__has_feature(objc_arc)
    [notification_observers_ release];
#endif
    notification_observers_ = nil;
    delegate_.usesNotifications = NO;
  }

  void CleanupEventMonitoring() {
    StopEventMonitoring();
    delegate_.delegateChanged = nil;
    if (objc_getAssociatedObject(NSApp, kApplicationDelegateObserverKey) == delegate_)
      objc_setAssociatedObject(NSApp, kApplicationDelegateObserverKey, nil,
                               OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    for (id host in delegate_hosts_)
      if (objc_getAssociatedObject(host, kApplicationQuitObserverKey) == delegate_)
        objc_setAssociatedObject(host, kApplicationQuitObserverKey, nil,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    // Clean up macOS-specific event monitoring
    if (lock_file_handle_ != -1) {
      close(lock_file_handle_);
      lock_file_handle_ = -1;
    }

    if (delegate_) {
      // Do not erase a delegate a host installed after nativeapi initialized.
      if ([NSApp delegate] == delegate_)
        [NSApp setDelegate:nil];
      delegate_->quitPolicy->Shutdown();
      delegate_.app = nullptr;
#if !__has_feature(objc_arc)
      [delegate_ release];
#endif
      delegate_ = nil;
    }
  }

 private:
  Application* app_;
  NativeApplicationDelegate* delegate_;
  bool event_monitoring_ = false;
  NSMutableArray* notification_observers_ = nil;
  NSHashTable* delegate_hosts_ = nil;
  int lock_file_handle_ = -1;
  NSImageView* dock_icon_view_ = nil;
  NSProgressIndicator* dock_progress_ = nil;
};

Application::Application()
    : initialized_(true), running_(false), exit_code_(0), pimpl_(std::make_unique<Impl>(this)) {
  // A binding may first create this singleton from a Flutter/Dart worker.
  // AppKit initialization belongs to UI and must not be silently lost.
  auto initialize =
      CreateGuardedCallback<>(std::function<void()>([this] { pimpl_->Initialize(); }));
  if ([NSThread isMainThread])
    initialize();
  else
    (void)RunOnMainThread(std::move(initialize));

  // Emit application started event
  Emit<ApplicationStartedEvent>();
}

Application::~Application() {
  ShutdownEmitter();
  InvalidateQuitRequest();
  // Clean up platform-specific event monitoring
  pimpl_->CleanupEventMonitoring();
}

int Application::Run() {
  running_ = true;
  exit_code_ = 0;

  // Start the platform-specific main event loop
  int result = pimpl_->Run();

  running_ = false;

  // Emit exit event
  Emit<ApplicationExitingEvent>(result);

  return result;
}

int Application::Run(std::shared_ptr<Window> window) {
  if (!window) {
    return -1;  // Invalid window
  }

  running_ = true;
  exit_code_ = 0;

  // Start the platform-specific main event loop with window
  int result = pimpl_->Run(window);

  running_ = false;

  // Emit exit event
  Emit<ApplicationExitingEvent>(result);

  return result;
}

void Application::Quit(int exit_code) {
  RequestQuit(exit_code);
}

void Application::PerformQuit(int exit_code) {
  pimpl_->Quit(exit_code);
}

void Application::StartEventListening() {
  auto update = CreateGuardedCallback<>(std::function<void()>([this] {
    if (pimpl_ && GetTotalListenerCount() > 0)
      pimpl_->StartEventMonitoring();
  }));
  if (IsMainThread())
    update();
  else
    (void)RunOnMainThread(std::move(update));
}

void Application::StopEventListening() {
  auto update = CreateGuardedCallback<>(std::function<void()>([this] {
    if (pimpl_ && GetTotalListenerCount() == 0)
      pimpl_->StopEventMonitoring();
  }));
  if (IsMainThread())
    update();
  else
    (void)RunOnMainThread(std::move(update));
}

bool Application::IsRunning() const {
  // The plain Dart UI handoff queries this before it starts pumping AppKit.
  // Complete a worker-created singleton now, without waiting for that pump.
  if ([NSThread isMainThread])
    pimpl_->Initialize();
  return running_;
}

bool Application::IsSingleInstance() const {
  return false;
}

bool Application::Show() {
  // unhide: brings the hidden windows back and activates the application; an
  // application that is not hidden is only brought to the front.
  [NSApp unhide:nil];
  [NSApp activateIgnoringOtherApps:YES];
  return true;
}

bool Application::Hide() {
  [NSApp hide:nil];
  return true;
}

bool Application::IsVisible() const {
  return ![NSApp isHidden];
}

bool Application::SetIcon(const std::string& icon_path) {
  return pimpl_->SetIcon(icon_path);
}

bool Application::SetDockIconVisible(bool visible) {
  return pimpl_->SetDockIconVisible(visible);
}

bool Application::SetProgressBar(double progress) {
  return pimpl_->SetProgressBar(progress);
}

bool Application::SetBadgeLabel(const std::string& label) {
  return pimpl_->SetBadgeLabel(label);
}

bool Application::SetBrightness(Brightness brightness) {
  return pimpl_->SetBrightness(brightness);
}

bool Application::SetMenuBar(std::shared_ptr<Menu> menu) {
  return pimpl_->SetMenuBar(menu);
}

std::shared_ptr<Window> Application::GetPrimaryWindow() const {
  return primary_window_;
}

void Application::SetPrimaryWindow(std::shared_ptr<Window> window) {
  primary_window_ = window;
}

std::vector<std::shared_ptr<Window>> Application::GetAllWindows() const {
  auto& window_manager = WindowManager::GetInstance();
  return window_manager.GetAll();
}

}  // namespace nativeapi
