#include "../../foundation/dispatcher_common.h"
#include "../../foundation/dispatcher_platform.h"
namespace nativeapi::dispatcher_platform {
bool PlatformIsMainThread() {
  return dispatcher_internal::IsMainThreadByCapturedId();
}
void PlatformSetMainThread() {
  dispatcher_internal::CaptureCallerAsMainThread();
}
// No implicit browser scheduler. Embedders can use SetMainThreadDispatcher().
bool PlatformIsMainThreadDispatchSupported() {
  return false;
}
bool PlatformRunOnMainThread(std::function<void()>) {
  return false;
}
bool PlatformRunMainThreadLoopFor(int) {
  return false;
}
}  // namespace nativeapi::dispatcher_platform
