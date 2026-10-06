// Desktop-platform fallback contract; no window is shown and no input is sent.
#include "nativeapi.h"
#include <iostream>
#ifdef __linux__
#include <gtk/gtk.h>
#endif

int main(int argc, char** argv) {
#ifdef __linux__
  if (!gtk_init_check(&argc, &argv)) return 77;
#endif
  using namespace nativeapi;
  Application::GetInstance();
  Window window;
  if (Window::IsCornerPreferenceSupported() || native_window_is_corner_preference_supported()) return 1;
  for (auto preference : {WindowCornerPreference::Default, WindowCornerPreference::DoNotRound,
                          WindowCornerPreference::Round, WindowCornerPreference::RoundSmall}) {
    if (window.SetCornerPreference(preference) || window.GetCornerPreference() != WindowCornerPreference::Default)
      return 1;
  }
  const auto handle = native_window_create();
  if (!handle || native_window_set_corner_preference(handle, NATIVE_WINDOW_CORNER_PREFERENCE_ROUND) ||
      native_window_get_corner_preference(handle) != NATIVE_WINDOW_CORNER_PREFERENCE_DEFAULT) return 1;
  native_window_free(handle);
  std::cout << "PASS unsupported C++ and C ABI leave system corner policy unchanged\n";
}
