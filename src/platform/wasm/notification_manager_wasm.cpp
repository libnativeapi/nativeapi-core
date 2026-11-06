// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../notification_manager.h"
namespace nativeapi {
class NotificationManager::Impl {};
NotificationManager::NotificationManager() : pimpl_(std::make_unique<Impl>()) {}
NotificationManager::~NotificationManager() {
  ShutdownEmitter();
}
NotificationManager& NotificationManager::GetInstance() {
  static NotificationManager instance;
  return instance;
}
bool NotificationManager::IsSupported() {
  return false;
}
bool NotificationManager::Initialize() {
  return false;
}
void NotificationManager::Shutdown() {}
bool NotificationManager::Show(const std::string&,
                               const std::string&,
                               const std::string&,
                               const std::string&) {
  return false;
}
bool NotificationManager::Remove(const std::string&) {
  return false;
}
std::string NotificationManager::GetLastError() const {
  return "System notifications are not implemented on this platform";
}
}  // namespace nativeapi
