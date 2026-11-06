#include "../../application.h"
namespace nativeapi {
class Application::Impl {};
Application::Application() : pimpl_(std::make_unique<Impl>()) {}
Application::~Application() {
  ShutdownEmitter();
  InvalidateQuitRequest();
}
// The JavaScript host owns the event loop; never block it or terminate the host.
int Application::Run() {
  return 0;
}
int Application::Run(std::shared_ptr<Window>) {
  return 0;
}
void Application::Quit(int) {}
void Application::PerformQuit(int) {}
bool Application::IsRunning() const {
  return false;
}
bool Application::IsSingleInstance() const {
  return false;
}
bool Application::Show() {
  return false;
}
bool Application::Hide() {
  return false;
}
bool Application::IsVisible() const {
  return false;
}
bool Application::SetIcon(const std::string&) {
  return false;
}
bool Application::SetDockIconVisible(bool) {
  return false;
}
bool Application::SetProgressBar(double) {
  return false;
}
bool Application::SetBadgeLabel(const std::string&) {
  return false;
}
bool Application::SetBrightness(Brightness) {
  return false;
}
bool Application::SetMenuBar(std::shared_ptr<Menu>) {
  return false;
}
std::shared_ptr<Window> Application::GetPrimaryWindow() const {
  return nullptr;
}
void Application::SetPrimaryWindow(std::shared_ptr<Window>) {}
std::vector<std::shared_ptr<Window>> Application::GetAllWindows() const {
  return {};
}
void Application::StartEventListening() {}
void Application::StopEventListening() {}
}  // namespace nativeapi
