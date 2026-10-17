// Copyright (c) 2026. SPDX-License-Identifier: MIT
//
// gsct 解码器对外 C 接口。
// 供 Flutter FFI 调用，也可被任意 C/C++ 宿主集成。

#ifndef GSCT_API_H
#define GSCT_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define GSCT_API __declspec(dllexport)
#else
#define GSCT_API __attribute__((visibility("default")))
#endif

/// 状态码
typedef enum {
    GSCT_OK = 0,            ///< 成功
    GSCT_ERR_IO = 1,        ///< 文件读写失败
    GSCT_ERR_FORMAT = 2,    ///< 码流格式非法
    GSCT_ERR_DECOMPRESS = 3,///< zlib 解压失败
    GSCT_ERR_ASTC = 4,      ///< ASTC 纹理解码失败
    GSCT_ERR_UNSUPPORTED = 5,///< 遇到不支持的编码方案
    GSCT_ERR_INTERNAL = 6,  ///< 内部错误（内存分配等）
} gsct_status;

/// 码流探测结果
typedef struct {
    uint32_t num_points;     ///< 高斯点总数
    uint8_t num_attributes;  ///< 属性个数
    uint8_t version_major;   ///< 码流版本（主）
    uint8_t version_minor;   ///< 码流版本（次）
    uint8_t version_patch;   ///< 码流版本（修订）
    int32_t sh_degree;       ///< SH 阶数；-1 表示不含高阶 SH
} gsct_info;

/// 探测 gsct 码流（只读头部与属性元数据，不解压）
///
/// [input_path] 码流文件路径（UTF-8）
/// [out_info]   探测结果输出
///
/// 返回 [GSCT_OK] 或错误码。
GSCT_API int gsct_probe(const char* input_path, gsct_info* out_info);

/// 解码 gsct 码流并写出标准 3DGS PLY
///
/// 输出为 binary_little_endian，字段与 3DGS 训练脚本导出的 PLY 一致
/// （x/y/z、nx/ny/nz、f_dc_0..2、f_rest_0..44、opacity、scale_0..2、rot_0..3），
/// 可直接被 gsplat.js 等标准渲染器加载。
///
/// [input_path]  gsct 码流文件路径（UTF-8）
/// [output_path] 输出 PLY 文件路径（UTF-8）
///
/// 返回 [GSCT_OK] 或错误码。
GSCT_API int gsct_decode_to_ply(const char* input_path, const char* output_path);

/// 取最近一次错误描述（UTF-8，线程局部）
///
/// 返回的字符串在下一次调用本库任意接口前有效，调用方不应释放。
GSCT_API const char* gsct_last_error(void);

#ifdef __cplusplus
}
#endif

#endif  // GSCT_API_H
