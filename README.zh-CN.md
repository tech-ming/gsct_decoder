# gsct_decoder

[English](README.md) | 简体中文

HarmonyOS 3D 影像（Remy）导出的 **gsct** 压缩码流解码器，输出标准 3DGS PLY。

## 背景

HarmonyOS「Remy」拍摄的 3D 影像导出为单个 `.mp4`，其中封装了预览视频与
3DGS 高斯泼溅模型。模型部分使用 UWA 私有压缩扩展
`UWA_primitive_3DGS_compression`，码流以 `gsct` 魔数开头，属性数据经
**zlib + 简单量化 + ASTC 纹理**编码。

该格式与 UWA T/UWA 050-2026 标准定稿后的 EGSC 格式（`UWA_gaussian_splatting_compression_EGSC`，
属性经视频编码压缩）**不是同一套编解码**：gsct 不需要视频解码器，
只需 zlib 与 ASTC 解码。

本包移植自 UWA 官方参考实现
[spatial-vivid-toolkit](https://gitcode.com/UHD-World-Association/spatial-vivid-toolkit)
中未被 CLI 暴露的 `gsdecoder.cpp`，封装为 Flutter FFI 插件。

## 用法

```dart
import 'package:gsct_decoder/gsct_decoder.dart';

// 探测（轻量，只解析头部）
final info = GsctDecoder.probe('/path/to/stream.gsct');
if (info != null) {
  print('${info.numPoints} 个高斯点，SH 阶数 ${info.shDegree}');
}

// 解码为 PLY（CPU 密集，内部走独立 isolate）
await GsctDecoder.decodeToPly(
  inputPath: '/path/to/stream.gsct',
  outputPath: '/path/to/output.ply',
);
```

输出的 PLY 为 `binary_little_endian`，字段与 3DGS 训练脚本导出的一致
（`x/y/z`、`nx/ny/nz`、`f_dc_0..2`、`f_rest_0..44`、`opacity`、`scale_0..2`、`rot_0..3`），
可被 gsplat.js 等标准渲染器直接加载。

## 支持平台

Android / iOS / OpenHarmony（均为 FFI 插件，原生库随宿主应用构建）。

## 构建

- Android：`android/build.gradle` → NDK → `src/CMakeLists.txt`，构建时自动编译
- iOS：`ios/gsct_decoder.podspec` → `src/CMakeLists.txt`，构建时自动编译
- OHOS：**预编译库**提交在 `ohos/libs/<abi>/`，随 har 打包（见下）

### OHOS 为什么用预编译库

hvigor 的 `BuildNativeWithCmake` / `BuildNativeWithNinja` 只在**模块源码集内**
存在 C/C++ 文件时才执行（`CmakeUtil.nativeTaskCondition` 要求
`CodeMap.has(CodeType.CPP)`）。本插件源码与 Android / iOS / 桌面共用、位于模块外的
`src/`，`ohos/` 模块内没有任何 `.cpp` —— 该任务会被**静默跳过**（构建无报错，
但既不编译也不产出 `.so`，最终 hap 里没有 `libgsct_decoder.so`，运行时
`DynamicLibrary.open` 失败，3D 影像降级为环绕视频）。

故 OHOS 走与 `sqlcipher_flutter_libs` 相同的路线：产物放 `ohos/libs/<abi>/`，
由 hvigor 的 `ProcessLibs` 任务收集、裁剪并打包进 har。

### 重新编译 OHOS 原生库

**改动 `src/` 下的 C++ 代码后必须重新执行**（Android / iOS 会自动重编，OHOS 不会）：

```bash
bash ohos/build_libs.sh   # 需要 DevEco Studio SDK；用 HOS_SDK 可覆盖 SDK 路径
```

脚本用 SDK 自带 cmake / ninja 与 `ohos.toolchain.cmake` 编译 arm64-v8a、
armeabi-v7a、x86_64 三个架构，产物直接写入 `ohos/libs/`。

固定 `OHOS_STL=c++_static`：hap 内不存在 `libc++_shared.so`
（`libflutter.so`、`libsqlcipher.so` 也只依赖 `libc.so`），动态链接 STL
会导致运行时加载失败。产物只导出 `gsct_probe` / `gsct_decode_to_ply` /
`gsct_last_error` 三个 C 符号。

桌面验证（可选）：

```bash
cmake -S src -B build -DGSCT_BUILD_CLI=ON -DGSCT_LIB_TYPE=STATIC
cmake --build build --parallel
./build/gsct_cli probe  stream.gsct
./build/gsct_cli decode stream.gsct out.ply
```

## 依赖与许可

本包自有代码 MIT。内嵌 astc-encoder（Apache-2.0）、zlib（zlib License），
解码逻辑移植自 spatial-vivid-toolkit（MIT）。
详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 已知限制

- 仅支持 `superCompressionScheme = 1`（zlib）的码流；
- 属性编码支持简单量化（scheme 0）、矢量量化（scheme 1）、
  ASTC 纹理（scheme 2），与上游实现一致；
- 输出的 PLY 含完整 SH，体积约为原始码流的 10 倍
  （510600 点约 126 MB），渲染侧加载时需注意内存占用。
