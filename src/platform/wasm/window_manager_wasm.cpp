#include "../../window_manager.h"
namespace nativeapi {
class WindowManager::Impl {};
WindowManager::WindowManager() : pimpl_(std::make_unique<Impl>()) {}
WindowManager::~WindowManager() {
  ShutdownEmitter();
}
std::shared_ptr<Window> WindowManager::Get(WindowId) {
  return nullptr;
}
std::shared_ptr<Window> WindowManager::GetCurrent() {
  return nullptr;
}
std::vector<std::shared_ptr<Window>> WindowManager::GetAll() {
  return {};
}
std::shared_ptr<Window> WindowManager::GetWindowAtPoint(Point, WindowId) {
  return nullptr;
}
void WindowManager::SetWillShowHook(std::optional<WindowWillShowHook>) {}
void WindowManager::SetWillHideHook(std::optional<WindowWillHideHook>) {}
bool WindowManager::HasWillShowHook() const {
  return false;
}
bool WindowManager::HasWillHideHook() const {
  return false;
}
void WindowManager::HandleWillShow(WindowId) {}
void WindowManager::HandleWillHide(WindowId) {}
bool WindowManager::CallOriginalShow(WindowId) {
  return false;
}
bool WindowManager::CallOriginalHide(WindowId) {
  return false;
}
void WindowManager::StartEventListening() {}
void WindowManager::StopEventListening() {}
void WindowManager::DispatchWindowEvent(const WindowEvent&) {}
}  // namespace nativeapi
