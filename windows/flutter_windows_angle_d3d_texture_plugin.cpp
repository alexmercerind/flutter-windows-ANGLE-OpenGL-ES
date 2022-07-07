#include "flutter_windows_angle_d3d_texture_plugin.h"

// Flutter
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <flutter/texture_registrar.h>
// Standard C++ STL
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#define STRING(s) #s

namespace flutter_windows_angle_d3d_texture {

constexpr auto kD3D11Texture2DWidth = 640;
constexpr auto kD3D11Texture2DHeight = 480;

GLuint CompileShader(GLenum type, const std::string& source) {
  GLuint shader = glCreateShader(type);
  const char* source_c_str[1] = {source.c_str()};
  glShaderSource(shader, 1, source_c_str, NULL);
  glCompileShader(shader);
  GLint result;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
  if (result == 0) {
    GLint size;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
    std::vector<GLchar> text(size);
    glGetShaderInfoLog(shader, (GLsizei)text.size(), NULL, text.data());
    auto error = std::string("Shader Compilation Failed: ");
    error += text.data();
    std::cerr << text.data() << std::endl;
  }
  return shader;
}

GLuint CompileProgram(const std::string& vsSource,
                      const std::string& fsSource) {
  GLuint program = glCreateProgram();
  if (program == 0) {
    std::cerr << "Program Creation Failed." << std::endl;
  }
  GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);
  if (vs == 0 || fs == 0) {
    glDeleteShader(fs);
    glDeleteShader(vs);
    glDeleteProgram(program);
    return 0;
  }
  glAttachShader(program, vs);
  glDeleteShader(vs);
  glAttachShader(program, fs);
  glDeleteShader(fs);
  glLinkProgram(program);
  GLint status;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == 0) {
    GLint size;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &size);
    std::vector<GLchar> text(size);
    glGetProgramInfoLog(program, (GLsizei)text.size(), NULL, text.data());
    auto error = std::string("Program Link Failed: ");
    error += text.data();
    std::cerr << text.data() << std::endl;
  }
  return program;
}

void FlutterWindowsAngleD3dTexturePlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto plugin = std::make_unique<FlutterWindowsAngleD3dTexturePlugin>(
      registrar,
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(),
          "com.alexmercerind/flutter_windows_angle_d3d_texture",
          &flutter::StandardMethodCodec::GetInstance()),
      registrar->texture_registrar());
  plugin->channel()->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });
  registrar->AddPlugin(std::move(plugin));
}

FlutterWindowsAngleD3dTexturePlugin::FlutterWindowsAngleD3dTexturePlugin(
    flutter::PluginRegistrarWindows* registrar,
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel,
    flutter::TextureRegistrar* texture_registrar)
    : registrar_(registrar),
      channel_(std::move(channel)),
      texture_registrar_(texture_registrar) {}

FlutterWindowsAngleD3dTexturePlugin::~FlutterWindowsAngleD3dTexturePlugin() {}

void FlutterWindowsAngleD3dTexturePlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (method_call.method_name().compare("CreateID3D11Device") == 0) {
    HRESULT hr = S_OK;
    if (FAILED(hr = ::D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                        D3D11_CREATE_DEVICE_VIDEO_SUPPORT, NULL,
                                        0, D3D11_SDK_VERSION, &d3d11_device_,
                                        NULL, &d3d11_device_context_))) {
      std::cerr << "D3D11CreateDevice FAILED." << std::endl;
    }
    result->Success(flutter::EncodableValue(nullptr));
  } else if (method_call.method_name().compare(
                 "ID3D11Device::CreateTexture2D") == 0) {
    D3D11_TEXTURE2D_DESC d3d11_texture2D_desc = {0};
    d3d11_texture2D_desc.Width = kD3D11Texture2DWidth;
    d3d11_texture2D_desc.Height = kD3D11Texture2DHeight;
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
    HRESULT hr = d3d11_device_->CreateTexture2D(&d3d11_texture2D_desc, nullptr,
                                                &d3d11_texture_2D_);
    if FAILED (hr) {
      std::cerr << "CreateTexture2D FAILED." << std::endl;
    }
    result->Success(flutter::EncodableValue(nullptr));
  } else if (method_call.method_name().compare(
                 "eglCreatePbufferFromClientBuffer") == 0) {
    ComPtr<IDXGIResource> dxgi_resource;
    HRESULT hr = d3d11_texture_2D_.As(&dxgi_resource);
    if FAILED (hr) {
      std::cerr << "Microsoft::WRL::ComPtr<ID3D11Texture2D>::As<IDXGIResource>"
                   " FAILED."
                << std::endl;
    }
    hr = dxgi_resource->GetSharedHandle(&shared_handle_);
    if FAILED (hr) {
      std::cerr
          << "Microsoft::WRL::ComPtr<IDXGIResource>::GetSharedHandle FAILED."
          << std::endl;
    }
    EGLint buffer_attributes[] = {EGL_WIDTH,
                                  kD3D11Texture2DWidth,
                                  EGL_HEIGHT,
                                  kD3D11Texture2DHeight,
                                  EGL_TEXTURE_TARGET,
                                  EGL_TEXTURE_2D,
                                  EGL_TEXTURE_FORMAT,
                                  EGL_TEXTURE_RGBA,
                                  EGL_NONE};
    const EGLint config_attributes[] = {EGL_RED_SIZE,   8, EGL_GREEN_SIZE,   8,
                                        EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE,   8,
                                        EGL_DEPTH_SIZE, 8, EGL_STENCIL_SIZE, 8,
                                        EGL_NONE};
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                         EGL_NONE};
    const EGLint default_display_attributes[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
        EGL_TRUE,
        EGL_NONE,
    };
    const EGLint fl9_3_display_attributes[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE,
        9,
        EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE,
        3,
        EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
        EGL_TRUE,
        EGL_NONE,
    };
    const EGLint warp_display_attributes[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
        EGL_TRUE,
        EGL_NONE,
    };
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    display_ =
        eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY,
                                 default_display_attributes);
    if (display_ == EGL_NO_DISPLAY) {
      std::cerr << "eglGetPlatformDisplayEXT FAILED." << std::endl;
    }
    if (eglInitialize(display_, NULL, NULL) == EGL_FALSE) {
      std::cerr << "eglInitialize FAILED." << std::endl;
      display_ = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE,
                                          EGL_DEFAULT_DISPLAY,
                                          fl9_3_display_attributes);
      if (display_ == EGL_NO_DISPLAY) {
        std::cerr << "eglGetPlatformDisplayEXT FAILED." << std::endl;
      }
      if (eglInitialize(display_, NULL, NULL) == EGL_FALSE) {
        std::cerr << "eglInitialize FAILED." << std::endl;
        display_ = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE,
                                            EGL_DEFAULT_DISPLAY,
                                            warp_display_attributes);
        if (display_ == EGL_NO_DISPLAY) {
          std::cerr << "eglGetPlatformDisplayEXT FAILED." << std::endl;
        }
        if (eglInitialize(display_, NULL, NULL) == EGL_FALSE) {
          std::cerr << "eglInitialize FAILED." << std::endl;
        }
      }
    }
    EGLint count = 0;
    if ((eglChooseConfig(display_, config_attributes, &config_, 1, &count) ==
         EGL_FALSE) ||
        (count == 0)) {
      std::cerr << "eglChooseConfig FAILED." << std::endl;
    }
    context_ =
        eglCreateContext(display_, config_, EGL_NO_CONTEXT, context_attributes);
    if (context_ == EGL_NO_CONTEXT) {
      std::cerr << "eglCreateContext FAILED." << std::endl;
    }
    surface_ = eglCreatePbufferFromClientBuffer(
        display_, EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE, shared_handle_,
        config_, buffer_attributes);
    if (surface_ == EGL_NO_SURFACE) {
      std::cerr << "eglCreatePbufferFromClientBuffer FAILED." << std::endl;
    }
    result->Success(flutter::EncodableValue(nullptr));
  } else if (method_call.method_name().compare("eglBindTexImage") == 0) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    eglBindTexImage(display_, surface_, EGL_BACK_BUFFER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    result->Success(flutter::EncodableValue(nullptr));
  } else if (method_call.method_name().compare(
                 "flutter::TextureRegistrar::RegisterTexture") == 0) {
    gpu_surface_descriptor_ =
        std::make_unique<FlutterDesktopGpuSurfaceDescriptor>();
    gpu_surface_descriptor_->struct_size =
        sizeof(FlutterDesktopGpuSurfaceDescriptor);
    gpu_surface_descriptor_->handle = shared_handle_;
    gpu_surface_descriptor_->width = gpu_surface_descriptor_->visible_width =
        kD3D11Texture2DWidth;
    gpu_surface_descriptor_->height = gpu_surface_descriptor_->visible_height =
        kD3D11Texture2DHeight;
    gpu_surface_descriptor_->release_context = nullptr;
    gpu_surface_descriptor_->release_callback = [](void* release_context) {};
    gpu_surface_descriptor_->format = kFlutterDesktopPixelFormatBGRA8888;
    gpu_surface_texture_ =
        std::make_unique<flutter::TextureVariant>(flutter::GpuSurfaceTexture(
            kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle,
            [&](size_t width,
                size_t height) -> const FlutterDesktopGpuSurfaceDescriptor* {
              return gpu_surface_descriptor_.get();
            }));
    texture_id_ =
        texture_registrar_->RegisterTexture(gpu_surface_texture_.get());
    // Whatever.
    std::thread([&]() {
      while (true) {
        std::cout << "texture_id_         : " << texture_id_ << std::endl;
        std::cout << "EGLSurface          : " << surface_ << std::endl;
        std::cout << "EGLDisplay          : " << display_ << std::endl;
        std::cout << "EGLContext          : " << context_ << std::endl;
        std::cout << "EGLConfig           : " << config_ << std::endl;
        std::cout << "HANDLE              : " << shared_handle_ << std::endl;
        std::cout << "ID3D11Device*       : " << d3d11_device_ << std::endl;
        std::cout << "ID3D11DeviceContext : " << d3d11_device_context_
                  << std::endl;
        std::cout << "ID3D11Texture2D*    : " << d3d11_texture_2D_.Get()
                  << std::endl;
        texture_registrar_->MarkTextureFrameAvailable(texture_id_);
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    }).detach();
    result->Success(flutter::EncodableValue(texture_id_));
  } else if (method_call.method_name().compare("glDrawArrays") == 0) {
    if (eglMakeCurrent(display_, surface_, surface_, context_) == 0) {
      std::cerr << "eglMakeCurrent FAILED." << std::endl;
    }
    constexpr char kVertexShader[] = R"(attribute vec4 vPosition;
    void main()
    {
        gl_Position = vPosition;
    })";
    constexpr char kFragmentShader[] = R"(precision mediump float;
    void main()
    {
        gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
    })";
    GLuint program = CompileProgram(kVertexShader, kFragmentShader);
    std::cout << program << std::endl;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    GLfloat vertices[] = {
        0.0f, 0.5f, 0.0f, -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f,
    };
    glViewport(0, 0, kD3D11Texture2DWidth, kD3D11Texture2DHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    result->Success(flutter::EncodableValue(nullptr));
  } else {
    result->NotImplemented();
  }
}

}  // namespace flutter_windows_angle_d3d_texture
