# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "flutter_windows_angle_d3d_texture")
project(${PROJECT_NAME} LANGUAGES CXX)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "flutter_windows_angle_d3d_texture_plugin")

# Define the plugin library target. Its name must not be changed (see comment
# on PLUGIN_NAME above).
add_library(${PLUGIN_NAME} SHARED
  "angle_surface_manager.cc"
  "flutter_windows_angle_d3d_texture_plugin.cc"
  "flutter_windows_angle_d3d_texture_plugin_c_api.cc"
  ${PLUGIN_SOURCES}
)

# Apply a standard set of build settings that are configured in the
# application-level CMakeLists.txt. This can be removed for plugins that want
# full control over build settings.
apply_standard_settings(${PLUGIN_NAME})

# Symbols are hidden by default to reduce the chance of accidental conflicts
# between plugins. This should not be removed; any symbols that should be
# exported should be explicitly exported with the FLUTTER_PLUGIN_EXPORT macro.
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include/external")

# Source include directories and library dependencies. Add any plugin-specific
# dependencies here.
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/lib/libEGL.dll.lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/lib/libGLESv2.dll.lib"
)

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or libraries created by an
# external build triggered from this build file.
set(flutter_windows_angle_d3d_texture_bundled_libraries
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-console-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-console-l1-2-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-datetime-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-debug-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-errorhandling-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-fibers-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-file-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-file-l1-2-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-file-l2-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-handle-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-heap-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-interlocked-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-libraryloader-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-localization-l1-2-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-memory-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-namedpipe-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-processenvironment-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-processthreads-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-processthreads-l1-1-1.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-profile-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-rtlsupport-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-string-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-synch-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-synch-l1-2-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-sysinfo-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-timezone-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-core-util-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-conio-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-convert-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-environment-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-filesystem-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-heap-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-locale-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-math-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-multibyte-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-private-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-process-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-runtime-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-stdio-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-string-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-time-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-crt-utility-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-downlevel-kernel32-l2-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/api-ms-win-eventing-provider-l1-1-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/d3dcompiler_47.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/libc++.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/libEGL.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/libGLESv2.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/ucrtbase.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/ucrtbased.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/vccorlib140.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/vccorlib140d.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/vcruntime140.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/vcruntime140d.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/vcruntime140_1.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/vcruntime140_1d.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/vk_swiftshader.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/vulkan-1.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/bin/dll/zlib.dll"
  PARENT_SCOPE
)
