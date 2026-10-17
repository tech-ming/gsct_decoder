// Copyright (c) 2026. SPDX-License-Identifier: MIT
//
// 桌面验证用命令行工具：gsct_cli <probe|decode> <输入> [输出]

#include <cstdio>
#include <cstring>

#include "gsct_api.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "用法:\n"
                     "  gsct_cli probe  <input.gsct>\n"
                     "  gsct_cli decode <input.gsct> <output.ply>\n");
        return 1;
    }

    const char* cmd = argv[1];

    if (std::strcmp(cmd, "probe") == 0) {
        gsct_info info{};
        const int rc = gsct_probe(argv[2], &info);
        if (rc != GSCT_OK) {
            std::fprintf(stderr, "探测失败(%d): %s\n", rc, gsct_last_error());
            return rc;
        }
        std::printf("点数: %u\n属性数: %u\n版本: %u.%u.%u\nSH 阶数: %d\n",
                    info.num_points, info.num_attributes, info.version_major,
                    info.version_minor, info.version_patch, info.sh_degree);
        return 0;
    }

    if (std::strcmp(cmd, "decode") == 0) {
        if (argc < 4) {
            std::fprintf(stderr, "decode 需要输出路径\n");
            return 1;
        }
        const int rc = gsct_decode_to_ply(argv[2], argv[3]);
        if (rc != GSCT_OK) {
            std::fprintf(stderr, "解码失败(%d): %s\n", rc, gsct_last_error());
            return rc;
        }
        std::printf("解码完成: %s\n", argv[3]);
        return 0;
    }

    std::fprintf(stderr, "未知命令: %s\n", cmd);
    return 1;
}
