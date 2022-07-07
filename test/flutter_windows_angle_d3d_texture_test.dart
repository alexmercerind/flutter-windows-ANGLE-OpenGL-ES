import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_windows_angle_d3d_texture/flutter_windows_angle_d3d_texture.dart';
import 'package:flutter_windows_angle_d3d_texture/flutter_windows_angle_d3d_texture_platform_interface.dart';
import 'package:flutter_windows_angle_d3d_texture/flutter_windows_angle_d3d_texture_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockFlutterWindowsAngleD3dTexturePlatform 
    with MockPlatformInterfaceMixin
    implements FlutterWindowsAngleD3dTexturePlatform {

  @override
  Future<String?> getPlatformVersion() => Future.value('42');
}

void main() {
  final FlutterWindowsAngleD3dTexturePlatform initialPlatform = FlutterWindowsAngleD3dTexturePlatform.instance;

  test('$MethodChannelFlutterWindowsAngleD3dTexture is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelFlutterWindowsAngleD3dTexture>());
  });

  test('getPlatformVersion', () async {
    FlutterWindowsAngleD3dTexture flutterWindowsAngleD3dTexturePlugin = FlutterWindowsAngleD3dTexture();
    MockFlutterWindowsAngleD3dTexturePlatform fakePlatform = MockFlutterWindowsAngleD3dTexturePlatform();
    FlutterWindowsAngleD3dTexturePlatform.instance = fakePlatform;
  
    expect(await flutterWindowsAngleD3dTexturePlugin.getPlatformVersion(), '42');
  });
}
