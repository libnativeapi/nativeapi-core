add_executable(url_opener_test url_opener_test.cpp)
target_link_libraries(url_opener_test PRIVATE nativeapi)
add_test(NAME url_opener_test COMMAND url_opener_test)

add_executable(window_shape_test window_shape_test.cpp)
target_link_libraries(window_shape_test PRIVATE nativeapi)
add_test(NAME window_shape_test COMMAND window_shape_test)

add_executable(window_shadow_blur_test window_shadow_blur_test.cpp)
target_link_libraries(window_shadow_blur_test PRIVATE nativeapi)
add_test(NAME window_shadow_blur_test COMMAND window_shadow_blur_test)

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJCXX)
  add_executable(window_shape_macos_test window_shape_macos_test.mm)
  target_link_libraries(window_shape_macos_test PRIVATE nativeapi)
  add_test(NAME window_shape_macos_test COMMAND window_shape_macos_test)
endif()

# Links the full library because EventEmitter now routes async delivery through
# the platform dispatcher. The test installs its own dispatcher (see
# FakeMainThread) so no run loop is required at runtime.
find_package(Threads REQUIRED)
add_executable(event_emitter_test event_emitter_test.cpp)
target_link_libraries(event_emitter_test PRIVATE nativeapi Threads::Threads)
add_test(NAME event_emitter_test COMMAND event_emitter_test)
set_tests_properties(event_emitter_test PROPERTIES TIMEOUT 120)

add_executable(id_allocator_test id_allocator_test.cpp)
target_link_libraries(id_allocator_test PRIVATE nativeapi Threads::Threads)
add_test(NAME id_allocator_test COMMAND id_allocator_test)

add_executable(handle_table_test handle_table_test.cpp)
target_link_libraries(handle_table_test PRIVATE nativeapi Threads::Threads)
add_test(NAME handle_table_test COMMAND handle_table_test)
set_tests_properties(handle_table_test PROPERTIES TIMEOUT 120)

add_executable(shortcut_accelerator_test shortcut_accelerator_test.cpp)
target_link_libraries(shortcut_accelerator_test PRIVATE nativeapi)
add_test(NAME shortcut_accelerator_test COMMAND shortcut_accelerator_test)

add_executable(callback_lifetime_test callback_lifetime_test.cpp)
target_link_libraries(callback_lifetime_test PRIVATE nativeapi Threads::Threads)
add_test(NAME callback_lifetime_test COMMAND callback_lifetime_test)
set_tests_properties(callback_lifetime_test PROPERTIES TIMEOUT 20)

# The registry side of launch-at-login: the Run value, the StartupApproved flag that
# Task Manager toggles, and a non-ASCII identifier. Writes only under HKCU, under a
# test-only identifier, and removes what it writes.
if(WIN32)
  add_executable(launch_at_login_test launch_at_login_test.cpp)
  target_link_libraries(launch_at_login_test PRIVATE nativeapi)
  add_test(NAME launch_at_login_test COMMAND launch_at_login_test)
endif()

# Interactive: with any argument it opens real menus and closes them from a timer
# (no input needed, but it does need a desktop session). A no-op without arguments.
if(WIN32)
  add_executable(menu_lifetime_test menu_lifetime_test.cpp)
  target_link_libraries(menu_lifetime_test PRIVATE nativeapi)
  add_test(NAME menu_lifetime_test COMMAND menu_lifetime_test)
endif()

# Interactive: with a round name it opens a real menu and waits for
# tools/gui/core_menu_backend_test.ps1 to work it with the mouse. Without arguments
# it is a no-op, so CTest can run it.
if(WIN32)
  add_executable(menu_click_test menu_click_test.cpp)
  target_link_libraries(menu_click_test PRIVATE nativeapi)
  add_test(NAME menu_click_test COMMAND menu_click_test)
endif()

if(WIN32)
  add_executable(menu_backend_test menu_backend_test.cpp)
  target_link_libraries(menu_backend_test PRIVATE nativeapi)
  add_test(NAME menu_backend_test COMMAND menu_backend_test)
  if(NATIVEAPI_ENABLE_WINUI3)
    target_sources(menu_backend_test PRIVATE "../cmake/winui3-example.manifest")
    nativeapi_deploy_winui3(menu_backend_test)
  endif()
endif()

# Explicit desktop integration test; not part of headless CTest runs.
if(WIN32 AND NATIVEAPI_ENABLE_WINUI3)
  add_executable(winui3_dialog_test winui3_dialog_test.cpp ../cmake/winui3-example.manifest)
  target_link_libraries(winui3_dialog_test PRIVATE nativeapi)
  nativeapi_deploy_winui3(winui3_dialog_test)
endif()

if(WIN32)
  add_executable(desktop_features_test desktop_features_test.cpp)
  target_link_libraries(desktop_features_test PRIVATE nativeapi)
  add_test(NAME desktop_features_test COMMAND desktop_features_test)
  set_tests_properties(desktop_features_test PROPERTIES TIMEOUT 20)
  if(NATIVEAPI_ENABLE_WINUI3)
    target_sources(desktop_features_test PRIVATE ../cmake/winui3-example.manifest)
    nativeapi_deploy_winui3(desktop_features_test)
  endif()
endif()

# Explicit desktop integration test for the classic Win32 title-bar backend.
if(WIN32 AND NOT NATIVEAPI_ENABLE_WINUI3)
  add_executable(window_maximize_test window_maximize_test.cpp)
  target_link_libraries(window_maximize_test PRIVATE nativeapi)
endif()

# Explicit desktop test: temporarily switches the app theme and restores it.
# Not registered with CTest because it needs an interactive desktop.
if(WIN32)
  add_executable(menu_theme_test menu_theme_test.cpp)
  target_link_libraries(menu_theme_test PRIVATE nativeapi)
endif()

# Explicit desktop integration test; requires a running window manager.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(window_size_constraints_linux_test window_size_constraints_linux_test.cpp)
  target_link_libraries(window_size_constraints_linux_test PRIVATE nativeapi)
endif()

if(WIN32)
  add_executable(window_size_constraints_windows_test window_size_constraints_windows_test.cpp)
  target_link_libraries(window_size_constraints_windows_test PRIVATE nativeapi)
endif()

# Explicit desktop integration test; requires a running window manager.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(window_resizable_linux_test window_resizable_linux_test.cpp)
  target_link_libraries(window_resizable_linux_test PRIVATE nativeapi)
endif()

# Explicit desktop integration test; creates windows but sends no input.
if(WIN32 OR (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"))
  if(APPLE)
    set_source_files_properties(window_resizable_desktop_test.cpp PROPERTIES LANGUAGE OBJCXX)
  endif()
  add_executable(window_resizable_desktop_test window_resizable_desktop_test.cpp)
  target_link_libraries(window_resizable_desktop_test PRIVATE nativeapi)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(window_controls_linux_test window_controls_linux_test.cpp)
  target_link_libraries(window_controls_linux_test PRIVATE nativeapi)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(window_restore_linux_test window_restore_linux_test.cpp)
  target_link_libraries(window_restore_linux_test PRIVATE nativeapi)
endif()

# Explicit desktop integration test; creates windows but sends no input.
if(WIN32 OR (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"))
  if(APPLE)
    set_source_files_properties(window_aspect_ratio_desktop_test.cpp PROPERTIES LANGUAGE OBJCXX)
  endif()
  add_executable(window_aspect_ratio_desktop_test window_aspect_ratio_desktop_test.cpp)
  target_link_libraries(window_aspect_ratio_desktop_test PRIVATE nativeapi)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(window_aspect_ratio_linux_test window_aspect_ratio_linux_test.cpp)
  target_link_libraries(window_aspect_ratio_linux_test PRIVATE nativeapi)
endif()
