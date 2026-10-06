// macOS has no equivalent system menu. No window is shown or input sent.
#include "nativeapi.h"
#include <iostream>

int main() {
  using namespace nativeapi;
  Application::GetInstance();
  Window window;
  if (Window::IsSystemMenuSupported() || window.ShowSystemMenu({10, 20}) ||
      native_window_is_system_menu_supported()) return 1;
  const auto handle = native_window_create_with_native_window(window.GetNativeObject());
  if (!handle || native_window_show_system_menu(handle, {10, 20}) ||
      native_window_show_system_menu(0, {10, 20})) return 1;
  native_window_free(handle);
  std::cout << "PASS unsupported native system menu and C ABI\n";
}
