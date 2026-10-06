#include "../../window_close_dispatch.h"
namespace nativeapi::detail {
std::shared_ptr<WindowCloseState> GetNativeWindowCloseState(void*, WindowId) {
  return nullptr;
}
bool RequestNativeWindowClose(void*, WindowId) {
  return false;
}
bool IsNativeWindowCloseSupported() {
  return false;
}
}  // namespace nativeapi::detail

namespace nativeapi {
bool Window::DispatchWindowTask(std::function<void()>) {
  return false;
}
}  // namespace nativeapi
