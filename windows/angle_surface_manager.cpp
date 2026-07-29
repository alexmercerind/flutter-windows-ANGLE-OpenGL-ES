#include "angle_surface_manager.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

std::string Hex(unsigned long value) {
  constexpr char kDigits[] = "0123456789ABCDEF";
  std::string result = "0x00000000";
  for (int index = 0; index < 8; ++index) {
    result[9 - index] = kDigits[value & 0xF];
    value >>= 4;
  }
  return result;
}

void CheckHResult(HRESULT result, const char* operation) {
  if (FAILED(result)) {
    throw std::runtime_error(std::string(operation) + " failed with HRESULT " +
                             Hex(static_cast<unsigned long>(result)));
  }
}

void CheckEGL(EGLBoolean result, const char* operation) {
  if (result != EGL_TRUE) {
    throw std::runtime_error(std::string(operation) +
                             " failed with EGL error " +
                             Hex(static_cast<unsigned long>(eglGetError())));
  }
}

void LogEGLFailure(EGLBoolean result, const char* operation) noexcept {
  if (result != EGL_TRUE) {
    std::cerr << "ANGLESurfaceManager: " << operation
              << " failed during cleanup with EGL error "
              << Hex(static_cast<unsigned long>(eglGetError())) << std::endl;
  }
}

}  // namespace

ANGLESurfaceManager::ANGLESurfaceManager(int32_t width, int32_t height)
    : width_(width), height_(height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("ANGLE surface dimensions must be positive");
  }
  Create();
}

ANGLESurfaceManager::~ANGLESurfaceManager() {
  std::lock_guard<std::mutex> lock(mutex_);
  CleanUp();
}

int32_t ANGLESurfaceManager::width() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return width_;
}

int32_t ANGLESurfaceManager::height() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return height_;
}

HANDLE ANGLESurfaceManager::handle() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return handle_;
}

void ANGLESurfaceManager::HandleResize(int32_t width, int32_t height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("ANGLE surface dimensions must be positive");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (width == width_ && height == height_) {
    return;
  }
  MakeCurrent(false);
  DestroyEGLSurface();
  internal_d3d_11_texture_2d_.Reset();
  d3d_11_texture_2d_.Reset();
  internal_handle_ = nullptr;
  handle_ = nullptr;
  width_ = width;
  height_ = height;
  CreateD3DTextures();
  CreateEGLSurface();
}

void ANGLESurfaceManager::Draw(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  MakeCurrent(true);
  try {
    callback();
    glFinish();
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
      throw std::runtime_error("OpenGL ES rendering failed with error " +
                               Hex(static_cast<unsigned long>(error)));
    }
    MakeCurrent(false);
  } catch (...) {
    if (eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT) != EGL_TRUE) {
      std::cerr << "ANGLESurfaceManager: failed to clear current context after "
                   "rendering exception"
                << std::endl;
    }
    throw;
  }
}

void ANGLESurfaceManager::Read() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!d3d_11_device_context_ || !d3d_11_texture_2d_ ||
      !internal_d3d_11_texture_2d_) {
    throw std::runtime_error("D3D11 texture resources are not initialized");
  }
  d3d_11_device_context_->CopyResource(d3d_11_texture_2d_.Get(),
                                       internal_d3d_11_texture_2d_.Get());
  d3d_11_device_context_->Flush();
}

void ANGLESurfaceManager::Create() {
  try {
    CreateD3DDevice();
    CreateD3DTextures();
    CreateEGLDisplay();
    CreateEGLContext();
    CreateEGLSurface();
  } catch (...) {
    CleanUp();
    throw;
  }
}

bool ANGLESurfaceManager::TryCreateD3DDevice(
    D3D_DRIVER_TYPE driver_type,
    const D3D_FEATURE_LEVEL* feature_levels,
    UINT feature_level_count) {
  d3d_11_device_.Reset();
  d3d_11_device_context_.Reset();
  feature_level_ = D3D_FEATURE_LEVEL_9_3;
  const HRESULT result = D3D11CreateDevice(
      driver_type == D3D_DRIVER_TYPE_UNKNOWN ? adapter_.Get() : nullptr,
      driver_type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels,
      feature_level_count, D3D11_SDK_VERSION, &d3d_11_device_, &feature_level_,
      &d3d_11_device_context_);
  return SUCCEEDED(result);
}

void ANGLESurfaceManager::CreateD3DDevice() {
  constexpr std::array<D3D_FEATURE_LEVEL, 4> kFeatureLevels = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
  };
  constexpr std::array<D3D_FEATURE_LEVEL, 1> kFeatureLevel9_3 = {
      D3D_FEATURE_LEVEL_9_3,
  };

  CheckHResult(CreateDXGIFactory(IID_PPV_ARGS(&dxgi_factory_)),
               "CreateDXGIFactory");
  CheckHResult(dxgi_factory_->EnumAdapters(0, &adapter_),
               "IDXGIFactory::EnumAdapters");

  using_warp_ = false;
  feature_level_9_3_fallback_ = false;
  if (!TryCreateD3DDevice(D3D_DRIVER_TYPE_UNKNOWN, kFeatureLevels.data(),
                          static_cast<UINT>(kFeatureLevels.size()))) {
    feature_level_9_3_fallback_ = true;
    if (!TryCreateD3DDevice(D3D_DRIVER_TYPE_UNKNOWN, kFeatureLevel9_3.data(),
                            static_cast<UINT>(kFeatureLevel9_3.size()))) {
      adapter_.Reset();
      using_warp_ = true;
      feature_level_9_3_fallback_ = false;
      if (!TryCreateD3DDevice(D3D_DRIVER_TYPE_WARP, kFeatureLevels.data(),
                              static_cast<UINT>(kFeatureLevels.size()))) {
        throw std::runtime_error(
            "D3D11 hardware, feature-level 9_3, and WARP creation failed");
      }
      ComPtr<IDXGIDevice> dxgi_device;
      CheckHResult(d3d_11_device_.As(&dxgi_device),
                   "ID3D11Device::QueryInterface(IDXGIDevice)");
      CheckHResult(dxgi_device->GetAdapter(&adapter_),
                   "IDXGIDevice::GetAdapter");
    }
  }

  DXGI_ADAPTER_DESC adapter_description{};
  CheckHResult(adapter_->GetDesc(&adapter_description),
               "IDXGIAdapter::GetDesc");
  std::wcout << L"ANGLESurfaceManager: adapter="
             << adapter_description.Description
             << (using_warp_ ? L" backend=WARP" : L" backend=D3D11")
             << std::endl;
  std::cout << "ANGLESurfaceManager: Direct3D feature level="
            << (static_cast<unsigned int>(feature_level_) >> 12) << "_"
            << ((static_cast<unsigned int>(feature_level_) >> 8) & 0xF)
            << std::endl;

  ComPtr<IDXGIDevice> dxgi_device;
  CheckHResult(d3d_11_device_.As(&dxgi_device),
               "ID3D11Device::QueryInterface(IDXGIDevice)");
  CheckHResult(dxgi_device->SetGPUThreadPriority(5),
               "IDXGIDevice::SetGPUThreadPriority");
}

void ANGLESurfaceManager::CreateD3DTextures() {
  D3D11_TEXTURE2D_DESC description{};
  description.Width = static_cast<UINT>(width_);
  description.Height = static_cast<UINT>(height_);
  description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  description.MipLevels = 1;
  description.ArraySize = 1;
  description.SampleDesc.Count = 1;
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  description.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  CheckHResult(d3d_11_device_->CreateTexture2D(&description, nullptr,
                                               &internal_d3d_11_texture_2d_),
               "ID3D11Device::CreateTexture2D(internal)");
  ComPtr<IDXGIResource> internal_resource;
  CheckHResult(internal_d3d_11_texture_2d_.As(&internal_resource),
               "ID3D11Texture2D::QueryInterface(internal IDXGIResource)");
  CheckHResult(internal_resource->GetSharedHandle(&internal_handle_),
               "IDXGIResource::GetSharedHandle(internal)");

  CheckHResult(d3d_11_device_->CreateTexture2D(&description, nullptr,
                                               &d3d_11_texture_2d_),
               "ID3D11Device::CreateTexture2D(external)");
  ComPtr<IDXGIResource> external_resource;
  CheckHResult(d3d_11_texture_2d_.As(&external_resource),
               "ID3D11Texture2D::QueryInterface(external IDXGIResource)");
  CheckHResult(external_resource->GetSharedHandle(&handle_),
               "IDXGIResource::GetSharedHandle(external)");
  if (internal_handle_ == nullptr || handle_ == nullptr) {
    throw std::runtime_error("D3D11 shared texture returned a null handle");
  }
}

void ANGLESurfaceManager::CreateEGLDisplay() {
  auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
      eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (get_platform_display == nullptr) {
    throw std::runtime_error("eglGetPlatformDisplayEXT is unavailable");
  }

  DXGI_ADAPTER_DESC description{};
  CheckHResult(adapter_->GetDesc(&description), "IDXGIAdapter::GetDesc");
  std::vector<EGLint> attributes = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
  };
  if (using_warp_) {
    attributes.insert(attributes.end(),
                      {EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
                       EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D_WARP_ANGLE});
  } else {
    attributes.insert(attributes.end(),
                      {EGL_PLATFORM_ANGLE_D3D_LUID_HIGH_ANGLE,
                       static_cast<EGLint>(description.AdapterLuid.HighPart),
                       EGL_PLATFORM_ANGLE_D3D_LUID_LOW_ANGLE,
                       static_cast<EGLint>(description.AdapterLuid.LowPart)});
  }
  if (feature_level_9_3_fallback_) {
    attributes.insert(attributes.end(),
                      {EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE, 9,
                       EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE, 3});
  }
  attributes.insert(
      attributes.end(),
      {EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE, EGL_NONE});

  display_ = get_platform_display(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY,
                                  attributes.data());
  if (display_ == EGL_NO_DISPLAY) {
    throw std::runtime_error(
        "eglGetPlatformDisplayEXT returned EGL_NO_DISPLAY");
  }
  CheckEGL(eglInitialize(display_, nullptr, nullptr), "eglInitialize");
  CheckEGL(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
}

void ANGLESurfaceManager::CreateEGLContext() {
  const EGLint configuration_attributes[] = {
      EGL_SURFACE_TYPE,
      EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE,
      8,
      EGL_GREEN_SIZE,
      8,
      EGL_BLUE_SIZE,
      8,
      EGL_ALPHA_SIZE,
      8,
      EGL_DEPTH_SIZE,
      8,
      EGL_STENCIL_SIZE,
      8,
      EGL_NONE,
  };
  EGLint count = 0;
  CheckEGL(
      eglChooseConfig(display_, configuration_attributes, &config_, 1, &count),
      "eglChooseConfig");
  if (count != 1 || config_ == nullptr) {
    throw std::runtime_error("No suitable EGL configuration was found");
  }
  const EGLint context_attributes[] = {
      EGL_CONTEXT_CLIENT_VERSION,
      2,
      EGL_NONE,
  };
  context_ =
      eglCreateContext(display_, config_, EGL_NO_CONTEXT, context_attributes);
  if (context_ == EGL_NO_CONTEXT) {
    throw std::runtime_error("eglCreateContext failed with EGL error " +
                             Hex(static_cast<unsigned long>(eglGetError())));
  }
}

void ANGLESurfaceManager::CreateEGLSurface() {
  const EGLint buffer_attributes[] = {
      EGL_WIDTH,          width_,         EGL_HEIGHT,         height_,
      EGL_TEXTURE_TARGET, EGL_TEXTURE_2D, EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
      EGL_NONE,
  };
  surface_ = eglCreatePbufferFromClientBuffer(
      display_, EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE,
      reinterpret_cast<EGLClientBuffer>(internal_handle_), config_,
      buffer_attributes);
  if (surface_ == EGL_NO_SURFACE) {
    throw std::runtime_error(
        "eglCreatePbufferFromClientBuffer failed with EGL error " +
        Hex(static_cast<unsigned long>(eglGetError())));
  }
}

void ANGLESurfaceManager::DestroyEGLSurface() {
  if (surface_ == EGL_NO_SURFACE) {
    return;
  }
  CheckEGL(eglDestroySurface(display_, surface_), "eglDestroySurface");
  surface_ = EGL_NO_SURFACE;
}

void ANGLESurfaceManager::MakeCurrent(bool value) {
  CheckEGL(value ? eglMakeCurrent(display_, surface_, surface_, context_)
                 : eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                  EGL_NO_CONTEXT),
           value ? "eglMakeCurrent" : "eglMakeCurrent(clear)");
}

void ANGLESurfaceManager::CleanUp() noexcept {
  if (display_ != EGL_NO_DISPLAY) {
    LogEGLFailure(eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                 EGL_NO_CONTEXT),
                  "eglMakeCurrent(clear)");
    if (surface_ != EGL_NO_SURFACE) {
      LogEGLFailure(eglDestroySurface(display_, surface_), "eglDestroySurface");
      surface_ = EGL_NO_SURFACE;
    }
    if (context_ != EGL_NO_CONTEXT) {
      LogEGLFailure(eglDestroyContext(display_, context_), "eglDestroyContext");
      context_ = EGL_NO_CONTEXT;
    }
    LogEGLFailure(eglTerminate(display_), "eglTerminate");
    display_ = EGL_NO_DISPLAY;
  }
  config_ = nullptr;
  internal_handle_ = nullptr;
  handle_ = nullptr;
  internal_d3d_11_texture_2d_.Reset();
  d3d_11_texture_2d_.Reset();
  d3d_11_device_context_.Reset();
  d3d_11_device_.Reset();
  adapter_.Reset();
  dxgi_factory_.Reset();
}
