#include "flutter_windows_angle_d3d_texture_plugin.h"

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <windows.h>

namespace flutter_windows_angle_d3d_texture {

void FlutterWindowsAngleD3dTexturePlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto plugin = std::make_unique<FlutterWindowsAngleD3dTexturePlugin>(
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "flutter_windows_angle_d3d_texture",
          &flutter::StandardMethodCodec::GetInstance(),
          registrar->texture_registrar()));
  plugin->channel()->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });
  registrar->AddPlugin(std::move(plugin));
}

FlutterWindowsAngleD3dTexturePlugin::FlutterWindowsAngleD3dTexturePlugin(
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel,
    flutter::TextureRegistrar* texture_registrar) {}

FlutterWindowsAngleD3dTexturePlugin::~FlutterWindowsAngleD3dTexturePlugin() {}

void FlutterWindowsAngleD3dTexturePlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  result->NotImplemented();
}

}  // namespace flutter_windows_angle_d3d_texture
