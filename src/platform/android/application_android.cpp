#include <android/log.h>
#include "../../application.h"
#include "../../window_manager.h"

#define LOG_TAG "NativeApi"
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace nativeapi {

class Application::Impl {
 public:
  Impl() {}
};

Application::Application() : pimpl_(std::make_unique<Impl>()) {}
Application::~Application() {}

int Application::Run() {
  ALOGW("Application::Run not applicable on Android (handled by Activity lifecycle)");
  return 0;
}

int Application::Run(std::shared_ptr<Window> window) {
  ALOGW("Application::Run with window not applicable on Android");
  return 0;
}

void Application::Quit(int exit_code) {
  ALOGW("Application::Quit requests Activity finish");
}

bool Application::IsRunning() const {
  return true;
}

bool Application::IsSingleInstance() const {
  return false;
}

// No application-level hiding here; see Application::Hide().
bool Application::Show() {
  return false;
}

bool Application::Hide() {
  return false;
}

bool Application::IsVisible() const {
  return true;
}

bool Application::SetIcon(const std::string& icon_path) {
  ALOGW("Application::SetIcon not implemented on Android");
  return false;
}

bool Application::SetDockIconVisible(bool visible) {
  ALOGW("Application::SetDockIconVisible not applicable on Android");
  return false;
}

bool Application::SetProgressBar(double progress) {
  ALOGW("Application::SetProgressBar not applicable on Android");
  return false;
}

bool Application::SetBadgeLabel(const std::string& label) {
  ALOGW("Application::SetBadgeLabel not applicable on Android");
  return false;
}

bool Application::SetBrightness(Brightness brightness) {
  ALOGW("Application::SetBrightness not applicable on Android");
  return false;
}

bool Application::SetMenuBar(std::shared_ptr<Menu> menu) {
  ALOGW("Application::SetMenuBar not implemented on Android");
  return false;
}

std::shared_ptr<Window> Application::GetPrimaryWindow() const {
  return nullptr;
}

void Application::SetPrimaryWindow(std::shared_ptr<Window> window) {
  ALOGW("Application::SetPrimaryWindow not implemented on Android");
}

std::vector<std::shared_ptr<Window>> Application::GetAllWindows() const {
  auto& window_manager = WindowManager::GetInstance();
  return window_manager.GetAll();
}

}  // namespace nativeapi
