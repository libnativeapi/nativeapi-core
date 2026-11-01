#pragma once

// Private producer for desktop toolbar events, excluded from the public ABI.
#include "window_manager.h"

namespace nativeapi::detail {
struct WindowDockDispatch {
  static void Emit(const WindowEvent& event) {
    try {
      WindowManager::GetInstance().DispatchWindowEvent(event);
    } catch (...) {
    }
  }
};
}  // namespace nativeapi::detail
