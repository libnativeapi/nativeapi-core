#pragma once
// Private AppKit host continuation; never part of API_HEADERS.
#import <Cocoa/Cocoa.h>
#include <functional>
#include <memory>
namespace nativeapi {
class Application;
namespace detail {
class MacApplicationQuitPolicy : public std::enable_shared_from_this<MacApplicationQuitPolicy> {
 public:
  static std::shared_ptr<MacApplicationQuitPolicy> Create(Application* application);
  ~MacApplicationQuitPolicy();
  NSApplicationTerminateReply Ask(id host,
                                  NSApplication* application,
                                  std::function<NSApplicationTerminateReply()> original,
                                  bool approved = false);
  void ObserveHost(id host);
  void HostChanged();
  void Exiting();
  void ReplyFromHost(bool allowed);
  void Shutdown();

 private:
  explicit MacApplicationQuitPolicy(Application* application);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace detail
}  // namespace nativeapi
