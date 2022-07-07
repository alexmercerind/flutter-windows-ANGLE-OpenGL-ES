#ifndef FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_D3D_TEXTURE_PLUGIN_H_
#define FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_D3D_TEXTURE_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/texture_registrar.h>

#include <memory>

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

  flutter::TextureRegistrar* texture_registrar_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;
};

}  // namespace flutter_windows_angle_d3d_texture

#endif
