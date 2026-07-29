# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "flutter_windows_angle_opengl_es")
project(${PROJECT_NAME} LANGUAGES CXX)

# Explicitly opt in to modern CMake behaviors to avoid warnings with recent
# versions of CMake.
cmake_policy(VERSION 3.14...3.25)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "flutter_windows_angle_opengl_es_plugin")

# Any new source files that you add to the plugin should be added here.
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
  if("${FLUTTER_TARGET_PLATFORM}" STREQUAL "windows-x64")
    set(ANGLE_ARCH "x64")
  elseif("${FLUTTER_TARGET_PLATFORM}" STREQUAL "windows-arm64")
    set(ANGLE_ARCH "arm64")
  else()
    message(FATAL_ERROR
      "Unsupported FLUTTER_TARGET_PLATFORM: ${FLUTTER_TARGET_PLATFORM}")
  endif()
elseif(DEFINED CMAKE_GENERATOR_PLATFORM AND
       NOT "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
  string(TOUPPER "${CMAKE_GENERATOR_PLATFORM}" ANGLE_GENERATOR_PLATFORM)
  if("${ANGLE_GENERATOR_PLATFORM}" STREQUAL "X64")
    set(ANGLE_ARCH "x64")
  elseif("${ANGLE_GENERATOR_PLATFORM}" STREQUAL "ARM64")
    set(ANGLE_ARCH "arm64")
  else()
    message(FATAL_ERROR
      "Unsupported CMAKE_GENERATOR_PLATFORM: ${CMAKE_GENERATOR_PLATFORM}")
  endif()
else()
  message(FATAL_ERROR
    "Unable to determine the Windows target architecture. "
    "FLUTTER_TARGET_PLATFORM or CMAKE_GENERATOR_PLATFORM is required.")
endif()

set(ANGLE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/external")
set(ANGLE_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin/${ANGLE_ARCH}")
set(ANGLE_DLL_DIR "${ANGLE_BIN_DIR}/dll")
set(ANGLE_LIB_DIR "${ANGLE_BIN_DIR}/lib")

foreach(ANGLE_REQUIRED_DIRECTORY
    "${ANGLE_INCLUDE_DIR}/EGL"
    "${ANGLE_INCLUDE_DIR}/GLES2"
    "${ANGLE_INCLUDE_DIR}/GLES3"
    "${ANGLE_INCLUDE_DIR}/KHR"
    "${ANGLE_DLL_DIR}"
    "${ANGLE_LIB_DIR}")
  if(NOT IS_DIRECTORY "${ANGLE_REQUIRED_DIRECTORY}")
    message(FATAL_ERROR
      "Missing ANGLE directory for ${ANGLE_ARCH}: ${ANGLE_REQUIRED_DIRECTORY}")
  endif()
endforeach()

set(ANGLE_EGL_IMPORT_LIBRARY "${ANGLE_LIB_DIR}/libEGL.dll.lib")
set(ANGLE_GLES_IMPORT_LIBRARY "${ANGLE_LIB_DIR}/libGLESv2.dll.lib")
foreach(ANGLE_REQUIRED_LIBRARY
    "${ANGLE_EGL_IMPORT_LIBRARY}"
    "${ANGLE_GLES_IMPORT_LIBRARY}")
  if(NOT EXISTS "${ANGLE_REQUIRED_LIBRARY}")
    message(FATAL_ERROR
      "Missing ANGLE import library for ${ANGLE_ARCH}: ${ANGLE_REQUIRED_LIBRARY}")
  endif()
endforeach()

target_include_directories(${PLUGIN_NAME} PRIVATE "${ANGLE_INCLUDE_DIR}")

# Source include directories and library dependencies. Add any plugin-specific
# dependencies here.
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE
  d3d11.lib
  dxgi.lib
  flutter
  flutter_wrapper_plugin
  "${ANGLE_EGL_IMPORT_LIBRARY}"
  "${ANGLE_GLES_IMPORT_LIBRARY}"
)

# List of absolute paths to libraries that should be bundled with the plugin.
# These files are collected from the same verified staging directory used to
# create the release ZIP. No architecture fallback is permitted.
set(flutter_windows_angle_opengl_es_bundled_libraries
  "${ANGLE_DLL_DIR}/d3dcompiler_47.dll"
  "${ANGLE_DLL_DIR}/libEGL.dll"
  "${ANGLE_DLL_DIR}/libGLESv2.dll"
  "${ANGLE_DLL_DIR}/vk_swiftshader.dll"
  "${ANGLE_DLL_DIR}/vk_swiftshader_icd.json"
  "${ANGLE_DLL_DIR}/vulkan-1.dll"
)

foreach(ANGLE_RUNTIME_FILE IN LISTS
    flutter_windows_angle_opengl_es_bundled_libraries)
  if(NOT EXISTS "${ANGLE_RUNTIME_FILE}")
    message(FATAL_ERROR
      "Missing required ANGLE runtime file for ${ANGLE_ARCH}: ${ANGLE_RUNTIME_FILE}")
  endif()
endforeach()

foreach(ANGLE_OPTIONAL_RUNTIME libc++.dll zlib.dll)
  if(EXISTS "${ANGLE_DLL_DIR}/${ANGLE_OPTIONAL_RUNTIME}")
    list(APPEND flutter_windows_angle_opengl_es_bundled_libraries
      "${ANGLE_DLL_DIR}/${ANGLE_OPTIONAL_RUNTIME}")
  endif()
endforeach()

set(flutter_windows_angle_opengl_es_bundled_libraries
  "${flutter_windows_angle_opengl_es_bundled_libraries}"
  PARENT_SCOPE)
