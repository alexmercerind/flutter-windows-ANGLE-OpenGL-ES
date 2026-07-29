#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Microsoft::WRL::ComPtr;

template <typename T>
T LoadRequired(HMODULE module, const char* name) {
  auto address = reinterpret_cast<T>(GetProcAddress(module, name));
  if (address == nullptr) {
    throw std::runtime_error(std::string("Missing export: ") + name);
  }
  return address;
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

std::string Hex(unsigned long value) {
  constexpr char kDigits[] = "0123456789ABCDEF";
  std::string result = "0x00000000";
  for (int index = 0; index < 8; ++index) {
    result[9 - index] = kDigits[value & 0xF];
    value >>= 4;
  }
  return result;
}

void CheckHr(HRESULT result, const char* operation) {
  if (FAILED(result)) {
    throw std::runtime_error(std::string(operation) + " failed with HRESULT " +
                             Hex(static_cast<unsigned long>(result)));
  }
}

struct Api {
  HMODULE egl_module = nullptr;
  HMODULE gles_module = nullptr;
  PFNEGLGETPROCADDRESSPROC egl_get_proc_address = nullptr;
  PFNEGLGETPLATFORMDISPLAYEXTPROC egl_get_platform_display = nullptr;
  PFNEGLINITIALIZEPROC egl_initialize = nullptr;
  PFNEGLTERMINATEPROC egl_terminate = nullptr;
  PFNEGLCHOOSECONFIGPROC egl_choose_config = nullptr;
  PFNEGLBINDAPIPROC egl_bind_api = nullptr;
  PFNEGLCREATEPBUFFERSURFACEPROC egl_create_pbuffer_surface = nullptr;
  PFNEGLCREATEPBUFFERFROMCLIENTBUFFERPROC
  egl_create_pbuffer_from_client_buffer = nullptr;
  PFNEGLDESTROYSURFACEPROC egl_destroy_surface = nullptr;
  PFNEGLCREATECONTEXTPROC egl_create_context = nullptr;
  PFNEGLDESTROYCONTEXTPROC egl_destroy_context = nullptr;
  PFNEGLMAKECURRENTPROC egl_make_current = nullptr;
  PFNEGLGETERRORPROC egl_get_error = nullptr;
  PFNGLCLEARCOLORPROC gl_clear_color = nullptr;
  PFNGLCLEARPROC gl_clear = nullptr;
  PFNGLFINISHPROC gl_finish = nullptr;
  PFNGLGETERRORPROC gl_get_error = nullptr;
  PFNGLGETSTRINGPROC gl_get_string = nullptr;
  PFNGLREADPIXELSPROC gl_read_pixels = nullptr;

  Api() {
    egl_module = LoadLibraryW(L"libEGL.dll");
    gles_module = LoadLibraryW(L"libGLESv2.dll");
    if (egl_module == nullptr || gles_module == nullptr) {
      throw std::runtime_error("Unable to load libEGL.dll and libGLESv2.dll");
    }
    egl_get_proc_address =
        LoadRequired<PFNEGLGETPROCADDRESSPROC>(egl_module, "eglGetProcAddress");
    egl_get_platform_display =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            egl_get_proc_address("eglGetPlatformDisplayEXT"));
    if (egl_get_platform_display == nullptr) {
      throw std::runtime_error("Missing eglGetPlatformDisplayEXT");
    }
    egl_initialize =
        LoadRequired<PFNEGLINITIALIZEPROC>(egl_module, "eglInitialize");
    egl_terminate =
        LoadRequired<PFNEGLTERMINATEPROC>(egl_module, "eglTerminate");
    egl_choose_config =
        LoadRequired<PFNEGLCHOOSECONFIGPROC>(egl_module, "eglChooseConfig");
    egl_bind_api = LoadRequired<PFNEGLBINDAPIPROC>(egl_module, "eglBindAPI");
    egl_create_pbuffer_surface = LoadRequired<PFNEGLCREATEPBUFFERSURFACEPROC>(
        egl_module, "eglCreatePbufferSurface");
    egl_create_pbuffer_from_client_buffer =
        LoadRequired<PFNEGLCREATEPBUFFERFROMCLIENTBUFFERPROC>(
            egl_module, "eglCreatePbufferFromClientBuffer");
    egl_destroy_surface =
        LoadRequired<PFNEGLDESTROYSURFACEPROC>(egl_module, "eglDestroySurface");
    egl_create_context =
        LoadRequired<PFNEGLCREATECONTEXTPROC>(egl_module, "eglCreateContext");
    egl_destroy_context =
        LoadRequired<PFNEGLDESTROYCONTEXTPROC>(egl_module, "eglDestroyContext");
    egl_make_current =
        LoadRequired<PFNEGLMAKECURRENTPROC>(egl_module, "eglMakeCurrent");
    egl_get_error = LoadRequired<PFNEGLGETERRORPROC>(egl_module, "eglGetError");
    gl_clear_color =
        LoadRequired<PFNGLCLEARCOLORPROC>(gles_module, "glClearColor");
    gl_clear = LoadRequired<PFNGLCLEARPROC>(gles_module, "glClear");
    gl_finish = LoadRequired<PFNGLFINISHPROC>(gles_module, "glFinish");
    gl_get_error = LoadRequired<PFNGLGETERRORPROC>(gles_module, "glGetError");
    gl_get_string =
        LoadRequired<PFNGLGETSTRINGPROC>(gles_module, "glGetString");
    gl_read_pixels =
        LoadRequired<PFNGLREADPIXELSPROC>(gles_module, "glReadPixels");
  }

  ~Api() {
    if (gles_module != nullptr) {
      FreeLibrary(gles_module);
    }
    if (egl_module != nullptr) {
      FreeLibrary(egl_module);
    }
  }
};

void CheckEgl(Api& api, EGLBoolean result, const char* operation) {
  if (result != EGL_TRUE) {
    const EGLint error = api.egl_get_error();
    throw std::runtime_error(std::string(operation) +
                             " failed with EGL error " +
                             Hex(static_cast<unsigned long>(error)));
  }
}

void CheckNoEglError(Api& api, const char* operation) {
  const EGLint error = api.egl_get_error();
  if (error != EGL_SUCCESS) {
    throw std::runtime_error(std::string(operation) + " left EGL error " +
                             Hex(static_cast<unsigned long>(error)));
  }
}

void CheckNoGlError(Api& api, const char* operation) {
  const GLenum error = api.gl_get_error();
  if (error != GL_NO_ERROR) {
    throw std::runtime_error(std::string(operation) + " left OpenGL ES error " +
                             Hex(static_cast<unsigned long>(error)));
  }
}

void CheckRgbaPixel(const std::array<unsigned char, 4>& pixel) {
  const std::array<int, 4> expected = {64, 128, 191, 255};
  for (size_t index = 0; index < pixel.size(); ++index) {
    if (std::abs(static_cast<int>(pixel[index]) - expected[index]) > 2) {
      throw std::runtime_error("Unexpected RGBA readback pixel");
    }
  }
}

void RenderClear(Api& api) {
  api.gl_clear_color(0.25f, 0.5f, 0.75f, 1.0f);
  api.gl_clear(GL_COLOR_BUFFER_BIT);
  api.gl_finish();
  CheckNoGlError(api, "glClear/glFinish");
}

void PrintRenderer(Api& api, const char* backend, bool require_swiftshader) {
  const auto* vendor =
      reinterpret_cast<const char*>(api.gl_get_string(GL_VENDOR));
  const auto* renderer =
      reinterpret_cast<const char*>(api.gl_get_string(GL_RENDERER));
  const auto* version =
      reinterpret_cast<const char*>(api.gl_get_string(GL_VERSION));
  if (vendor == nullptr || renderer == nullptr || version == nullptr) {
    throw std::runtime_error("glGetString returned null");
  }
  std::cout << "backend=" << backend << "\nvendor=" << vendor
            << "\nrenderer=" << renderer << "\nversion=" << version << "\n";
  if (require_swiftshader) {
    const std::string lower_renderer = ToLower(std::string(renderer));
    const std::string lower_vendor = ToLower(std::string(vendor));
    if (lower_renderer.find("swiftshader") == std::string::npos) {
      throw std::runtime_error(
          "Vulkan renderer does not identify itself as SwiftShader");
    }
    if (lower_vendor.find("google") == std::string::npos &&
        lower_vendor.find("swiftshader") == std::string::npos) {
      throw std::runtime_error(
          "Vulkan vendor does not identify the expected SwiftShader "
          "implementation");
    }
  }
}

EGLConfig ChooseConfig(Api& api, EGLDisplay display) {
  const EGLint config_attributes[] = {
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
      EGL_NONE,
  };
  EGLConfig config = nullptr;
  EGLint config_count = 0;
  CheckEgl(api,
           api.egl_choose_config(display, config_attributes, &config, 1,
                                 &config_count),
           "eglChooseConfig");
  if (config_count != 1 || config == nullptr) {
    throw std::runtime_error("No suitable EGL pbuffer config");
  }
  return config;
}

void RunBackend(Api& api, bool swiftshader) {
  const EGLint d3d11_display_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
      EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE,
      EGL_FALSE,
      EGL_NONE,
  };
  const EGLint swiftshader_display_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
      EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_DEVICE_TYPE_SWIFTSHADER_ANGLE,
      EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE,
      EGL_FALSE,
      EGL_NONE,
  };
  const EGLint* display_attributes =
      swiftshader ? swiftshader_display_attributes : d3d11_display_attributes;
  EGLDisplay display = api.egl_get_platform_display(
      EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, display_attributes);
  if (display == EGL_NO_DISPLAY) {
    throw std::runtime_error(
        "eglGetPlatformDisplayEXT returned EGL_NO_DISPLAY");
  }

  EGLint major = 0;
  EGLint minor = 0;
  CheckEgl(api, api.egl_initialize(display, &major, &minor), "eglInitialize");
  try {
    CheckEgl(api, api.egl_bind_api(EGL_OPENGL_ES_API), "eglBindAPI");
    EGLConfig config = ChooseConfig(api, display);
    const EGLint surface_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface surface =
        api.egl_create_pbuffer_surface(display, config, surface_attributes);
    if (surface == EGL_NO_SURFACE) {
      throw std::runtime_error("eglCreatePbufferSurface failed");
    }
    try {
      const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                           EGL_NONE};
      EGLContext context = api.egl_create_context(
          display, config, EGL_NO_CONTEXT, context_attributes);
      if (context == EGL_NO_CONTEXT) {
        throw std::runtime_error("eglCreateContext failed");
      }
      try {
        CheckEgl(api, api.egl_make_current(display, surface, surface, context),
                 "eglMakeCurrent");
        CheckNoEglError(api, "EGL context creation");
        PrintRenderer(api, swiftshader ? "swiftshader" : "d3d11", swiftshader);
        RenderClear(api);
        std::array<unsigned char, 4> pixel{};
        api.gl_read_pixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        CheckNoGlError(api, "glReadPixels");
        CheckRgbaPixel(pixel);
        std::cout << "pixel=" << static_cast<int>(pixel[0]) << ","
                  << static_cast<int>(pixel[1]) << ","
                  << static_cast<int>(pixel[2]) << ","
                  << static_cast<int>(pixel[3]) << "\nresult=passed\n";
        CheckEgl(api,
                 api.egl_make_current(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                      EGL_NO_CONTEXT),
                 "eglMakeCurrent(clear)");
      } catch (...) {
        api.egl_destroy_context(display, context);
        throw;
      }
      CheckEgl(api, api.egl_destroy_context(display, context),
               "eglDestroyContext");
    } catch (...) {
      api.egl_destroy_surface(display, surface);
      throw;
    }
    CheckEgl(api, api.egl_destroy_surface(display, surface),
             "eglDestroySurface");
  } catch (...) {
    api.egl_terminate(display);
    throw;
  }
  CheckEgl(api, api.egl_terminate(display), "eglTerminate");
  CheckNoEglError(api, "EGL cleanup");
}

struct D3dInterop {
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11Texture2D> render_texture;
  ComPtr<ID3D11Texture2D> staging_texture;
  HANDLE shared_handle = nullptr;
  LUID adapter_luid{};
};

D3dInterop CreateD3dInterop(bool warp) {
  D3dInterop result;
  constexpr std::array<D3D_FEATURE_LEVEL, 4> kFeatureLevels = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
  };
  if (!warp) {
    ComPtr<IDXGIFactory1> factory;
    CheckHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    CheckHr(factory->EnumAdapters1(0, &result.adapter),
            "IDXGIFactory1::EnumAdapters1");
    DXGI_ADAPTER_DESC1 description{};
    CheckHr(result.adapter->GetDesc1(&description), "IDXGIAdapter1::GetDesc1");
    result.adapter_luid = description.AdapterLuid;
  }

  D3D_FEATURE_LEVEL selected_level{};
  CheckHr(D3D11CreateDevice(
              warp ? nullptr : result.adapter.Get(),
              warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_UNKNOWN, nullptr,
              D3D11_CREATE_DEVICE_BGRA_SUPPORT, kFeatureLevels.data(),
              static_cast<UINT>(kFeatureLevels.size()), D3D11_SDK_VERSION,
              &result.device, &selected_level, &result.context),
          "D3D11CreateDevice");

  D3D11_TEXTURE2D_DESC texture_description{};
  texture_description.Width = 4;
  texture_description.Height = 4;
  texture_description.MipLevels = 1;
  texture_description.ArraySize = 1;
  texture_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  texture_description.SampleDesc.Count = 1;
  texture_description.Usage = D3D11_USAGE_DEFAULT;
  texture_description.BindFlags =
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  texture_description.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
  CheckHr(result.device->CreateTexture2D(&texture_description, nullptr,
                                         &result.render_texture),
          "ID3D11Device::CreateTexture2D(shared)");

  ComPtr<IDXGIResource> resource;
  CheckHr(result.render_texture.As(&resource),
          "ID3D11Texture2D::QueryInterface(IDXGIResource)");
  CheckHr(resource->GetSharedHandle(&result.shared_handle),
          "IDXGIResource::GetSharedHandle");
  if (result.shared_handle == nullptr) {
    throw std::runtime_error("D3D11 shared texture returned a null handle");
  }

  texture_description.Usage = D3D11_USAGE_STAGING;
  texture_description.BindFlags = 0;
  texture_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  texture_description.MiscFlags = 0;
  CheckHr(result.device->CreateTexture2D(&texture_description, nullptr,
                                         &result.staging_texture),
          "ID3D11Device::CreateTexture2D(staging)");
  return result;
}

void RunInterop(Api& api, bool warp) {
  D3dInterop d3d = CreateD3dInterop(warp);
  const EGLint hardware_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
      EGL_PLATFORM_ANGLE_D3D_LUID_HIGH_ANGLE,
      static_cast<EGLint>(d3d.adapter_luid.HighPart),
      EGL_PLATFORM_ANGLE_D3D_LUID_LOW_ANGLE,
      static_cast<EGLint>(d3d.adapter_luid.LowPart),
      EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE,
      EGL_FALSE,
      EGL_NONE,
  };
  const EGLint warp_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
      EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D_WARP_ANGLE,
      EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE,
      EGL_FALSE,
      EGL_NONE,
  };
  EGLDisplay display = api.egl_get_platform_display(
      EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY,
      warp ? warp_attributes : hardware_attributes);
  if (display == EGL_NO_DISPLAY) {
    throw std::runtime_error(
        "interop eglGetPlatformDisplayEXT returned EGL_NO_DISPLAY");
  }

  EGLint major = 0;
  EGLint minor = 0;
  CheckEgl(api, api.egl_initialize(display, &major, &minor), "eglInitialize");
  try {
    CheckEgl(api, api.egl_bind_api(EGL_OPENGL_ES_API), "eglBindAPI");
    EGLConfig config = ChooseConfig(api, display);
    const EGLint surface_attributes[] = {
        EGL_WIDTH,          4,
        EGL_HEIGHT,         4,
        EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
        EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
        EGL_NONE,
    };
    EGLSurface surface = api.egl_create_pbuffer_from_client_buffer(
        display, EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE,
        reinterpret_cast<EGLClientBuffer>(d3d.shared_handle), config,
        surface_attributes);
    if (surface == EGL_NO_SURFACE) {
      throw std::runtime_error(
          "eglCreatePbufferFromClientBuffer(shared D3D11 texture) failed");
    }
    try {
      const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                           EGL_NONE};
      EGLContext context = api.egl_create_context(
          display, config, EGL_NO_CONTEXT, context_attributes);
      if (context == EGL_NO_CONTEXT) {
        throw std::runtime_error("interop eglCreateContext failed");
      }
      try {
        CheckEgl(api, api.egl_make_current(display, surface, surface, context),
                 "interop eglMakeCurrent");
        PrintRenderer(api, warp ? "interop-warp" : "interop-d3d11", false);
        RenderClear(api);
        CheckEgl(api,
                 api.egl_make_current(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                      EGL_NO_CONTEXT),
                 "interop eglMakeCurrent(clear)");
      } catch (...) {
        api.egl_destroy_context(display, context);
        throw;
      }
      CheckEgl(api, api.egl_destroy_context(display, context),
               "interop eglDestroyContext");
    } catch (...) {
      api.egl_destroy_surface(display, surface);
      throw;
    }
    CheckEgl(api, api.egl_destroy_surface(display, surface),
             "interop eglDestroySurface");
  } catch (...) {
    api.egl_terminate(display);
    throw;
  }
  CheckEgl(api, api.egl_terminate(display), "interop eglTerminate");

  d3d.context->CopyResource(d3d.staging_texture.Get(),
                            d3d.render_texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  CheckHr(d3d.context->Map(d3d.staging_texture.Get(), 0, D3D11_MAP_READ, 0,
                           &mapped),
          "ID3D11DeviceContext::Map");
  const auto* pixel = static_cast<const unsigned char*>(mapped.pData);
  const std::array<int, 4> expected_bgra = {191, 128, 64, 255};
  bool matches = true;
  for (size_t index = 0; index < expected_bgra.size(); ++index) {
    if (std::abs(static_cast<int>(pixel[index]) - expected_bgra[index]) > 2) {
      matches = false;
    }
  }
  const std::array<unsigned char, 4> actual = {pixel[0], pixel[1], pixel[2],
                                               pixel[3]};
  d3d.context->Unmap(d3d.staging_texture.Get(), 0);
  if (!matches) {
    throw std::runtime_error("Unexpected D3D11 BGRA staging readback pixel");
  }
  std::cout << "pixel_bgra=" << static_cast<int>(actual[0]) << ","
            << static_cast<int>(actual[1]) << "," << static_cast<int>(actual[2])
            << "," << static_cast<int>(actual[3]) << "\nresult=passed\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    bool run_d3d11 = argc == 1;
    bool run_swiftshader = argc == 1;
    bool run_interop_d3d11 = argc == 1;
    bool run_interop_warp = argc == 1;
    if (argc == 2) {
      const std::string mode = argv[1];
      run_d3d11 = mode == "--d3d11";
      run_swiftshader = mode == "--swiftshader";
      run_interop_d3d11 = mode == "--interop-d3d11";
      run_interop_warp = mode == "--interop-warp";
      if (!run_d3d11 && !run_swiftshader && !run_interop_d3d11 &&
          !run_interop_warp) {
        throw std::runtime_error(
            "Expected --d3d11, --swiftshader, --interop-d3d11, or "
            "--interop-warp");
      }
    } else if (argc != 1) {
      throw std::runtime_error(
          "Expected zero arguments or one backend argument");
    }
    Api api;
    if (run_d3d11) {
      RunBackend(api, false);
    }
    if (run_swiftshader) {
      RunBackend(api, true);
    }
    if (run_interop_d3d11) {
      RunInterop(api, false);
    }
    if (run_interop_warp) {
      RunInterop(api, true);
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "result=failed\nerror=" << error.what() << "\n";
    return EXIT_FAILURE;
  }
}
