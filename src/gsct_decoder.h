// Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
// SPDX-License-Identifier: MIT
//
// 本文件移植自 spatial-vivid-toolkit packages/gltf/src/gsdecoder.cpp
// （华为官方 MIT 许可参考实现），做了以下调整：
//   1. 移除未使用的 zstd / happly / glm 依赖；
//   2. 结构体字段由 size_t 改为 uint32_t —— 码流字段本就是 32 位，
//      用 size_t 在 x64 上属于依赖「高 32 位恰为 0」的隐式假设；
//   3. 去掉文件 I/O 分支，统一走内存输入、内存输出。

#ifndef GSCT_DECODER_H
#define GSCT_DECODER_H

#include <cstdint>
#include <string>
#include <vector>

namespace gsct {

/// gsct 码流魔数
constexpr char UWA_MAGIC[5] = "gsct";

/// 属性类型（与码流 attributeType 字段对应）
enum AttributeType : uint32_t {
    ATTR_POSITION = 0,
    ATTR_ROTATION = 1,
    ATTR_SCALE = 2,
    ATTR_DC_OPACITY = 3,
    ATTR_SHN = 4,
};

/// 属性编码方案
enum EncoderScheme : uint8_t {
    SCHEME_QUANTIZE = 0,  ///< 简单量化（8/16 位）
    SCHEME_VQ = 1,        ///< 矢量量化（码本）
    SCHEME_TEXTURE2D = 2, ///< 二维纹理（ASTC）
};

/// 码流头（对应 UWAGSMetas，pack=1 共 21 字节）
#pragma pack(push, 1)
struct StreamHeader {
    uint8_t magic[4];              ///< "gsct"
    uint8_t version[3];            ///< 主、次、修订
    uint32_t totalByteLength;      ///< 属性数据解压后的总字节数
    uint8_t superCompressionScheme;///< 1 = zlib
    uint32_t reserved;
    uint32_t numPoints;            ///< 高斯点总数
    uint8_t numAttribute;          ///< 属性个数
};
#pragma pack(pop)

/// 分块量化参数（属性按 patch 分块时每块独立 min/max）
struct PatchMeta {
    uint32_t patchIndex = 0;
    uint32_t patchSize = 0;
    uint8_t patchQuantBits = 0;
    std::vector<float> patchQuantMin;
    std::vector<float> patchQuantMax;
};

/// 单个属性的元数据
struct AttributeMeta {
    uint32_t attributeType = 0;      ///< AttributeType
    uint8_t componentsCount = 0;     ///< 分量数
    uint8_t uncompressedDataType = 0;
    uint8_t quantizationFlag = 0;    ///< 0/1
    uint8_t encoderScheme = 0;       ///< EncoderScheme
    uint8_t quantizationBits = 0;
    std::vector<float> quantMin;
    std::vector<float> quantMax;

    // encoderScheme == 1（矢量量化）
    uint8_t indexType = 0;
    uint32_t codebookLength = 0;

    // encoderScheme == 2（二维纹理）
    uint8_t mimeType = 0;
    uint16_t singleWidth = 0;   ///< 单张纹理宽度（即一个 SH 系数占用的宽度）
    uint16_t singleHeight = 0;  ///< 纹理高度（点数在纵向的排布）
    uint8_t align = 0;
    uint8_t concat = 0;
    uint8_t concatMaxWidth = 0;
    uint8_t concatMaxHeight = 0;
    uint8_t texNum = 0;
    std::vector<uint32_t> texSizes;  ///< 各纹理字节数（含 16 字节 ASTC 头）

    uint32_t byteOffset = 0;          ///< 在属性数据流中的偏移
    uint32_t byteLength = 0;
    uint32_t uncompressedByteLength = 0;
    uint32_t patchNum = 0;

    // patchNum > 1 时的全局开关
    uint8_t patchGlobalEnableIndex = 0;
    uint8_t patchGlobalEnableSize = 0;
    uint8_t patchGlobalEnableQuantBit = 0;
    uint8_t patchGlobalEnableQuantMinMax = 0;
    uint8_t patchGlobalEnable2DMapping = 0;
    uint32_t lastPatchSize = 0;
    std::vector<PatchMeta> patches;
};

/// 解码后的高斯点数据
///
/// 各数组均为紧密排布（无步长）：
///   position / scaling : numPoints × 3
///   rotation           : numPoints × 4
///   opacity / color    : numPoints（color 为 × 3）
///   shn                : 系数优先 —— shn[coef * numPoints * 3 + point * 3 + channel]
struct SplatData {
    uint32_t numPoints = 0;
    int shDegree = 0;  ///< 0 表示无高阶 SH

    std::vector<float> position;
    std::vector<float> scaling;
    std::vector<float> rotation;
    std::vector<float> opacity;
    std::vector<float> color;  ///< DC 分量（原始值，非 RGB）
    std::vector<float> shn;    ///< 高阶 SH 系数
};

/// 解析码流头与属性元数据（不解压属性数据）
///
/// 用于快速探测点数、SH 阶数等信息。
/// [metaEnd] 非空时输出属性元数据结束位置（即压缩属性数据流的起点）。
/// 成功返回 true；失败时 error 写入原因。
bool parseHeader(const uint8_t* data, size_t size, StreamHeader& header,
                 std::vector<AttributeMeta>& attributes, std::string& error,
                 size_t* metaEnd = nullptr);

/// 完整解码 gsct 码流为高斯点数据
///
/// 内部完成：zlib 解压 → 属性逆量化 → ASTC 纹理解码 → 数据重组。
/// 成功返回 true；失败时 error 写入原因。
bool decode(const uint8_t* data, size_t size, SplatData& out, std::string& error);

}  // namespace gsct

#endif  // GSCT_DECODER_H
