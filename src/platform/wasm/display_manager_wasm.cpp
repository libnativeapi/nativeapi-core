#include "../../display_manager.h"
namespace nativeapi {
DisplayManager::DisplayManager() = default;
DisplayManager::~DisplayManager() {
  ShutdownEmitter();
}
bool DisplayManager::EnumerateNativeDisplays(std::vector<NativeDisplayInfo>&) {
  return false;
}
Point DisplayManager::GetCursorPosition() {
  return {0, 0};
}
void DisplayManager::StartEventListening() {}
void DisplayManager::StopEventListening() {}
}  // namespace nativeapi
