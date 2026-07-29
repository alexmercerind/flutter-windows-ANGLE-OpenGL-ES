#ifndef ANGLE_SURFACE_MANAGER_H_
#define ANGLE_SURFACE_MANAGER_H_

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>

#include <cstdint>
#include <functional>
#include <mutex>

// Provides an OpenGL ES render target backed by a D3D11 shared texture.
class ANGLESurfaceManager {
 public:
  ANGLESurfaceManager(int32_t width, int32_t height);
  ~ANGLESurfaceManager();

  ANGLESurfaceManager(const ANGLESurfaceManager&) = delete;
  ANGLESurfaceManager& operator=(const ANGLESurfaceManager&) = delete;

  int32_t width() const;
  int32_t height() const;
  HANDLE handle() const;

  void HandleResize(int32_t width, int32_t height);
  void Draw(std::function<void()> callback);
  void Read();

 private:
  void Create();
  void CreateD3DDevice();
  bool TryCreateD3DDevice(D3D_DRIVER_TYPE driver_type,
                          const D3D_FEATURE_LEVEL* feature_levels,
                          UINT feature_level_count);
  void CreateD3DTextures();
  void CreateEGLDisplay();
  void CreateEGLContext();
  void CreateEGLSurface();
  void DestroyEGLSurface();
  void MakeCurrent(bool value);
  void CleanUp() noexcept;

  int32_t width_ = 1;
  int32_t height_ = 1;
  bool using_warp_ = false;
  bool feature_level_9_3_fallback_ = false;
  D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_9_3;
  HANDLE internal_handle_ = nullptr;
  HANDLE handle_ = nullptr;
  mutable std::mutex mutex_;

  Microsoft::WRL::ComPtr<IDXGIFactory> dxgi_factory_;
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter_;
  Microsoft::WRL::ComPtr<ID3D11Device> d3d_11_device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_11_device_context_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> internal_d3d_11_texture_2d_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> d3d_11_texture_2d_;

  EGLSurface surface_ = EGL_NO_SURFACE;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
  EGLConfig config_ = nullptr;
};

#endif  // ANGLE_SURFACE_MANAGER_H_
