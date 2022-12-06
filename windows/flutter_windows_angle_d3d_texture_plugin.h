#ifndef FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_D3D_TEXTURE_PLUGIN_H_
#define FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_D3D_TEXTURE_PLUGIN_H_

#ifndef EGL_EGL_PROTOTYPES
#define EGL_EGL_PROTOTYPES 1
#endif

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/texture_registrar.h>

#include "angle_surface_manager.h"

namespace flutter_windows_angle_d3d_texture {

class FlutterWindowsAngleD3dTexturePlugin : public flutter::Plugin {
 public:
  flutter::MethodChannel<flutter::EncodableValue>* channel() const {
    return channel_.get();
  }

  flutter::TextureRegistrar* texture_registrar() const {
    return texture_registrar_;
  }

  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

  FlutterWindowsAngleD3dTexturePlugin(
      flutter::PluginRegistrarWindows* registrar,
      std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel,
      flutter::TextureRegistrar* texture_registrar);

  virtual ~FlutterWindowsAngleD3dTexturePlugin();

  FlutterWindowsAngleD3dTexturePlugin(
      const FlutterWindowsAngleD3dTexturePlugin&) = delete;
  FlutterWindowsAngleD3dTexturePlugin& operator=(
      const FlutterWindowsAngleD3dTexturePlugin&) = delete;

 private:
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  flutter::PluginRegistrarWindows* registrar_ = nullptr;
  flutter::TextureRegistrar* texture_registrar_ = nullptr;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_ =
      nullptr;
  std::unique_ptr<ANGLESurfaceManager> surface_manager_ = nullptr;
  std::unique_ptr<FlutterDesktopGpuSurfaceDescriptor> texture_ = nullptr;
  std::unique_ptr<flutter::TextureVariant> texture_variant_ = nullptr;

  int64_t texture_id_ = 0;
};

}  // namespace flutter_windows_angle_d3d_texture

#endif
