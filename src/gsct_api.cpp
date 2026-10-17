// Copyright (c) 2026. SPDX-License-Identifier: MIT
//
// gsct 解码器 C 接口实现（含标准 3DGS PLY 输出）。
//
// 文件 I/O 统一走 FILE*：Windows 下用 _wfopen 接受 UTF-8 路径（转宽字符），
// 其余平台直接 fopen —— 避免 MinGW 的 libstdc++ 不支持宽字符路径重载的问题。

#include "gsct_api.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gsct_decoder.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

/// 线程局部的最近错误描述
thread_local std::string g_lastError;

void setError(const std::string& msg) { g_lastError = msg; }

/// 以 UTF-8 路径打开文件
///
/// [mode] 取 "rb" / "wb"，Windows 下内部转换为宽字符版本。
FILE* openFileUtf8(const char* path, const char* mode) {
#ifdef _WIN32
    const int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (len <= 0) return nullptr;
    std::wstring widePath(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath.data(), len);
    if (!widePath.empty() && widePath.back() == L'\0') widePath.pop_back();

    wchar_t wideMode[4] = {0};
    for (int i = 0; i < 3 && mode[i] != '\0'; ++i) {
        wideMode[i] = static_cast<wchar_t>(mode[i]);
    }
    return _wfopen(widePath.c_str(), wideMode);
#else
    return std::fopen(path, mode);
#endif
}

/// 读取整个文件
bool readFile(const char* path, std::vector<uint8_t>& out, std::string& error) {
    FILE* fp = openFileUtf8(path, "rb");
    if (!fp) {
        error = "无法打开输入文件";
        return false;
    }
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    if (size <= 0) {
        std::fclose(fp);
        error = "输入文件为空";
        return false;
    }
    std::fseek(fp, 0, SEEK_SET);

    out.resize(static_cast<size_t>(size));
    const size_t read = std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    if (read != out.size()) {
        error = "读取输入文件失败";
        return false;
    }
    return true;
}

/// 写出标准 3DGS PLY
///
/// 字段顺序与 3DGS 训练脚本导出一致；f_rest 为「点内通道优先」
/// （每点先排通道 0 的 15 个系数，再通道 1、通道 2）。
bool writePly(const char* path, const gsct::SplatData& d, std::string& error) {
    FILE* fp = openFileUtf8(path, "wb");
    if (!fp) {
        error = "无法创建输出文件";
        return false;
    }

    const int shCoef = d.shDegree > 0 ? (d.shDegree + 1) * (d.shDegree + 1) - 1 : 0;
    const int restCount = shCoef * 3;
    const size_t perPoint = 17 + static_cast<size_t>(restCount);  // 每点 float 数

    std::string header;
    header.reserve(2048);
    header += "ply\n";
    header += "format binary_little_endian 1.0\n";
    header += "element vertex " + std::to_string(d.numPoints) + "\n";
    header += "property float x\nproperty float y\nproperty float z\n";
    header += "property float nx\nproperty float ny\nproperty float nz\n";
    header += "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n";
    for (int i = 0; i < restCount; ++i) {
        header += "property float f_rest_" + std::to_string(i) + "\n";
    }
    header += "property float opacity\n";
    header += "property float scale_0\nproperty float scale_1\nproperty float scale_2\n";
    header += "property float rot_0\nproperty float rot_1\nproperty float rot_2\n";
    header += "property float rot_3\n";
    header += "end_header\n";

    if (std::fwrite(header.data(), 1, header.size(), fp) != header.size()) {
        std::fclose(fp);
        error = "写出 PLY 头部失败";
        return false;
    }

    // 分块组装后整块写出，避免为整份数据额外分配上百 MB 的连续缓冲
    constexpr uint32_t kBatch = 16384;
    std::vector<float> buf(static_cast<size_t>(kBatch) * perPoint);

    for (uint32_t start = 0; start < d.numPoints; start += kBatch) {
        const uint32_t count = std::min<uint32_t>(kBatch, d.numPoints - start);
        float* p = buf.data();

        for (uint32_t i = 0; i < count; ++i) {
            const size_t idx = start + i;

            *p++ = d.position[idx * 3 + 0];
            *p++ = d.position[idx * 3 + 1];
            *p++ = d.position[idx * 3 + 2];

            *p++ = 0.0f;  // nx
            *p++ = 0.0f;  // ny
            *p++ = 0.0f;  // nz

            *p++ = d.color[idx * 3 + 0];
            *p++ = d.color[idx * 3 + 1];
            *p++ = d.color[idx * 3 + 2];

            // SHN 源布局为「系数优先」，此处重排为 PLY 的「点内通道优先」
            for (int c = 0; c < 3; ++c) {
                for (int k = 0; k < shCoef; ++k) {
                    *p++ = d.shn[(static_cast<size_t>(k) * d.numPoints + idx) * 3 + c];
                }
            }

            *p++ = d.opacity[idx];

            *p++ = d.scaling[idx * 3 + 0];
            *p++ = d.scaling[idx * 3 + 1];
            *p++ = d.scaling[idx * 3 + 2];

            *p++ = d.rotation[idx * 4 + 0];
            *p++ = d.rotation[idx * 4 + 1];
            *p++ = d.rotation[idx * 4 + 2];
            *p++ = d.rotation[idx * 4 + 3];
        }

        const size_t bytes = count * perPoint * sizeof(float);
        if (std::fwrite(buf.data(), 1, bytes, fp) != bytes) {
            std::fclose(fp);
            error = "写出 PLY 数据失败（磁盘空间不足？）";
            return false;
        }
    }

    std::fclose(fp);
    return true;
}

/// 由内部错误文本推断状态码
int classify(const std::string& error) {
    if (error.find("zlib") != std::string::npos) return GSCT_ERR_DECOMPRESS;
    if (error.find("ASTC") != std::string::npos) return GSCT_ERR_ASTC;
    if (error.find("不支持") != std::string::npos) return GSCT_ERR_UNSUPPORTED;
    if (error.find("文件") != std::string::npos) return GSCT_ERR_IO;
    return GSCT_ERR_FORMAT;
}

}  // namespace

extern "C" {

int gsct_probe(const char* input_path, gsct_info* out_info) {
    if (!input_path || !out_info) {
        setError("参数为空");
        return GSCT_ERR_INTERNAL;
    }
    std::memset(out_info, 0, sizeof(gsct_info));
    out_info->sh_degree = -1;

    std::vector<uint8_t> data;
    std::string error;
    if (!readFile(input_path, data, error)) {
        setError(error);
        return GSCT_ERR_IO;
    }

    gsct::StreamHeader header{};
    std::vector<gsct::AttributeMeta> attributes;
    if (!gsct::parseHeader(data.data(), data.size(), header, attributes, error)) {
        setError(error);
        return classify(error);
    }

    out_info->num_points = header.numPoints;
    out_info->num_attributes = header.numAttribute;
    out_info->version_major = header.version[0];
    out_info->version_minor = header.version[1];
    out_info->version_patch = header.version[2];

    for (const auto& a : attributes) {
        if (a.attributeType == gsct::ATTR_SHN) {
            out_info->sh_degree = static_cast<int32_t>(
                std::sqrt(static_cast<double>(a.componentsCount) / 3.0 + 1.0)) - 1;
            break;
        }
    }
    return GSCT_OK;
}

int gsct_decode_to_ply(const char* input_path, const char* output_path) {
    if (!input_path || !output_path) {
        setError("参数为空");
        return GSCT_ERR_INTERNAL;
    }

    std::vector<uint8_t> data;
    std::string error;
    if (!readFile(input_path, data, error)) {
        setError(error);
        return GSCT_ERR_IO;
    }

    gsct::SplatData splat;
    try {
        if (!gsct::decode(data.data(), data.size(), splat, error)) {
            setError(error);
            return classify(error);
        }
    } catch (const std::bad_alloc&) {
        setError("内存不足");
        return GSCT_ERR_INTERNAL;
    } catch (const std::exception& e) {
        setError(std::string("解码异常: ") + e.what());
        return GSCT_ERR_INTERNAL;
    }

    if (!writePly(output_path, splat, error)) {
        setError(error);
        return GSCT_ERR_IO;
    }
    return GSCT_OK;
}

const char* gsct_last_error(void) { return g_lastError.c_str(); }

}  // extern "C"
