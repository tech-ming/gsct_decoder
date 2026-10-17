# gsct_decoder example

English | [简体中文](example.zh-CN.md)

Minimal usage for extracting and decoding 3DGS data from a HarmonyOS Remy 3D photo.

## 1. Probe the stream

```dart
import 'package:gsct_decoder/gsct_decoder.dart';

final info = GsctDecoder.probe('/data/.../stream.gsct');
if (info != null) {
  print('${info.numPoints} gaussians, SH degree ${info.shDegree}');
}
```

The file path comes from the bufferView referenced by the
`UWA_primitive_3DGS_compression` extension of the GLB embedded in the Remy MP4.

## 2. Decode to PLY

```dart
await GsctDecoder.decodeToPly(
  inputPath: '/data/.../stream.gsct',
  outputPath: '/data/.../model.ply',
);
```

Decoding runs in a separate isolate (a few seconds for 510,600 gaussians) and
throws `GsctException` on failure. The output is a `binary_little_endian` PLY
(with full SH) that gsplat.js can load directly.

> Note: this plugin only decodes the stream — it does not unpack the MP4 or
> render anything. A complete pipeline requires the host application to locate
> the bufferView via the GLB's `UWA_primitive_3DGS_compression` extension,
> extract the stream into a file, and then call this plugin.
