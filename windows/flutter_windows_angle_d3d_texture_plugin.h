#ifndef FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_D3D_TEXTURE_PLUGIN_H_
#define FLUTTER_PLUGIN_FLUTTER_WINDOWS_ANGLE_D3D_TEXTURE_PLUGIN_H_

#ifndef EGL_EGL_PROTOTYPES
#define EGL_EGL_PROTOTYPES 1
#endif

// Flutter
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/texture_registrar.h>
// Win32 / C++/WinRT / C++/CX
#include <windows.h>
#include <wrl.h>
// Direct3D 11
#include <d3d.h>
#include <d3d11.h>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
// Standard C++ STL
#include <iostream>
#include <memory>
// ANGLE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

using namespace Microsoft::WRL;

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
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;
  flutter::TextureRegistrar* texture_registrar_ = nullptr;
  ID3D11Device* d3d11_device_ = nullptr;
  ID3D11DeviceContext* d3d11_device_context_ = nullptr;
  ComPtr<ID3D11Texture2D> d3d11_texture_2D_;
  HANDLE shared_handle_;
  EGLSurface surface_ = EGL_NO_SURFACE;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLConfig config_ = NULL;
};

}  // namespace flutter_windows_angle_d3d_texture

#endif
