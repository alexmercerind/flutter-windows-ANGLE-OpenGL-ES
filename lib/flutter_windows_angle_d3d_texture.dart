
import 'flutter_windows_angle_d3d_texture_platform_interface.dart';

class FlutterWindowsAngleD3dTexture {
  Future<String?> getPlatformVersion() {
    return FlutterWindowsAngleD3dTexturePlatform.instance.getPlatformVersion();
  }
}
