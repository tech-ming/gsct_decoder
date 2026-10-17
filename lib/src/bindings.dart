import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

/// 状态码（与 src/gsct_api.h 的 gsct_status 对应）
const int gsctOk = 0;

/// gsct_info 的 C 结构体镜像
final class GsctInfoNative extends Struct {
  @Uint32()
  external int numPoints;

  @Uint8()
  external int numAttributes;

  @Uint8()
  external int versionMajor;

  @Uint8()
  external int versionMinor;

  @Uint8()
  external int versionPatch;

  @Int32()
  external int shDegree;
}

typedef _ProbeC = Int32 Function(
    Pointer<Utf8> inputPath, Pointer<GsctInfoNative> outInfo);
typedef _ProbeDart = int Function(
    Pointer<Utf8> inputPath, Pointer<GsctInfoNative> outInfo);

typedef _DecodeToPlyC = Int32 Function(
    Pointer<Utf8> inputPath, Pointer<Utf8> outputPath);
typedef _DecodeToPlyDart = int Function(
    Pointer<Utf8> inputPath, Pointer<Utf8> outputPath);

typedef _LastErrorC = Pointer<Utf8> Function();
typedef _LastErrorDart = Pointer<Utf8> Function();

/// 原生库句柄（进程内单例）
DynamicLibrary? _lib;

/// 取原生库句柄，首次调用时加载
DynamicLibrary get _library {
  final cached = _lib;
  if (cached != null) return cached;

  final DynamicLibrary lib;
  if (Platform.isIOS || Platform.isMacOS) {
    // iOS/macOS 走静态链接，符号在主程序内
    lib = DynamicLibrary.process();
  } else if (Platform.isAndroid || Platform.isOhos) {
    lib = DynamicLibrary.open('libgsct_decoder.so');
  } else if (Platform.isWindows) {
    lib = DynamicLibrary.open('gsct_decoder.dll');
  } else {
    lib = DynamicLibrary.open('libgsct_decoder.so');
  }
  _lib = lib;
  return lib;
}

late final _ProbeDart _probe =
    _library.lookupFunction<_ProbeC, _ProbeDart>('gsct_probe');

late final _DecodeToPlyDart _decodeToPly =
    _library.lookupFunction<_DecodeToPlyC, _DecodeToPlyDart>(
        'gsct_decode_to_ply');

late final _LastErrorDart _lastError =
    _library.lookupFunction<_LastErrorC, _LastErrorDart>('gsct_last_error');

/// 探测 gsct 码流
///
/// 返回各字段的**纯 Dart 副本** —— [GsctInfoNative] 的 `.ref` 是原生内存视图，
/// 不能在释放缓冲后继续持有，故在此处一次性取出全部字段。
({
  int status,
  int numPoints,
  int numAttributes,
  int versionMajor,
  int versionMinor,
  int versionPatch,
  int shDegree,
}) probeNative(String inputPath) {
  final pathPtr = inputPath.toNativeUtf8();
  final infoPtr = calloc<GsctInfoNative>();
  try {
    final status = _probe(pathPtr, infoPtr);
    final info = infoPtr.ref;
    return (
      status: status,
      numPoints: info.numPoints,
      numAttributes: info.numAttributes,
      versionMajor: info.versionMajor,
      versionMinor: info.versionMinor,
      versionPatch: info.versionPatch,
      shDegree: info.shDegree,
    );
  } finally {
    calloc.free(pathPtr);
    calloc.free(infoPtr);
  }
}

/// 解码 gsct 码流为 PLY，返回状态码
int decodeToPlyNative(String inputPath, String outputPath) {
  final inputPtr = inputPath.toNativeUtf8();
  final outputPtr = outputPath.toNativeUtf8();
  try {
    return _decodeToPly(inputPtr, outputPtr);
  } finally {
    calloc.free(inputPtr);
    calloc.free(outputPtr);
  }
}

/// 取最近一次错误描述
String lastErrorNative() => _lastError().toDartString();
