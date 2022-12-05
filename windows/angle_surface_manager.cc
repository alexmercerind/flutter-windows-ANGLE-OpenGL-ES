#include "angle_surface_manager.h"

#include <iostream>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3d11.lib")

#define RETURN(message)                                                 \
  std::cout << "ANGLESurfaceManager Failure: " << message << std::endl; \
  return false

#define RETURN_IF_FAILED(message) \
  if (FAILED(hr)) {               \
    RETURN(message);              \
  }

ANGLESurfaceManager::ANGLESurfaceManager(HWND window, int32_t width,
                                         int32_t height)
    : window_(window), width_(width), height_(height) {
  // Presently, I believe it is a good idea to show these failure messages
  // directly to the user. It'll help fix the platform & hardware specific
  // issues.

  // Create D3D device & texture.
  auto success = InitializeD3D11();
  if (!success) {
    success = InitializeD3D9();
  }
  // Exit on error.
  if (!success) {
    ShowFailureMessage(L"Unable to create Windows Direct3D device.");
    return;
  }
  // Create & bind ANGLE EGL surface.
  success = CreateAndBindEGLSurface();
  // Exit on error.
  if (!success) {
    ShowFailureMessage(L"Unable to create ANGLE EGL surface.");
    return;
  }
  // Additional check.
  if (shared_handle_ == nullptr) {
    ShowFailureMessage(L"Unable to retrieve Direct3D shared HANDLE.");
    return;
  }
}

bool ANGLESurfaceManager::InitializeD3D11() {
  auto hr = ::D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                D3D11_CREATE_DEVICE_VIDEO_SUPPORT, NULL, 0,
                                D3D11_SDK_VERSION, &d3d_11_device_, NULL,
                                &d3d_11_device_context_);
  RETURN_IF_FAILED("D3D11CreateDevice");
  auto d3d11_texture2D_desc = D3D11_TEXTURE2D_DESC{0};
  d3d11_texture2D_desc.Width = width_;
  d3d11_texture2D_desc.Height = height_;
  d3d11_texture2D_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  d3d11_texture2D_desc.MipLevels = 1;
  d3d11_texture2D_desc.ArraySize = 1;
  d3d11_texture2D_desc.SampleDesc.Count = 1;
  d3d11_texture2D_desc.SampleDesc.Quality = 0;
  d3d11_texture2D_desc.Usage = D3D11_USAGE_DEFAULT;
  d3d11_texture2D_desc.BindFlags =
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  d3d11_texture2D_desc.CPUAccessFlags = 0;
  d3d11_texture2D_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
  hr = d3d_11_device_->CreateTexture2D(&d3d11_texture2D_desc, nullptr,
                                       &d3d11_texture_2D_);
  RETURN_IF_FAILED("ID3D11Device::CreateTexture2D");
  auto resource = Microsoft::WRL::ComPtr<IDXGIResource>{};
  hr = d3d11_texture_2D_.As(&resource);
  RETURN_IF_FAILED("ID3D11Texture2D::As");
  // IMPORTANT: Retrieve |shared_handle_| for interop.
  hr = resource->GetSharedHandle(&shared_handle_);
  RETURN_IF_FAILED("IDXGIResource::GetSharedHandle");
  auto eglGetPlatformDisplayEXT =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
          eglGetProcAddress("eglGetPlatformDisplayEXT"));
  // D3D11.
  display_ = eglGetPlatformDisplayEXT(
      EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, kD3D11DisplayAttributes);
  if (eglInitialize(display_, NULL, NULL) == EGL_FALSE) {
    // D3D 9.3 (D3D11 compatibility).
    display_ =
        eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY,
                                 kD3D9_3DisplayAttributes);
    if (eglInitialize(display_, NULL, NULL) == EGL_FALSE) {
      // Whatever.
      display_ =
          eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE,
                                   EGL_DEFAULT_DISPLAY, kWrapDisplayAttributes);
      if (eglInitialize(display_, NULL, NULL) == EGL_FALSE) {
        RETURN("eglGetPlatformDisplayEXT");
      }
    }
  }
  return true;
}

bool ANGLESurfaceManager::InitializeD3D9() {
  auto hr = ::Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d_9_ex_);
  RETURN_IF_FAILED("Direct3DCreate9Ex");
  auto present_params = D3DPRESENT_PARAMETERS{};
  present_params.BackBufferWidth = width_;
  present_params.BackBufferHeight = height_;
  present_params.BackBufferFormat = D3DFMT_A8R8G8B8;
  present_params.BackBufferCount = 1;
  present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
  present_params.hDeviceWindow = 0;
  present_params.Windowed = TRUE;
  present_params.Flags = D3DPRESENTFLAG_VIDEO;
  present_params.FullScreen_RefreshRateInHz = 0;
  present_params.PresentationInterval = 0;
  hr = d3d_9_ex_->CreateDeviceEx(
      D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL,
      D3DCREATE_FPU_PRESERVE | D3DCREATE_HARDWARE_VERTEXPROCESSING |
          D3DCREATE_DISABLE_PSGP_THREADING | D3DCREATE_MULTITHREADED,
      &present_params, NULL, &d3d_9_device_ex_);
  RETURN_IF_FAILED("IDirect3D9Ex::CreateDeviceEx");
  // IMPORTANT: Retrieve |shared_handle_| for interop.
  hr = d3d_9_device_ex_->CreateTexture(
      width_, height_, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
      D3DPOOL_DEFAULT, &d3d_9_texture_, &shared_handle_);
  RETURN_IF_FAILED("IDirect3DDevice9Ex::CreateTexture");
  auto eglGetPlatformDisplayEXT =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
          eglGetProcAddress("eglGetPlatformDisplayEXT"));
  // D3D 9.
  display_ = eglGetPlatformDisplayEXT(
      EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, kD3D9DisplayAttributes);
  if (eglInitialize(display_, NULL, NULL) == EGL_FALSE) {
    RETURN("eglGetPlatformDisplayEXT");
  }
  return true;
}

bool ANGLESurfaceManager::CreateAndBindEGLSurface() {
  auto count = 0;
  auto result = eglChooseConfig(display_, kEGLConfigurationAttributes, &config_,
                                1, &count);
  if (result == EGL_FALSE || count == 0) {
    RETURN("eglChooseConfig");
  }
  context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT,
                              kEGLContextAttributes);
  if (context_ == EGL_NO_CONTEXT) {
    RETURN("eglCreateContext");
  }
  EGLint buffer_attributes[] = {EGL_WIDTH,
                                width_,
                                EGL_HEIGHT,
                                height_,
                                EGL_TEXTURE_TARGET,
                                EGL_TEXTURE_2D,
                                EGL_TEXTURE_FORMAT,
                                EGL_TEXTURE_RGBA,
                                EGL_NONE};
  surface_ = eglCreatePbufferFromClientBuffer(
      display_, EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE, shared_handle_, config_,
      buffer_attributes);
  if (surface_ == EGL_NO_SURFACE) {
    RETURN("eglCreatePbufferFromClientBuffer");
  }
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  eglBindTexImage(display_, surface_, EGL_BACK_BUFFER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return true;
}

void ANGLESurfaceManager::ShowFailureMessage(std::wstring message) {
  ::MessageBox(window_, message.c_str(), L"ANGLESurfaceManager",
               MB_ICONERROR | MB_OK | MB_DEFBUTTON1);
  // Quit process.
  ::exit(1);
}
