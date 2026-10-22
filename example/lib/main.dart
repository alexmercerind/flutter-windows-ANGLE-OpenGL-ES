import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  int? textureId;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.waitUntilFirstFrameRasterized.then((_) async {
      const channel = MethodChannel('flutter-windows-ANGLE-OpenGL-ES');
      if (!mounted) return;
      final id = await channel.invokeMethod<int>('render');
      if (!mounted) return;
      setState(() => textureId = id);
    });
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('flutter-windows-ANGLE-OpenGL-ES')),
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
