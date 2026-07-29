#include "include/flutter_windows_angle_opengl_es/flutter_windows_angle_opengl_es_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "flutter_windows_angle_opengl_es_plugin.h"

void FlutterWindowsAngleOpenglEsPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  flutter_windows_angle_opengl_es::FlutterWindowsAngleOpenglEsPlugin::
      RegisterWithRegistrar(
          flutter::PluginRegistrarManager::GetInstance()
              ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
