cmake_minimum_required(VERSION 3.16)
project(tray_stats_example)

set(CMAKE_CXX_STANDARD 17)

# Include the nativeapi headers
include_directories(${CMAKE_SOURCE_DIR}/../../include)

# Add the executable
add_executable(tray_stats_example main.cpp)

# Link against the nativeapi library
target_link_libraries(tray_stats_example nativeapi)
if(NATIVEAPI_ENABLE_WINUI3)
    target_sources(tray_stats_example PRIVATE "../../cmake/winui3-example.manifest")
    nativeapi_deploy_winui3(tray_stats_example)
endif()

# Platform-specific settings
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iOS uses UIKit, Foundation, CoreGraphics (already linked by nativeapi)
elseif(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    target_link_libraries(tray_stats_example ${COCOA_LIBRARY})
elseif(WIN32)
    target_link_libraries(tray_stats_example user32 kernel32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(tray_stats_example PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(tray_stats_example ${GTK3_LIBRARIES})
elseif(ANDROID)
    target_link_libraries(tray_stats_example log android)
endif()
