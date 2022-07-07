#include "include/flutter_windows_angle_d3d_texture/flutter_windows_angle_d3d_texture_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "flutter_windows_angle_d3d_texture_plugin.h"

void FlutterWindowsAngleD3dTexturePluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  flutter_windows_angle_d3d_texture::FlutterWindowsAngleD3dTexturePlugin::
      RegisterWithRegistrar(
          flutter::PluginRegistrarManager::GetInstance()
              ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
