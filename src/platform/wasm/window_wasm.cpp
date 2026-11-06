#include "../../window.h"

namespace nativeapi {
class Window::Impl {};
Window::Window() : Window(nullptr) {}
Window::Window(void*) : pimpl_(std::make_unique<Impl>()) {}
Window::~Window() {
  ShutdownEmitter();
}

WindowNumber Window::GetNativeNumber() const {
  return 0;
}
WindowId Window::GetId() const {
  return IdAllocator::kInvalidId;
}
void Window::Focus() {}
void Window::Blur() {}
bool Window::IsFocused() const {
  return false;
}
void Window::Show() {}
void Window::ShowInactive() {}
void Window::Hide() {}
bool Window::IsVisible() const {
  return false;
}
WindowOcclusionState Window::GetOcclusionState() const {
  return WindowOcclusionState::Unknown;
}
bool Window::IsOcclusionStateSupported() {
  return false;
}
void Window::Maximize() {}
void Window::Unmaximize() {}
bool Window::IsMaximized() const {
  return false;
}
void Window::Minimize() {}
void Window::Restore() {}
bool Window::IsMinimized() const {
  return false;
}
void Window::SetFullScreen(bool is_full_screen) {}
bool Window::IsFullScreen() const {
  return false;
}
void Window::SetBounds(Rectangle bounds) {}
Rectangle Window::GetBounds() const {
  return {};
}
void Window::SetSize(Size size, bool animate) {}
Size Window::GetSize() const {
  return {};
}
void Window::SetContentSize(Size size) {}
Size Window::GetContentSize() const {
  return {};
}
void Window::SetContentBounds(Rectangle bounds) {}
Rectangle Window::GetContentBounds() const {
  return {};
}
void Window::SetMinimumSize(Size size) {}
Size Window::GetMinimumSize() const {
  return {};
}
void Window::SetMaximumSize(Size size) {}
Size Window::GetMaximumSize() const {
  return {};
}
void Window::SetResizable(bool is_resizable) {}
bool Window::IsResizable() const {
  return false;
}
void Window::SetMovable(bool is_movable) {}
bool Window::IsMovable() const {
  return false;
}
void Window::SetMinimizable(bool is_minimizable) {}
bool Window::IsMinimizable() const {
  return false;
}
void Window::SetMaximizable(bool is_maximizable) {}
bool Window::IsMaximizable() const {
  return false;
}
void Window::SetFullScreenable(bool is_full_screenable) {}
bool Window::IsFullScreenable() const {
  return false;
}
void Window::SetClosable(bool is_closable) {}
bool Window::IsClosable() const {
  return false;
}
void Window::SetWindowControlButtonsVisible(bool is_visible) {}
bool Window::IsWindowControlButtonsVisible() const {
  return false;
}
void Window::SetAlwaysOnTop(bool is_always_on_top) {}
bool Window::IsAlwaysOnTop() const {
  return false;
}
void Window::SetAlwaysOnBottom(bool is_always_on_bottom) {}
bool Window::IsAlwaysOnBottom() const {
  return false;
}
bool Window::SetParentWindow(std::shared_ptr<Window> parent) {
  return false;
}
std::shared_ptr<Window> Window::GetParentWindow() const {
  return nullptr;
}
void Window::SetAspectRatio(double aspect_ratio) {}
double Window::GetAspectRatio() const {
  return 0;
}
void Window::SetNonActivating(bool is_non_activating) {}
bool Window::IsNonActivating() const {
  return false;
}
void Window::SetPosition(Point point) {}
Point Window::GetPosition() const {
  return {};
}
void Window::Center() {}
void Window::SetTitle(std::string title) {}
std::string Window::GetTitle() const {
  return "";
}
bool Window::SetFrameless(bool /*is_frameless*/) {
  return false;
}
bool Window::IsFrameless() const {
  return false;
}
void Window::SetTitleBarStyle(TitleBarStyle style) {}
TitleBarStyle Window::GetTitleBarStyle() const {
  return TitleBarStyle::Normal;
}
bool Window::SetContentUnderTitleBar(bool is_content_under_title_bar) {
  return false;
}
bool Window::IsContentUnderTitleBar() const {
  return false;
}
bool Window::SetContentProtection(bool /*is_content_protected*/) {
  return false;
}
bool Window::IsContentProtected() const {
  return false;
}
bool Window::IsContentProtectionSupported() {
  return false;
}
bool Window::SetCornerPreference(WindowCornerPreference /*preference*/) {
  return false;
}
WindowCornerPreference Window::GetCornerPreference() const {
  return WindowCornerPreference::Default;
}
bool Window::IsCornerPreferenceSupported() {
  return false;
}
bool Window::IsContentUnderTitleBarSupported() {
  return false;
}
void Window::SetHasShadow(bool has_shadow) {}
bool Window::HasShadow() const {
  return false;
}
void Window::SetOpacity(float opacity) {}
float Window::GetOpacity() const {
  return 0;
}
bool Window::SetVisualEffect(VisualEffect effect) {
  return false;
}
VisualEffect Window::GetVisualEffect() const {
  return VisualEffect::None;
}
bool Window::IsVisualEffectSupported(VisualEffect effect) {
  return false;
}
void Window::SetBackgroundColor(const Color& color) {}
Color Window::GetBackgroundColor() const {
  return {};
}
void Window::SetVisibleOnAllWorkspaces(bool is_visible_on_all_workspaces) {}
bool Window::IsVisibleOnAllWorkspaces() const {
  return false;
}
void Window::SetVisibleInTaskbar(bool is_visible_in_taskbar) {}
bool Window::IsVisibleInTaskbar() const {
  return false;
}
bool Window::SetIgnoreMouseEvents(bool is_ignore_mouse_events, bool forward) {
  return false;
}
bool Window::IsIgnoreMouseEvents() const {
  return false;
}
bool Window::IsMouseMoveForwardingEnabled() const {
  return false;
}
bool Window::IsMouseMoveForwardingSupported() {
  return false;
}
void Window::SetFocusable(bool is_focusable) {}
bool Window::IsFocusable() const {
  return false;
}
bool Window::ShowSystemMenu(Point /*position*/) {
  return false;
}
bool Window::IsSystemMenuSupported() {
  return false;
}
bool Window::SetMaximizeButtonBounds(Rectangle) {
  return false;
}
Rectangle Window::GetMaximizeButtonBounds() const {
  return {};
}
bool Window::IsMaximizeButtonBoundsSupported() {
  return false;
}
bool Window::PerformTitleBarDoubleClick() {
  return false;
}
void Window::StartDragging() {}
void Window::StartResizing(ResizeEdge edge) {}
void* Window::GetNativeObjectInternal() const {
  return nullptr;
}
bool Window::SetShape(std::shared_ptr<WindowShape> shape) {
  return false;
}
bool Window::IsShaped() const {
  return false;
}
bool Window::IsShapeSupported() {
  return false;
}
bool Window::SetInputShape(std::shared_ptr<WindowShape> shape) {
  return false;
}
bool Window::IsInputShaped() const {
  return false;
}
bool Window::IsInputShapeSupported() {
  return false;
}
bool Window::SetTitleBarColors(const Color&, const Color&) {
  return false;
}
bool Window::ResetTitleBarColors() {
  return false;
}
bool Window::SetCustomShadow(std::shared_ptr<WindowShadow> shadow) {
  return false;
}
std::shared_ptr<WindowShadow> Window::GetCustomShadow() const {
  return nullptr;
}
bool Window::SetMaximizedVertically(bool) {
  return false;
}
bool Window::IsMaximizedVertically() const {
  return false;
}
bool Window::IsVerticalMaximizeSupported() {
  return false;
}
bool Window::Dock(WindowDockSide, double) {
  return false;
}
bool Window::Undock() {
  return false;
}
WindowDockSide Window::GetDockSide() const {
  return WindowDockSide::None;
}
bool Window::IsDockingSupported() {
  return false;
}
bool Window::AnimateBounds(Rectangle) {
  return false;
}
bool Window::IsBoundsAnimationSupported() {
  return false;
}
bool Window::SetVisibleOnFullScreen(bool) {
  return false;
}
bool Window::IsVisibleOnFullScreen() const {
  return false;
}
bool Window::IsVisibleOnFullScreenSupported() {
  return false;
}
bool Window::GrabKeyboard() {
  return false;
}
bool Window::UngrabKeyboard() {
  return false;
}
bool Window::IsKeyboardGrabbed() const {
  return false;
}
bool Window::IsKeyboardGrabSupported() {
  return false;
}
}  // namespace nativeapi
