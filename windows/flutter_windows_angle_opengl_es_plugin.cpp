#include "flutter_windows_angle_opengl_es_plugin.h"

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <flutter/texture_registrar.h>

#include <iostream>
#include <stdexcept>

namespace flutter_windows_angle_opengl_es {

struct FlutterWindowsAngleOpenglEsPlugin::RenderedTexture {
  std::unique_ptr<AngleSurfaceManager> surface;
  FlutterDesktopGpuSurfaceDescriptor descriptor{};
  std::unique_ptr<flutter::TextureVariant> texture;
  int64_t id = -1;
};

GLuint CompileShader(GLenum type, const std::string& source) {
  auto shader = glCreateShader(type);
  const char* s[1] = {source.c_str()};
  glShaderSource(shader, 1, s, NULL);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint CompileProgram(const std::string& vertex_shader_source,
                      const std::string& fragment_shader_source) {
  auto program = glCreateProgram();

  auto vs = CompileShader(GL_VERTEX_SHADER, vertex_shader_source);
  auto fs = CompileShader(GL_FRAGMENT_SHADER, fragment_shader_source);
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
  glBindAttribLocation(program, 0, "vPosition");
  glLinkProgram(program);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

void FlutterWindowsAngleOpenglEsPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto plugin = std::make_unique<FlutterWindowsAngleOpenglEsPlugin>(
      registrar,
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "flutter-windows-ANGLE-OpenGL-ES",
          &flutter::StandardMethodCodec::GetInstance()),
      registrar->texture_registrar());
  plugin->channel()->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });
  registrar->AddPlugin(std::move(plugin));
}

FlutterWindowsAngleOpenglEsPlugin::FlutterWindowsAngleOpenglEsPlugin(
    flutter::PluginRegistrarWindows* registrar,
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel,
    flutter::TextureRegistrar* texture_registrar)
    : registrar_(registrar),
      channel_(std::move(channel)),
      texture_registrar_(texture_registrar) {}

FlutterWindowsAngleOpenglEsPlugin::~FlutterWindowsAngleOpenglEsPlugin() {
  if (rendered_texture_) {
    auto texture = std::move(rendered_texture_);
    const auto id = texture->id;
    // Flutter may still invoke the texture callback until unregistration ends.
    // Keep its descriptor and D3D/EGL resources alive without capturing `this`.
    texture_registrar_->UnregisterTexture(id, [texture]() {});
  }
}

void FlutterWindowsAngleOpenglEsPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (method_call.method_name().compare("render") == 0) {
    // The example renders one immutable image. Reuse its registered texture
    // rather than invalidating a handle Flutter may still be consuming.
    if (rendered_texture_) {
      result->Success(flutter::EncodableValue(rendered_texture_->id));
      return;
    }
    try {
      constexpr auto w = 1920;
      constexpr auto h = 1080;
      auto texture = std::make_shared<RenderedTexture>();
      texture->surface = std::make_unique<AngleSurfaceManager>(w, h);
      auto& descriptor = texture->descriptor;
      descriptor.struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor);
      descriptor.handle = texture->surface->handle();
      descriptor.width = descriptor.visible_width = w;
      descriptor.height = descriptor.visible_height = h;
      descriptor.format = kFlutterDesktopPixelFormatBGRA8888;
      texture->texture =
          std::make_unique<flutter::TextureVariant>(flutter::GpuSurfaceTexture(
              kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle,
              [state = texture.get()](auto, auto) {
                return &state->descriptor;
              }));
      texture->surface->Draw([&]() {
        std::cout << glGetString(GL_VERSION) << std::endl;
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
        auto program = CompileProgram(kVertexShader, kFragmentShader);
        if (!program) {
          throw std::runtime_error(
              "Unable to compile or link triangle shaders.");
        }
        glEnableVertexAttribArray(0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        GLfloat vertices[] = {
            0.0f, -0.5f, 0.0f, -0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f,
        };
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(0, 0, w, h);
        glUseProgram(program);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisableVertexAttribArray(0);
        glUseProgram(0);
        glDeleteProgram(program);
      });
      texture->surface->Read();
      texture->id = texture_registrar_->RegisterTexture(texture->texture.get());
      if (texture->id < 0) {
        throw std::runtime_error("Unable to register Flutter texture.");
      }
      rendered_texture_ = std::move(texture);
      texture_registrar_->MarkTextureFrameAvailable(rendered_texture_->id);
      result->Success(flutter::EncodableValue(rendered_texture_->id));
    } catch (const std::exception& error) {
      result->Error("render_failed", error.what());
    }
  } else {
    result->NotImplemented();
  }
}

}  // namespace flutter_windows_angle_opengl_es
