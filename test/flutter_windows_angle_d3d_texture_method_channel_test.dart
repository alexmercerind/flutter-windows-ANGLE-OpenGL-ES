import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_windows_angle_d3d_texture/flutter_windows_angle_d3d_texture_method_channel.dart';

void main() {
  MethodChannelFlutterWindowsAngleD3dTexture platform = MethodChannelFlutterWindowsAngleD3dTexture();
  const MethodChannel channel = MethodChannel('flutter_windows_angle_d3d_texture');

  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    channel.setMockMethodCallHandler((MethodCall methodCall) async {
      return '42';
    });
  });

  tearDown(() {
    channel.setMockMethodCallHandler(null);
  });

  test('getPlatformVersion', () async {
    expect(await platform.getPlatformVersion(), '42');
  });
}
