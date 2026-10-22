// This file is a part of media_kit
// (https://github.com/media-kit/media-kit).
//
// Copyright © 2021 & onwards, Hitesh Kumar Saini <saini123hitesh@gmail.com>.
// All rights reserved.
// Use of this source code is governed by MIT license that can be found in the
// LICENSE file.

#include "angle_surface_manager.h"

#include <array>
#include <iostream>
#include <map>
#include <stdexcept>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

#define FAIL(message)                                                 \
  std::cout << "media_kit: AngleSurfaceManager: Failure: " << message \
            << std::endl;                                             \
  return false

#define CHECK_HRESULT(message) \
  if (FAILED(hr)) {            \
    FAIL(message);             \
  }

namespace {
// ANGLE may return different displays for hardware, WARP and feature levels.
std::mutex display_mutex;
std::map<EGLDisplay, size_t> display_users;
}  // namespace

AngleSurfaceManager::AngleSurfaceManager(int32_t width, int32_t height)
    : width_(width), height_(height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("Surface dimensions must be positive.");
  }
  try {
    Create();
  } catch (...) {
    CleanUp(true);
    throw;
  }
}

AngleSurfaceManager::~AngleSurfaceManager() {
  CleanUp(true);
}

void AngleSurfaceManager::HandleResize(int32_t width, int32_t height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("Surface dimensions must be positive.");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (width == width_ && height == height_ && surface_ != EGL_NO_SURFACE &&
      d3d_11_texture_2D_ && internal_d3d_11_texture_2D_) {
    return;
  }
  width_ = width;
  height_ = height;
  try {
    Create();
  } catch (...) {
    CleanUp(true);
    throw;
  }
}

void AngleSurfaceManager::Draw(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  MakeCurrent(true);
  try {
    callback();
    SwapBuffers();
  } catch (...) {
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    throw;
  }
  MakeCurrent(false);
}

void AngleSurfaceManager::Read() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (d3d_11_device_context_ && d3d_11_texture_2D_ &&
      internal_d3d_11_texture_2D_) {
    d3d_11_device_context_->CopyResource(d3d_11_texture_2D_.Get(),
                                         internal_d3d_11_texture_2D_.Get());
    d3d_11_device_context_->Flush();
  }
}

void AngleSurfaceManager::MakeCurrent(bool value) {
  const EGLBoolean ok =
      value ? eglMakeCurrent(display_, surface_, surface_, context_)
            : eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             EGL_NO_CONTEXT);
  if (!ok) {
    throw std::runtime_error("eglMakeCurrent failed.");
  }
}

void AngleSurfaceManager::SwapBuffers() {
  glFinish();
}

void AngleSurfaceManager::Create() {
  CleanUp(false);
  if (!CreateD3DTexture()) {
    throw std::runtime_error("Unable to create Windows Direct3D device.");
    return;
  }
  if (!CreateEGLDisplay()) {
    throw std::runtime_error("Unable to create ANGLE EGL display.");
    return;
  }
  if (!CreateAndBindEGLSurface()) {
    throw std::runtime_error("Unable to create ANGLE EGL surface.");
    return;
  }
  if (internal_handle_ == nullptr || handle_ == nullptr) {
    throw std::runtime_error("Unable to retrieve Direct3D shared HANDLE.");
    return;
  }
}

void AngleSurfaceManager::CleanUp(bool release_context) {
  if (display_ != EGL_NO_DISPLAY) {
    if (context_ != EGL_NO_CONTEXT && eglGetCurrentContext() == context_) {
      eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (surface_ != EGL_NO_SURFACE) {
      eglDestroySurface(display_, surface_);
      surface_ = EGL_NO_SURFACE;
    }
  }
  internal_d3d_11_texture_2D_.Reset();
  d3d_11_texture_2D_.Reset();
  internal_handle_ = nullptr;
  handle_ = nullptr;
  if (release_context) {
    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) {
      eglDestroyContext(display_, context_);
      context_ = EGL_NO_CONTEXT;
    }
    if (display_registered_) {
      std::lock_guard<std::mutex> lock(display_mutex);
      auto entry = display_users.find(display_);
      if (entry != display_users.end() && --entry->second == 0) {
        eglTerminate(display_);
        display_users.erase(entry);
      }
      display_registered_ = false;
    }
    display_ = EGL_NO_DISPLAY;
    // Release D3D device & context if the instance is being destroyed.
    if (d3d_11_device_context_) {
      d3d_11_device_context_->Release();
      d3d_11_device_context_ = nullptr;
    }
    if (d3d_11_device_) {
      d3d_11_device_->Release();
      d3d_11_device_ = nullptr;
    }
  }
}

bool AngleSurfaceManager::CreateD3DTexture() {
  if (d3d_11_device_ == nullptr) {
    constexpr std::array<D3D_FEATURE_LEVEL, 6> feature_levels = {
        D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    // Select the highest supported level through D3D11, with WARP fallback.
    // ANGLE chromium/8078 supports levels 10_0 and newer, not 9_x.
    const auto create_device = [&](D3D_DRIVER_TYPE driver_type) {
      HRESULT result = E_INVALIDARG;
      for (size_t first = 0; first < feature_levels.size(); ++first) {
        result = ::D3D11CreateDevice(
            nullptr, driver_type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            feature_levels.data() + first,
            static_cast<UINT>(feature_levels.size() - first), D3D11_SDK_VERSION,
            &d3d_11_device_, nullptr, &d3d_11_device_context_);
        // Older runtimes reject unrecognized feature-level enums rather than
        // selecting a lower supported level from the same list.
        if (result != E_INVALIDARG)
          break;
      }
      return result;
    };
    auto hr = create_device(D3D_DRIVER_TYPE_HARDWARE);
    if (FAILED(hr)) {
      use_warp_ = true;
      hr = create_device(D3D_DRIVER_TYPE_WARP);
    }
    CHECK_HRESULT("D3D11CreateDevice");
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device = nullptr;
  auto dxgi_device_success = d3d_11_device_->QueryInterface(
      __uuidof(IDXGIDevice), (void**)&dxgi_device);
  if (SUCCEEDED(dxgi_device_success) && dxgi_device != nullptr) {
    dxgi_device->SetGPUThreadPriority(5);  // Must be in interval [-7, 7].
  }

  auto level = d3d_11_device_->GetFeatureLevel();
  std::cout << "media_kit: AngleSurfaceManager: Direct3D Feature Level: "
            << (((unsigned)level) >> 12) << "_"
            << ((((unsigned)level) >> 8) & 0xf) << std::endl;
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

  // The general idea is to create two textures, one that is used to |Draw|
  // using ANGLE & another one that is used for |Read| the rendered content
  // using |handle|.
  // The internal texture is copied to the public texture once a frame is
  // requested using |ID3D11DeviceContext::CopyResource|. Draw and Read are
  // serialized here; consumers still own synchronization of their GPU reads.

  // Internal.
  auto hr = d3d_11_device_->CreateTexture2D(&d3d11_texture2D_desc, nullptr,
                                            &internal_d3d_11_texture_2D_);
  CHECK_HRESULT("ID3D11Device::CreateTexture2D");
  auto resource = Microsoft::WRL::ComPtr<IDXGIResource>{};
  hr = internal_d3d_11_texture_2D_.As(&resource);
  CHECK_HRESULT("ID3D11Texture2D::As");
  // Retrieve the shared |HANDLE| for interop.
  hr = resource->GetSharedHandle(&internal_handle_);
  CHECK_HRESULT("IDXGIResource::GetSharedHandle");

  // External.
  hr = d3d_11_device_->CreateTexture2D(&d3d11_texture2D_desc, nullptr,
                                       &d3d_11_texture_2D_);
  CHECK_HRESULT("ID3D11Device::CreateTexture2D");
  hr = d3d_11_texture_2D_.As(&resource);
  CHECK_HRESULT("ID3D11Texture2D::As");
  // Retrieve the shared |HANDLE| for interop.
  hr = resource->GetSharedHandle(&handle_);
  CHECK_HRESULT("IDXGIResource::GetSharedHandle");

  return true;
}

bool AngleSurfaceManager::CreateEGLDisplay() {
  std::lock_guard<std::mutex> lock(display_mutex);
  if (display_ == EGL_NO_DISPLAY) {
    auto eglGetPlatformDisplayEXT =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (eglGetPlatformDisplayEXT) {
      const EGLint* attributes =
          use_warp_ ? kD3D11WarpDisplayAttributes : kD3D11DisplayAttributes;
      display_ = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE,
                                          EGL_DEFAULT_DISPLAY, attributes);
      if (eglInitialize(display_, 0, 0) == EGL_FALSE) {
        FAIL("eglInitialize D3D11");
      }
    } else {
      FAIL("eglGetProcAddress");
    }
    ++display_users[display_];
    display_registered_ = true;
  }
  return true;
}

bool AngleSurfaceManager::CreateAndBindEGLSurface() {
  // Do not create |context_| again, likely due to |Resize|.
  if (context_ == EGL_NO_CONTEXT) {
    // First time from the constructor itself.
    auto count = 0;
    auto result = eglChooseConfig(display_, kEGLConfigurationAttributes,
                                  &config_, 1, &count);
    if (result == EGL_FALSE || count == 0) {
      FAIL("eglChooseConfig");
    }
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT,
                                kEGLContextAttributes);
    if (context_ == EGL_NO_CONTEXT) {
      FAIL("eglCreateContext");
    }
  }
  EGLint buffer_attributes[] = {
      EGL_WIDTH,          width_,         EGL_HEIGHT,         height_,
      EGL_TEXTURE_TARGET, EGL_TEXTURE_2D, EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
      EGL_NONE,
  };
  surface_ = eglCreatePbufferFromClientBuffer(
      display_, EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE, internal_handle_,
      config_, buffer_attributes);
  if (surface_ == EGL_NO_SURFACE) {
    FAIL("eglCreatePbufferFromClientBuffer");
  }
  // Draw renders directly into this pbuffer. Binding it as a GLES texture
  // would require a current context and would conflict with rendering to it.
  return true;
}
