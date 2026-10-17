# gsct_decoder

English | [简体中文](README.zh-CN.md)

A decoder for the **gsct** compressed stream exported by HarmonyOS 3D photos
(Remy). It converts the stream into a standard 3DGS PLY.

## Background

A 3D photo captured with HarmonyOS "Remy" is exported as a single `.mp4` that
bundles a preview video and a 3DGS (Gaussian splatting) model. The model uses
the UWA private extension `UWA_primitive_3DGS_compression`; its payload starts
with the `gsct` magic and its attributes are encoded with
**zlib + simple quantization + ASTC textures**.

This format is **not** the same codec as the EGSC format standardized later
(`UWA_gaussian_splatting_compression_EGSC` in UWA T/UWA 050-2026, where
attributes are compressed with a video codec): gsct needs no video decoder,
only zlib and ASTC decoding.

This package is ported from `gsdecoder.cpp` — the part of the official UWA
reference implementation
[spatial-vivid-toolkit](https://gitcode.com/UHD-World-Association/spatial-vivid-toolkit)
that is not exposed through its CLI — and packaged as a Flutter FFI plugin.

## Usage

```dart
import 'package:gsct_decoder/gsct_decoder.dart';

// Probe (cheap, header only)
final info = GsctDecoder.probe('/path/to/stream.gsct');
if (info != null) {
  print('${info.numPoints} gaussians, SH degree ${info.shDegree}');
}

// Decode to PLY (CPU bound, runs in a separate isolate)
await GsctDecoder.decodeToPly(
  inputPath: '/path/to/stream.gsct',
  outputPath: '/path/to/output.ply',
);
```

The output PLY is `binary_little_endian` with the same fields as those exported
by 3DGS training scripts (`x/y/z`, `nx/ny/nz`, `f_dc_0..2`, `f_rest_0..44`,
`opacity`, `scale_0..2`, `rot_0..3`) and can be loaded directly by standard
renderers such as gsplat.js.

## Supported platforms

Android / iOS / OpenHarmony (all FFI plugins; the native library is built or
shipped with the host application).

## Building

- Android: `android/build.gradle` → NDK → `src/CMakeLists.txt`, compiled at
  build time
- iOS: `ios/gsct_decoder.podspec` → `src/CMakeLists.txt`, compiled at build time
- OpenHarmony: **prebuilt binaries** committed under `ohos/libs/<abi>/` and
  packed into the har (see below)

### Why OpenHarmony uses prebuilt binaries

hvigor only runs the `BuildNativeWithCmake` / `BuildNativeWithNinja` tasks when
the **module's own source set** contains C/C++ files
(`CmakeUtil.nativeTaskCondition` requires `CodeMap.has(CodeType.CPP)`). Here the
sources are shared with Android / iOS / desktop and live in `src/`, outside the
`ohos/` module, which contains no `.cpp` at all — so those tasks are **silently
skipped**: the build reports no error, yet it neither compiles nor produces a
`.so`. The resulting hap has no `libgsct_decoder.so`, `DynamicLibrary.open`
fails at runtime, and the 3D photo silently degrades to its orbit video.

OpenHarmony therefore follows the same route as `sqlcipher_flutter_libs`: the
binaries live in `ohos/libs/<abi>/`, and hvigor's `ProcessLibs` task collects,
strips and packs them into the har.

### Rebuilding the OpenHarmony binaries

**Required after any change to the C++ code under `src/`** (Android / iOS
rebuild automatically, OpenHarmony does not):

```bash
bash ohos/build_libs.sh   # requires the DevEco Studio SDK; override it with HOS_SDK
```

The script uses the SDK's own cmake / ninja and `ohos.toolchain.cmake` to build
arm64-v8a, armeabi-v7a and x86_64, writing the results straight into
`ohos/libs/`.

`OHOS_STL=c++_static` is pinned: the hap contains no `libc++_shared.so`
(`libflutter.so` and `libsqlcipher.so` also depend on `libc.so` only), and
linking the STL dynamically would make the library fail to load at runtime. The
binaries export exactly three C symbols: `gsct_probe`, `gsct_decode_to_ply`
and `gsct_last_error`.

Desktop verification (optional):

```bash
cmake -S src -B build -DGSCT_BUILD_CLI=ON -DGSCT_LIB_TYPE=STATIC
cmake --build build --parallel
./build/gsct_cli probe  stream.gsct
./build/gsct_cli decode stream.gsct out.ply
```

## Dependencies and license

The package's own code is MIT. It bundles astc-encoder (Apache-2.0) and zlib
(zlib License); the decoding logic is ported from spatial-vivid-toolkit (MIT).
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.

## Known limitations

- Only streams with `superCompressionScheme = 1` (zlib) are supported;
- Attribute encodings: simple quantization (scheme 0), vector quantization
  (scheme 1) and ASTC textures (scheme 2), matching the upstream implementation;
- The output PLY carries full SH and is roughly 10× the size of the source
  stream (≈ 126 MB for 510,600 gaussians) — mind the memory usage on the
  rendering side.
