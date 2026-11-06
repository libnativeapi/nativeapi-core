// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../keyboard_monitor.h"

namespace nativeapi {

class KeyboardMonitor::Impl {
 public:
  Impl(KeyboardMonitor* monitor) : monitor_(monitor) {}
  KeyboardMonitor* monitor_;
};

KeyboardMonitor::KeyboardMonitor() : impl_(std::make_unique<Impl>(this)) {}
KeyboardMonitor::~KeyboardMonitor() {
  ShutdownEmitter();
}

void KeyboardMonitor::Start() {}

void KeyboardMonitor::Stop() {}

bool KeyboardMonitor::IsMonitoring() const {
  return false;
}

EventEmitter<KeyboardEvent>& KeyboardMonitor::GetInternalEventEmitter() {
  return *this;
}

}  // namespace nativeapi
