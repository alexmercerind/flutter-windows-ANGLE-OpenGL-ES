# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)
cmake_policy(VERSION 3.14...3.25)

# Project-level configuration.
set(PROJECT_NAME "flutter_windows_angle_opengl_es")
project(${PROJECT_NAME} LANGUAGES CXX)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "flutter_windows_angle_opengl_es_plugin")

list(APPEND PLUGIN_SOURCES
  "angle_surface_manager.cpp"
  "angle_surface_manager.h"
  "flutter_windows_angle_opengl_es_plugin.cpp"
  "flutter_windows_angle_opengl_es_plugin.h"
)

# Define the plugin library target. Its name must not be changed (see comment
# on PLUGIN_NAME above).
add_library(${PLUGIN_NAME} SHARED
  "include/flutter_windows_angle_opengl_es/flutter_windows_angle_opengl_es_plugin_c_api.h"
  "flutter_windows_angle_opengl_es_plugin_c_api.cpp"
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

if(DEFINED FLUTTER_TARGET_PLATFORM)
  if(FLUTTER_TARGET_PLATFORM STREQUAL "windows-x64")
    set(ANGLE_ARCH "x64")
  elseif(FLUTTER_TARGET_PLATFORM STREQUAL "windows-arm64")
    set(ANGLE_ARCH "arm64")
  else()
    message(FATAL_ERROR "Unsupported Flutter target: ${FLUTTER_TARGET_PLATFORM}")
  endif()
elseif(CMAKE_GENERATOR_PLATFORM)
  string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" ANGLE_ARCH)
  if(NOT ANGLE_ARCH MATCHES "^(x64|arm64)$")
    message(FATAL_ERROR "Unsupported Windows platform: ${CMAKE_GENERATOR_PLATFORM}")
  endif()
else()
  message(FATAL_ERROR "Windows target architecture was not provided")
endif()
if(CMAKE_GENERATOR_PLATFORM)
  string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" GENERATOR_ARCH)
  if(NOT GENERATOR_ARCH STREQUAL ANGLE_ARCH)
    message(FATAL_ERROR "Flutter target ${ANGLE_ARCH} conflicts with CMake platform ${GENERATOR_ARCH}")
  endif()
endif()
set(ANGLE_BIN "${CMAKE_CURRENT_SOURCE_DIR}/bin/${ANGLE_ARCH}")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include/external")

# Source include directories and library dependencies. Add any plugin-specific
# dependencies here.
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE
  d3d11.lib
  dxgi.lib
  flutter
  flutter_wrapper_plugin
  "${ANGLE_BIN}/lib/libEGL.dll.lib"
  "${ANGLE_BIN}/lib/libGLESv2.dll.lib"
)

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or libraries created by an
# external build triggered from this build file.
set(flutter_windows_angle_opengl_es_bundled_libraries
  "${ANGLE_BIN}/dll/d3dcompiler_47.dll"
  "${ANGLE_BIN}/dll/libEGL.dll"
  "${ANGLE_BIN}/dll/libGLESv2.dll"
  "${ANGLE_BIN}/dll/vulkan-1.dll"
  "${ANGLE_BIN}/dll/vk_swiftshader.dll"
  "${ANGLE_BIN}/dll/vk_swiftshader_icd.json"
  PARENT_SCOPE
)
