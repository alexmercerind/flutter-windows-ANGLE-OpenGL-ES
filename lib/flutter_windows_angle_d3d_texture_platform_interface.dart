import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'flutter_windows_angle_d3d_texture_method_channel.dart';

abstract class FlutterWindowsAngleD3dTexturePlatform extends PlatformInterface {
  /// Constructs a FlutterWindowsAngleD3dTexturePlatform.
  FlutterWindowsAngleD3dTexturePlatform() : super(token: _token);

  static final Object _token = Object();

  static FlutterWindowsAngleD3dTexturePlatform _instance = MethodChannelFlutterWindowsAngleD3dTexture();

  /// The default instance of [FlutterWindowsAngleD3dTexturePlatform] to use.
  ///
  /// Defaults to [MethodChannelFlutterWindowsAngleD3dTexture].
  static FlutterWindowsAngleD3dTexturePlatform get instance => _instance;
  
  /// Platform-specific implementations should set this with their own
  /// platform-specific class that extends [FlutterWindowsAngleD3dTexturePlatform] when
  /// they register themselves.
  static set instance(FlutterWindowsAngleD3dTexturePlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<String?> getPlatformVersion() {
    throw UnimplementedError('platformVersion() has not been implemented.');
  }
}
