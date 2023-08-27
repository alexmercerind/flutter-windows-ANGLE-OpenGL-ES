#include "flutter_windows_angle_opengl_es_plugin.h"

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <flutter/texture_registrar.h>

namespace flutter_windows_angle_opengl_es {

GLuint CompileShader(GLenum type, const std::string& source) {
  auto shader = glCreateShader(type);
  const char* s[1] = {source.c_str()};
  glShaderSource(shader, 1, s, NULL);
  glCompileShader(shader);
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
  glLinkProgram(program);
  return program;
}

void FlutterWindowsANGLEOpenGLESPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto plugin = std::make_unique<FlutterWindowsANGLEOpenGLESPlugin>(
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

FlutterWindowsANGLEOpenGLESPlugin::FlutterWindowsANGLEOpenGLESPlugin(
    flutter::PluginRegistrarWindows* registrar,
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel,
    flutter::TextureRegistrar* texture_registrar)
    : registrar_(registrar),
      channel_(std::move(channel)),
      texture_registrar_(texture_registrar) {}

FlutterWindowsANGLEOpenGLESPlugin::~FlutterWindowsANGLEOpenGLESPlugin() {}

void FlutterWindowsANGLEOpenGLESPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (method_call.method_name().compare("render") == 0) {
    constexpr auto w = 1920;
    constexpr auto h = 1080;
    // ---------------------------------------------
    surface_manager_ = std::make_unique<ANGLESurfaceManager>(w, h);
    // ---------------------------------------------
    texture_ = std::make_unique<FlutterDesktopGpuSurfaceDescriptor>();
    texture_->struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor);
    texture_->handle = surface_manager_->handle();
    texture_->width = texture_->visible_width = w;
    texture_->height = texture_->visible_height = h;
    texture_->release_context = nullptr;
    texture_->release_callback = [](void* release_context) {};
    texture_->format = kFlutterDesktopPixelFormatBGRA8888;
    // ---------------------------------------------
    texture_variant_ =
        std::make_unique<flutter::TextureVariant>(flutter::GpuSurfaceTexture(
            kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle,
            [&](auto, auto) { return texture_.get(); }));
    // ---------------------------------------------
    surface_manager_->Draw([&]() {
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
    });
    surface_manager_->Read();
    // ---------------------------------------------
    auto id = texture_registrar_->RegisterTexture(texture_variant_.get());
    texture_registrar_->MarkTextureFrameAvailable(id);
    result->Success(flutter::EncodableValue(id));
  } else {
    result->NotImplemented();
  }
}

}  // namespace flutter_windows_angle_opengl_es
