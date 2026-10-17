import 'dart:isolate';

import 'src/bindings.dart';

/// gsct 码流探测结果
class GsctInfo {
  const GsctInfo({
    required this.numPoints,
    required this.numAttributes,
    required this.version,
    required this.shDegree,
  });

  /// 高斯点总数
  final int numPoints;

  /// 属性个数
  final int numAttributes;

  /// 码流版本号，形如 `1.0.0`
  final String version;

  /// SH 阶数；-1 表示不含高阶 SH
  final int shDegree;

  @override
  String toString() =>
      'GsctInfo(points: $numPoints, attributes: $numAttributes, '
      'version: $version, shDegree: $shDegree)';
}

/// gsct 解码异常
class GsctException implements Exception {
  const GsctException(this.message);

  final String message;

  @override
  String toString() => 'GsctException: $message';
}

/// HarmonyOS 3D 影像（gsct 格式）解码器
///
/// 输入为 Remy 导出的 3D 影像中提取的 gsct 压缩码流，
/// 输出为标准 3DGS PLY（binary_little_endian，含 SH），
/// 可被 gsplat.js 等标准渲染器直接加载。
///
/// ## 线程模型
///
/// [decodeToPly] 在独立 isolate 中执行 —— 解码含 zlib 解压与 ASTC 纹理
/// 解码，百万点级数据耗时可达数秒，不应占用主 isolate。
class GsctDecoder {
  const GsctDecoder._();

  /// 探测 gsct 码流（只解析头部与属性元数据，不解码）
  ///
  /// 用于导入前的快速判定，耗时与文件大小同阶（毫秒级）。
  /// 非 gsct 文件或解析失败时返回 null。
  static GsctInfo? probe(String inputPath) {
    try {
      final result = probeNative(inputPath);
      if (result.status != gsctOk) return null;
      return GsctInfo(
        numPoints: result.numPoints,
        numAttributes: result.numAttributes,
        version:
            '${result.versionMajor}.${result.versionMinor}.${result.versionPatch}',
        shDegree: result.shDegree,
      );
    } catch (_) {
      // 原生库不可用（如未接入的平台）时按「非 gsct」处理，由调用方降级
      return null;
    }
  }

  /// [probe] 的 isolate 版本
  ///
  /// 探测需读取整个文件（属性元数据可能达上百 KB，无法只读固定头部），
  /// 在导入等 UI 路径上调用时应走本方法避免阻塞主 isolate。
  static Future<GsctInfo?> probeAsync(String inputPath) =>
      Isolate.run(() => probe(inputPath));

  /// 解码 gsct 码流并写出标准 3DGS PLY
  ///
  /// [inputPath]  gsct 码流文件路径
  /// [outputPath] 输出 PLY 文件路径
  ///
  /// 解码失败抛出 [GsctException]。
  static Future<void> decodeToPly({
    required String inputPath,
    required String outputPath,
  }) async {
    final error = await Isolate.run(() {
      final status = decodeToPlyNative(inputPath, outputPath);
      return status == gsctOk ? null : '($status) ${lastErrorNative()}';
    });
    if (error != null) {
      throw GsctException(error);
    }
  }
}
