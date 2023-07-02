#ifndef FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_OPENGL_ES_PLUGIN_H_
#define FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_OPENGL_ES_PLUGIN_H_

#ifndef EGL_EGL_PROTOTYPES
#define EGL_EGL_PROTOTYPES 1
#endif

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/texture_registrar.h>

#include "angle_surface_manager.h"

namespace flutter_windows_angle_opengl_es {

class FlutterWindowsANGLEOpenGLESPlugin : public flutter::Plugin {
 public:
  flutter::MethodChannel<flutter::EncodableValue>* channel() const {
    return channel_.get();
  }

  flutter::TextureRegistrar* texture_registrar() const {
    return texture_registrar_;
  }

  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

  FlutterWindowsANGLEOpenGLESPlugin(
      flutter::PluginRegistrarWindows* registrar,
      std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel,
      flutter::TextureRegistrar* texture_registrar);

  virtual ~FlutterWindowsANGLEOpenGLESPlugin();

  FlutterWindowsANGLEOpenGLESPlugin(const FlutterWindowsANGLEOpenGLESPlugin&) =
      delete;
  FlutterWindowsANGLEOpenGLESPlugin& operator=(
      const FlutterWindowsANGLEOpenGLESPlugin&) = delete;

 private:
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  std::unique_ptr<ANGLESurfaceManager> surface_manager_;

  std::unique_ptr<FlutterDesktopGpuSurfaceDescriptor> texture_;
  std::unique_ptr<flutter::TextureVariant> texture_variant_;

  flutter::PluginRegistrarWindows* registrar_;
  flutter::TextureRegistrar* texture_registrar_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;
};

}  // namespace flutter_windows_angle_opengl_es

#endif
