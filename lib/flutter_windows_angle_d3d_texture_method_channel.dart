import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'flutter_windows_angle_d3d_texture_platform_interface.dart';

/// An implementation of [FlutterWindowsAngleD3dTexturePlatform] that uses method channels.
class MethodChannelFlutterWindowsAngleD3dTexture extends FlutterWindowsAngleD3dTexturePlatform {
  /// The method channel used to interact with the native platform.
  @visibleForTesting
  final methodChannel = const MethodChannel('flutter_windows_angle_d3d_texture');

  @override
  Future<String?> getPlatformVersion() async {
    final version = await methodChannel.invokeMethod<String>('getPlatformVersion');
    return version;
  }
}
