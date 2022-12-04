import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({Key? key}) : super(key: key);

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  static const kChannelName = 'flutter-windows-ANGLE-OpenGL-Direct3D-Interop';
  static const kChannel = MethodChannel(kChannelName);

  int? id;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.waitUntilFirstFrameRasterized.then((_) {
      initPlatformState();
    });
  }

  Future<void> initPlatformState() async {
    id = await kChannel.invokeMethod('id');
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text(kChannelName),
        ),
        body: Center(
          child: id == null
              ? null
              : Texture(
                  textureId: id!,
                  filterQuality: FilterQuality.high,
                ),
        ),
      ),
    );
  }
}
