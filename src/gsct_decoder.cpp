// Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
// SPDX-License-Identifier: MIT
//
// 移植自 spatial-vivid-toolkit packages/gltf/src/gsdecoder.cpp。
// 调整点见 gsct_decoder.h 头部说明。

#include "gsct_decoder.h"

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "astcenc.h"

namespace gsct {

namespace {

/// ASTC 文件头（16 字节）
struct AstcHeader {
    uint8_t magic[4];  ///< 0x5CA1AB13
    uint8_t blockdimX;
    uint8_t blockdimY;
    uint8_t blockdimZ;
    uint8_t xsize[3];  ///< 小端 24 位
    uint8_t ysize[3];
    uint8_t zsize[3];
};

constexpr uint32_t kAstcMagic = 0x5CA1AB13;

uint32_t readU24LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16);
}

/// 读取 32 位无符号（小端）
uint32_t readU32LE(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

/// 读取 float（小端）
float readFloatLE(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

/// zlib 解压（对应原实现 decompress_zlib）
bool inflateZlib(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(src);
    stream.avail_in = static_cast<uInt>(srcSize);
    stream.next_out = dst;
    stream.avail_out = static_cast<uInt>(dstSize);

    if (inflateInit(&stream) != Z_OK) return false;

    const int ret = inflate(&stream, Z_FINISH);
    const bool ok = (ret == Z_STREAM_END) && (stream.total_out == dstSize);
    inflateEnd(&stream);
    return ok;
}

/// 元数据解析越界时抛出，由 parseHeader 统一捕获转为错误信息
struct ParseError : std::runtime_error {
    explicit ParseError(const char* msg) : std::runtime_error(msg) {}
};

/// 断言剩余可读字节足够，不足即抛出
///
/// 属性元数据是变长结构，损坏或伪造的码流很容易让朴素解析越界读取。
#define GSCT_NEED(n)                                                       \
    do {                                                                   \
        if (pointer + static_cast<size_t>(n) > size) {                     \
            throw ParseError("属性元数据越界");                             \
        }                                                                  \
    } while (0)

/// 解析单个 patch 元数据（对应 parseUWAGSPatch）
void parsePatch(const uint8_t* data, size_t size, size_t& pointer,
                const AttributeMeta& attr, PatchMeta& patch) {
    if (attr.patchGlobalEnableIndex) {
        GSCT_NEED(4);
        patch.patchIndex = readU32LE(data + pointer);
        pointer += 4;
    }
    if (attr.patchGlobalEnableSize) {
        GSCT_NEED(4);
        patch.patchSize = readU32LE(data + pointer);
        pointer += 4;
    }
    if (attr.quantizationFlag == 1) {
        if (attr.patchGlobalEnableQuantBit) {
            GSCT_NEED(1);
            patch.patchQuantBits = data[pointer++];
        }
        if (attr.patchGlobalEnableQuantMinMax) {
            GSCT_NEED(static_cast<size_t>(attr.componentsCount) * 8);
            patch.patchQuantMin.resize(attr.componentsCount);
            for (uint8_t i = 0; i < attr.componentsCount; ++i) {
                patch.patchQuantMin[i] = readFloatLE(data + pointer);
                pointer += 4;
            }
            patch.patchQuantMax.resize(attr.componentsCount);
            for (uint8_t i = 0; i < attr.componentsCount; ++i) {
                patch.patchQuantMax[i] = readFloatLE(data + pointer);
                pointer += 4;
            }
        }
    }
}

/// 解析单个属性元数据（对应 parseUWAGSAttributeMeta）
AttributeMeta parseAttributeMeta(const uint8_t* data, size_t size,
                                 size_t& pointer) {
    AttributeMeta m;
    GSCT_NEED(4);
    m.attributeType = readU32LE(data + pointer);
    pointer += 4;

    GSCT_NEED(3);
    m.componentsCount = data[pointer++];
    m.uncompressedDataType = data[pointer++];

    const uint8_t tmp = data[pointer++];
    m.quantizationFlag = (tmp >> 4) & 0xF;
    m.encoderScheme = tmp & 0xF;

    if (m.quantizationFlag == 1) {
        GSCT_NEED(1);
        m.quantizationBits = data[pointer++];
        GSCT_NEED(static_cast<size_t>(m.componentsCount) * 8);
        m.quantMin.resize(m.componentsCount);
        for (uint8_t i = 0; i < m.componentsCount; ++i) {
            m.quantMin[i] = readFloatLE(data + pointer);
            pointer += 4;
        }
        m.quantMax.resize(m.componentsCount);
        for (uint8_t i = 0; i < m.componentsCount; ++i) {
            m.quantMax[i] = readFloatLE(data + pointer);
            pointer += 4;
        }
    }

    if (m.encoderScheme == 1) {
        GSCT_NEED(5);
        m.indexType = data[pointer++];
        m.codebookLength = readU32LE(data + pointer);
        pointer += 4;
    }

    if (m.encoderScheme == 2) {
        GSCT_NEED(10);
        m.mimeType = data[pointer++];
        m.singleHeight = static_cast<uint16_t>(data[pointer]) |
                         (static_cast<uint16_t>(data[pointer + 1]) << 8);
        pointer += 2;
        m.singleWidth = static_cast<uint16_t>(data[pointer]) |
                        (static_cast<uint16_t>(data[pointer + 1]) << 8);
        pointer += 2;
        m.align = data[pointer++];
        m.concat = data[pointer++];
        m.concatMaxWidth = data[pointer++];
        m.concatMaxHeight = data[pointer++];
        m.texNum = data[pointer++];

        GSCT_NEED(static_cast<size_t>(m.texNum) * 4);
        m.texSizes.resize(m.texNum);
        for (uint8_t i = 0; i < m.texNum; ++i) {
            m.texSizes[i] = readU32LE(data + pointer);
            pointer += 4;
        }
    }

    GSCT_NEED(16);
    m.byteOffset = readU32LE(data + pointer);
    pointer += 4;
    m.byteLength = readU32LE(data + pointer);
    pointer += 4;
    m.uncompressedByteLength = readU32LE(data + pointer);
    pointer += 4;
    m.patchNum = readU32LE(data + pointer);
    pointer += 4;

    if (m.patchNum > 1) {
        GSCT_NEED(2);
        uint16_t flags;
        std::memcpy(&flags, data + pointer, 2);
        pointer += 2;

        m.patchGlobalEnableIndex = (flags >> 15) & 0x1;
        m.patchGlobalEnableSize = (flags >> 14) & 0x1;
        m.patchGlobalEnableQuantBit = (flags >> 13) & 0x1;
        m.patchGlobalEnableQuantMinMax = (flags >> 12) & 0x1;
        m.patchGlobalEnable2DMapping = (flags >> 11) & 0x1;

        if (m.patchGlobalEnableSize == 0) {
            GSCT_NEED(4);
            m.lastPatchSize = readU32LE(data + pointer);
            pointer += 4;
        }

        m.patches.resize(m.patchNum);
        for (uint32_t i = 0; i < m.patchNum; ++i) {
            parsePatch(data, size, pointer, m, m.patches[i]);
        }
    }

    return m;
}

#undef GSCT_NEED

/// ASTC 纹理解码（对应 decodeASTC）
///
/// 解码后的像素按「系数优先」重排：
///   out[(pointIndex + coefIndex * numPoints) * 3 + channel]
/// 其中 coefIndex = x / singleWidth —— 纹理横向被切成 texNum 个单系数带。
bool decodeAstcTexture(const std::vector<uint8_t>& attrData, uint32_t offset,
                       uint32_t texSize, const AttributeMeta& attr,
                       uint32_t numPoints, std::vector<uint8_t>& out,
                       std::string& error) {
    if (texSize < 16) {
        error = "ASTC 纹理长度不足 16 字节";
        return false;
    }
    if (static_cast<size_t>(offset) + texSize > attrData.size()) {
        error = "ASTC 纹理区间越界";
        return false;
    }
    const AstcHeader* head =
        reinterpret_cast<const AstcHeader*>(attrData.data() + offset);
    if (readU32LE(head->magic) != kAstcMagic) {
        error = "ASTC 纹理魔数不匹配";
        return false;
    }

    const uint32_t blockSize = head->blockdimX;
    const uint32_t texWidth = readU24LE(head->xsize);
    const uint32_t texHeight = readU24LE(head->ysize);
    const uint32_t singleWidth = attr.singleWidth;
    const uint32_t blockPoints = singleWidth * texHeight;  // 单系数带内的点数

    if (blockSize == 0 || singleWidth == 0 || texWidth == 0 || texHeight == 0) {
        error = "ASTC 纹理尺寸非法";
        return false;
    }
    // 上限约束：纹理尺寸来自文件，伪造的巨大值会导致分配失败或整数溢出
    constexpr uint64_t kMaxPixels = 1ull << 26;  // 约 6700 万像素
    if (static_cast<uint64_t>(texWidth) * texHeight > kMaxPixels) {
        error = "ASTC 纹理尺寸超出上限";
        return false;
    }
    // 系数带数必须整除，否则像素重排会写出界
    if (texWidth % singleWidth != 0 || singleWidth % blockSize != 0 ||
        texHeight % blockSize != 0) {
        error = "ASTC 纹理尺寸与系数带不整除";
        return false;
    }

    astcenc_config config{};
    // 本库以 ASTCENC_DECOMPRESS_ONLY 构建，context 必须带同名标志
    astcenc_error status = astcenc_config_init(
        ASTCENC_PRF_LDR, blockSize, blockSize, 1, ASTCENC_PRE_MEDIUM,
        ASTCENC_FLG_DECOMPRESS_ONLY, &config);
    if (status != ASTCENC_SUCCESS) {
        error = std::string("astcenc 配置失败: ") + astcenc_get_error_string(status);
        return false;
    }

    unsigned hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0) hwThreads = 1;
    const unsigned threadCount = std::min<unsigned>(hwThreads, 8);

    astcenc_context* context = nullptr;
    status = astcenc_context_alloc(&config, threadCount, &context);
    if (status != ASTCENC_SUCCESS) {
        error = std::string("astcenc context 分配失败: ") +
                astcenc_get_error_string(status);
        return false;
    }

    astcenc_image image{};
    image.data_type = ASTCENC_TYPE_U8;
    image.dim_x = texWidth;
    image.dim_y = texHeight;
    image.dim_z = 1;
    std::vector<uint8_t> colorBuffer(static_cast<size_t>(texWidth) * texHeight * 4);
    void* slices[1] = {colorBuffer.data()};
    image.data = slices;

    static const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G,
                                         ASTCENC_SWZ_B, ASTCENC_SWZ_A};

    astcenc_error decodeError = ASTCENC_SUCCESS;
    {
        std::atomic<int> sharedError{static_cast<int>(ASTCENC_SUCCESS)};
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (unsigned t = 0; t < threadCount; ++t) {
            threads.emplace_back([&, t]() {
                const astcenc_error e = astcenc_decompress_image(
                    context, attrData.data() + offset + 16, texSize - 16, &image,
                    &swizzle, t);
                if (e != ASTCENC_SUCCESS) {
                    // 多线程共享同一错误槽位，仅首次写入生效
                    int expected = static_cast<int>(ASTCENC_SUCCESS);
                    sharedError.compare_exchange_strong(expected,
                                                        static_cast<int>(e));
                }
            });
        }
        for (auto& t : threads) t.join();
        decodeError = static_cast<astcenc_error>(sharedError.load());
    }
    astcenc_context_free(context);

    if (decodeError != ASTCENC_SUCCESS) {
        error = std::string("ASTC 解码失败: ") +
                astcenc_get_error_string(decodeError);
        return false;
    }

    // 像素重排：ASTC 块内 blockSize² 个像素对应连续的点，
    // 块序号 × blockSize² + 块内序号 = 该点在单系数带内的线性索引。
    //
    // 输出缓冲按整张纹理分配：纹理横向被切成 texWidth / singleWidth 个系数带，
    // 带 k 的数据整体偏移 k * blockPoints。
    out.resize(static_cast<size_t>(texWidth) * texHeight * 3);
    const uint32_t blockSides = singleWidth / blockSize;

    for (uint32_t y = 0; y < texHeight; ++y) {
        const uint8_t* srcRow = colorBuffer.data() + static_cast<size_t>(y) * texWidth * 4;
        const uint32_t blockOffsetY = y / blockSize;
        const uint32_t inBlockOffsetY = y - blockOffsetY * blockSize;

        for (uint32_t x = 0; x < texWidth; ++x) {
            const uint32_t coefIndex = x / singleWidth;      // 系数索引
            const uint32_t xInCoef = x % singleWidth;        // 带内横坐标
            const uint32_t blockOffsetX = xInCoef / blockSize;
            const uint32_t inBlockOffsetX = xInCoef - blockOffsetX * blockSize;

            const uint32_t blockOffset =
                (blockOffsetY * blockSides + blockOffsetX) * blockSize * blockSize;
            const uint32_t inNum = inBlockOffsetY * blockSize + inBlockOffsetX;
            const uint32_t pointIndex = blockOffset + inNum;

            const uint8_t* src = srcRow + static_cast<size_t>(x) * 4;
            uint8_t* dst = out.data() +
                           (static_cast<size_t>(pointIndex) +
                            static_cast<size_t>(coefIndex) * blockPoints) * 3;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }
    return true;
}

/// 解码单个属性在点 idx 处的值（对应 decode_attribute）
///
/// 输出 decoded 长度至少为 componentsCount。
bool decodeAttributeValue(const std::vector<uint8_t>& attrData,
                          const AttributeMeta& attr,
                          const std::vector<uint8_t>& images,
                          uint32_t numPoints, uint32_t idx,
                          std::vector<float>& decoded, std::string& error) {
    const uint8_t comp = attr.componentsCount;
    decoded.resize(comp);

    if (attr.encoderScheme == 2) {
        // 二维纹理（ASTC）
        if (!images.empty()) {
            const float quantScale = static_cast<float>((1 << attr.quantizationBits) - 1);
            for (uint8_t j = 0; j < comp; ++j) {
                decoded[j] =
                    images[(static_cast<size_t>(idx) +
                            static_cast<size_t>(j / 3) * numPoints) * 3 + j % 3] /
                    quantScale;
            }
        } else {
            for (uint8_t j = 0; j < comp; ++j) {
                decoded[j] = -1.0f * attr.quantMin[j] /
                             (attr.quantMax[j] - attr.quantMin[j]);
            }
        }
    } else if (attr.encoderScheme == 1) {
        // 矢量量化
        if (attr.indexType != 7) {
            error = "矢量量化仅支持 uint32 索引";
            return false;
        }
        uint32_t index;
        std::memcpy(&index, attrData.data() + attr.byteOffset + idx * 4, 4);
        const size_t quantizedOffset = attr.byteOffset + static_cast<size_t>(numPoints) * 4;
        for (uint8_t j = 0; j < comp; ++j) {
            const size_t off = quantizedOffset + (static_cast<size_t>(index) * comp + j);
            decoded[j] = static_cast<float>(attrData[off]) / 255.0f;
        }
    } else {
        // 简单量化
        const size_t quantizedSize = attr.quantizationBits > 8 ? 2 : 1;
        for (uint8_t j = 0; j < comp; ++j) {
            const size_t off =
                attr.byteOffset +
                (static_cast<size_t>(idx) * comp + j) * quantizedSize;
            uint16_t q = 0;
            if (quantizedSize == 2) {
                std::memcpy(&q, attrData.data() + off, 2);
            } else {
                q = attrData[off];
            }
            decoded[j] = static_cast<float>(q) /
                         static_cast<float>((1u << attr.quantizationBits) - 1);
        }
    }

    // 逆量化
    if (attr.patchNum == 1) {
        for (uint8_t j = 0; j < comp; ++j) {
            decoded[j] = decoded[j] * (attr.quantMax[j] - attr.quantMin[j]) +
                         attr.quantMin[j];
        }
    } else {
        if (attr.patchGlobalEnableSize != 0) {
            error = "不支持的分块尺寸模式";
            return false;
        }
        const size_t chunkSize = static_cast<size_t>(std::llround(
            static_cast<double>(numPoints - attr.lastPatchSize) /
            (attr.patchNum - 1)));
        if (chunkSize == 0) {
            error = "分块尺寸为 0";
            return false;
        }
        const size_t patchIdx = std::min<size_t>(idx / chunkSize,
                                                 attr.patches.size() - 1);
        const PatchMeta& patch = attr.patches[patchIdx];
        for (uint8_t j = 0; j < comp; ++j) {
            decoded[j] = decoded[j] *
                             (patch.patchQuantMax[j] - patch.patchQuantMin[j]) +
                         patch.patchQuantMin[j];
        }
    }
    return true;
}

/// 把解码值写入输出数组（对应 decode_attribute 尾部的 switch）
void storeAttribute(SplatData& out, const AttributeMeta& attr, uint32_t idx,
                    const std::vector<float>& decoded) {
    const uint32_t n = out.numPoints;
    switch (attr.attributeType) {
        case ATTR_POSITION:
            for (int j = 0; j < 3; ++j) out.position[idx * 3 + j] = decoded[j];
            break;
        case ATTR_ROTATION:
            for (int j = 0; j < 4; ++j) out.rotation[idx * 4 + j] = decoded[j];
            break;
        case ATTR_SCALE:
            for (int j = 0; j < 3; ++j) out.scaling[idx * 3 + j] = decoded[j];
            break;
        case ATTR_DC_OPACITY: {
            const uint8_t comp = attr.componentsCount;
            std::memcpy(&out.color[idx * 3], decoded.data(),
                        sizeof(float) * (comp - 1));
            float op = decoded[comp - 1];
            if (op < 0.001f) op = 0.001f;
            if (op > 0.999f) op = 0.999f;
            // 反 sigmoid：PLY 中的 opacity 存的是激活前的原始值
            out.opacity[idx] = std::log(op / (1.0f - op));
            break;
        }
        case ATTR_SHN: {
            // shn 布局为「系数优先」：shn[(coef) * numPoints * 3 + point * 3 + channel]
            const uint8_t comp = attr.componentsCount;
            for (uint8_t j = 0; j < comp / 3; ++j) {
                std::memcpy(&out.shn[(static_cast<size_t>(idx) +
                                      static_cast<size_t>(j) * n) * 3],
                            decoded.data() + j * 3, sizeof(float) * 3);
            }
            break;
        }
        default:
            break;
    }
}

}  // namespace

bool parseHeader(const uint8_t* data, size_t size, StreamHeader& header,
                 std::vector<AttributeMeta>& attributes, std::string& error,
                 size_t* metaEnd) {
    if (size < sizeof(StreamHeader)) {
        error = "码流长度不足";
        return false;
    }
    if (std::memcmp(data, UWA_MAGIC, 4) != 0) {
        error = "魔数不是 gsct";
        return false;
    }

    std::memcpy(&header, data, sizeof(StreamHeader));

    size_t pointer = sizeof(StreamHeader);
    attributes.clear();
    attributes.reserve(header.numAttribute);
    try {
        for (uint8_t i = 0; i < header.numAttribute; ++i) {
            attributes.push_back(parseAttributeMeta(data, size, pointer));
        }
    } catch (const ParseError& e) {
        error = e.what();
        return false;
    } catch (const std::bad_alloc&) {
        error = "内存不足";
        return false;
    }
    if (metaEnd) *metaEnd = pointer;
    return true;
}

bool decode(const uint8_t* data, size_t size, SplatData& out, std::string& error) {
    StreamHeader header{};
    std::vector<AttributeMeta> attributes;
    size_t metaEnd = 0;
    if (!parseHeader(data, size, header, attributes, error, &metaEnd)) return false;

    if (header.superCompressionScheme != 1) {
        error = "不支持的压缩方案（期望 zlib）";
        return false;
    }
    if (metaEnd >= size) {
        error = "无属性数据流";
        return false;
    }

    // zlib 解压
    std::vector<uint8_t> attrData(header.totalByteLength);
    if (!inflateZlib(data + metaEnd, size - metaEnd, attrData.data(),
                     attrData.size())) {
        error = "zlib 解压失败";
        return false;
    }

    // 属性数据范围校验：元数据声明的区间必须落在解压结果内，
    // 否则后续逐点解码会越界读取
    for (const auto& a : attributes) {
        if (static_cast<size_t>(a.byteOffset) + a.byteLength > attrData.size()) {
            error = "属性数据区间越界";
            return false;
        }
    }

    // 定位各属性并确定 SH 阶数
    const AttributeMeta* posAttr = nullptr;
    const AttributeMeta* rotAttr = nullptr;
    const AttributeMeta* scaleAttr = nullptr;
    const AttributeMeta* dcAttr = nullptr;
    const AttributeMeta* shAttr = nullptr;
    int maxComponents = 3;

    for (const auto& a : attributes) {
        if (a.componentsCount > maxComponents) maxComponents = a.componentsCount;
        switch (a.attributeType) {
            case ATTR_POSITION: posAttr = &a; break;
            case ATTR_ROTATION: rotAttr = &a; break;
            case ATTR_SCALE: scaleAttr = &a; break;
            case ATTR_DC_OPACITY: dcAttr = &a; break;
            case ATTR_SHN: shAttr = &a; break;
            default: break;
        }
    }
    if (!posAttr || !rotAttr || !scaleAttr || !dcAttr) {
        error = "缺少必需的属性（POSITION/ROTATION/SCALE/DC）";
        return false;
    }

    const uint32_t n = header.numPoints;
    out.numPoints = n;
    out.position.assign(static_cast<size_t>(n) * 3, 0.0f);
    out.scaling.assign(static_cast<size_t>(n) * 3, 0.0f);
    out.rotation.assign(static_cast<size_t>(n) * 4, 0.0f);
    out.opacity.assign(n, 0.0f);
    out.color.assign(static_cast<size_t>(n) * 3, 0.0f);

    // SH：纹理编码时解码 ASTC；否则置空
    std::vector<uint8_t> images;
    if (shAttr) {
        out.shDegree = static_cast<int>(
            std::sqrt(static_cast<double>(shAttr->componentsCount) / 3.0 + 1.0)) - 1;
        if (shAttr->encoderScheme == SCHEME_TEXTURE2D) {
            uint32_t offset = shAttr->byteOffset;
            for (uint8_t i = 0; i < shAttr->texNum; ++i) {
                std::vector<uint8_t> texPixels;
                if (!decodeAstcTexture(attrData, offset, shAttr->texSizes[i],
                                       *shAttr, n, texPixels, error)) {
                    return false;
                }
                // 首张纹理解码后即可准确预估总量
                if (i == 0) {
                    images.reserve(texPixels.size() * shAttr->texNum);
                }
                images.insert(images.end(), texPixels.begin(), texPixels.end());
                offset += shAttr->texSizes[i];
            }

            // 纹理像素数必须与「系数数 × 点数」吻合，否则逐点取值会越界
            const size_t expectedPixels =
                static_cast<size_t>(shAttr->componentsCount) * n;
            if (images.size() != expectedPixels) {
                error = "SH 纹理尺寸与点数不匹配";
                return false;
            }
        }
        out.shn.assign(static_cast<size_t>(shAttr->componentsCount) * n, 0.0f);
    }

    // 逐点解码（多线程分块）
    unsigned hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0) hwThreads = 4;
    const unsigned numThreads = std::max(1u, hwThreads);
    const size_t chunkSize = (n + numThreads - 1) / numThreads;

    std::vector<std::string> errors(numThreads);
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (unsigned t = 0; t < numThreads; ++t) {
        const size_t start = t * chunkSize;
        const size_t end = std::min<size_t>(start + chunkSize, n);
        if (start >= end) break;

        threads.emplace_back([&, start, end, t]() {
            std::vector<float> decoded;
            decoded.reserve(maxComponents);
            for (size_t i = start; i < end; ++i) {
                for (const auto& a : attributes) {
                    // 未解码 SH 纹理时跳过 SHN（避免写入占位值）
                    if (a.attributeType == ATTR_SHN && images.empty()) continue;
                    if (!decodeAttributeValue(attrData, a, images, n,
                                              static_cast<uint32_t>(i), decoded,
                                              errors[t])) {
                        return;
                    }
                    storeAttribute(out, a, static_cast<uint32_t>(i), decoded);
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    for (const auto& e : errors) {
        if (!e.empty()) {
            error = e;
            return false;
        }
    }
    return true;
}

}  // namespace gsct
