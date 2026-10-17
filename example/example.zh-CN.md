# gsct_decoder 示例

[English](example.md) | 简体中文

从 HarmonyOS Remy 3D 影像中提取并解码 3DGS 数据的最小用法。

## 1. 探测码流

```dart
import 'package:gsct_decoder/gsct_decoder.dart';

final info = GsctDecoder.probe('/data/.../stream.gsct');
if (info != null) {
  print('${info.numPoints} 个高斯点，SH 阶数 ${info.shDegree}');
}
```

文件路径来自 Remy MP4 内 GLB 的 `UWA_primitive_3DGS_compression`
扩展所指向的 bufferView。

## 2. 解码为 PLY

```dart
await GsctDecoder.decodeToPly(
  inputPath: '/data/.../stream.gsct',
  outputPath: '/data/.../model.ply',
);
```

解码在独立 isolate 中执行（510600 点约需数秒），失败抛 `GsctException`。
输出为 `binary_little_endian` PLY（含完整 SH），可被 gsplat.js 直接加载。

> 注：本插件只负责码流解码，不含 MP4 拆包与渲染。完整链路需要宿主应用自行
> 从 Remy MP4 内 GLB 的 `UWA_primitive_3DGS_compression` 扩展定位 bufferView、
> 切出码流文件后调用本插件。
