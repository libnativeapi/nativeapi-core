# Optional MSVC/C++/WinRT integration. Packages are supplied by the consumer;
# configuring the default library never downloads or requires Windows App SDK.
set(NATIVEAPI_WINAPPSDK_DIR "" CACHE PATH "Extracted Microsoft.WindowsAppSDK NuGet package (1.6)")
set(NATIVEAPI_CPPWINRT_EXE "" CACHE FILEPATH "cppwinrt.exe from Microsoft.Windows.CppWinRT")
set(NATIVEAPI_WEBVIEW2_DIR "" CACHE PATH "Extracted Microsoft.Web.WebView2 NuGet package")

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "NATIVEAPI_ENABLE_WINUI3 requires Windows and MSVC")
endif()
if(NOT EXISTS "${NATIVEAPI_WINAPPSDK_DIR}/include/WindowsAppSDK-VersionInfo.h" OR
   NOT EXISTS "${NATIVEAPI_CPPWINRT_EXE}")
  message(FATAL_ERROR "Set NATIVEAPI_WINAPPSDK_DIR and NATIVEAPI_CPPWINRT_EXE; see docs/winui3.md")
endif()
if(NOT EXISTS "${NATIVEAPI_WEBVIEW2_DIR}/lib/Microsoft.Web.WebView2.Core.winmd")
  message(FATAL_ERROR "Set NATIVEAPI_WEBVIEW2_DIR (WinUI metadata references WebView2); see docs/winui3.md")
endif()
# This package layout and hosting API have been validated with the 1.6 release.
file(READ "${NATIVEAPI_WINAPPSDK_DIR}/include/WindowsAppSDK-VersionInfo.h" _version)
if(NOT _version MATCHES "WINDOWSAPPSDK_RELEASE_MAJORMINOR[ \t]+0x00010006")
  message(FATAL_ERROR "The WinUI3 backend currently supports Windows App SDK 1.6 packages")
endif()
if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
  set(_arch arm64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_arch x64)
else()
  set(_arch x86)
endif()
set(_projection "${CMAKE_CURRENT_BINARY_DIR}/winui3-projection")
file(GLOB _metadata "${NATIVEAPI_WINAPPSDK_DIR}/lib/uap10.0/*.winmd"
                    "${NATIVEAPI_WINAPPSDK_DIR}/lib/uap10.0.17763/*.winmd")
add_custom_command(
  OUTPUT "${_projection}/projection.stamp"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_projection}"
  COMMAND "${NATIVEAPI_CPPWINRT_EXE}" -input sdk
          -input "${NATIVEAPI_WINAPPSDK_DIR}/lib/uap10.0"
          -input "${NATIVEAPI_WINAPPSDK_DIR}/lib/uap10.0.17763"
          -input "${NATIVEAPI_WEBVIEW2_DIR}/lib/Microsoft.Web.WebView2.Core.winmd"
          -output "${_projection}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_projection}/projection.stamp"
  DEPENDS ${_metadata}
  VERBATIM)
add_custom_target(nativeapi_winui3_projection DEPENDS "${_projection}/projection.stamp")
add_dependencies(nativeapi nativeapi_winui3_projection)
target_sources(nativeapi PRIVATE platform/windows/menu_winui3_windows.cpp
  platform/windows/message_dialog_winui3_windows.cpp
  platform/windows/winui3_runtime_windows.cpp
  platform/windows/window_winui3_windows.cpp
  platform/windows/view_winui3_windows.cpp)
target_include_directories(nativeapi PRIVATE "${_projection}" "${NATIVEAPI_WINAPPSDK_DIR}/include")
target_compile_definitions(nativeapi PRIVATE NATIVEAPI_ENABLE_WINUI3 NOMINMAX)
target_link_libraries(nativeapi PRIVATE windowsapp)

# Call this for each executable using the optional backend (also for DLL consumers:
# deploy the bootstrap DLL beside the executable, not beside the plugin DLL).
function(nativeapi_deploy_winui3 target)
  if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
    set(_arch arm64)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_arch x64)
  else()
    set(_arch x86)
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${NATIVEAPI_WINAPPSDK_DIR}/runtimes/win-${_arch}/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
      "$<TARGET_FILE_DIR:${target}>"
    VERBATIM)
endfunction()
