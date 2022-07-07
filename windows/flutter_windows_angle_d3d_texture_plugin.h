#ifndef FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_D3D_TEXTURE_PLUGIN_H_
#define FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_D3D_TEXTURE_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <memory>

namespace flutter_windows_angle_d3d_texture {

class FlutterWindowsAngleD3dTexturePlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  FlutterWindowsAngleD3dTexturePlugin();

  virtual ~FlutterWindowsAngleD3dTexturePlugin();

  // Disallow copy and assign.
  FlutterWindowsAngleD3dTexturePlugin(const FlutterWindowsAngleD3dTexturePlugin&) = delete;
  FlutterWindowsAngleD3dTexturePlugin& operator=(const FlutterWindowsAngleD3dTexturePlugin&) = delete;

 private:
  // Called when a method is called on this plugin's channel from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
};

}  // namespace flutter_windows_angle_d3d_texture

#endif  // FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_D3D_TEXTURE_PLUGIN_H_
