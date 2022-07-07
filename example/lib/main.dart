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
  int? textureId;

  @override
  void initState() {
    super.initState();
    initPlatformState();
  }

  Future<void> initPlatformState() async {
    const channel =
        MethodChannel('com.alexmercerind/flutter_windows_angle_d3d_texture');
    await channel.invokeMethod('CreateID3D11Device');
    await channel.invokeMethod('ID3D11Device::CreateTexture2D');
    await channel.invokeMethod('eglCreatePbufferFromClientBuffer');
    await channel.invokeMethod('eglBindTexImage');
    await channel.invokeMethod('glDrawArrays');
    textureId = await channel
        .invokeMethod('flutter::TextureRegistrar::RegisterTexture');
    setState(() {});
    debugPrint(textureId.toString());
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text('flutter_windows_angle_d3d_texture'),
        ),
        body: Center(
          child: textureId == null
              ? null
              : Texture(
                  textureId: textureId!,
                  filterQuality: FilterQuality.high,
                ),
        ),
      ),
    );
  }
}
