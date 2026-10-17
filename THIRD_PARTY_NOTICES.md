# 第三方组件声明

本包自有代码采用 MIT License（见 LICENSE）。以下第三方组件继续适用其原始许可，
不因本包采用 MIT License 而被重新许可。

## astc-encoder

- 位置：`src/third_party/astc-encoder/`
- 来源：https://github.com/ARM-software/astc-encoder（commit `30aabb3f42406df45a910d8496f9bee17eeba9bb`）
- 许可：Apache License 2.0（见 `src/third_party/astc-encoder/LICENSE.txt`）
- 版权：Copyright 2020-2025 Arm Limited
- 用途：SH（球谐高阶）系数的 ASTC 纹理解码

## zlib

- 位置：`src/third_party/zlib/`
- 来源：https://github.com/madler/zlib（commit `e3dc0a85b7032e98380dec011bc8f2c2ee0d8fca`，版本 1.3.2.1）
- 许可：zlib License（见 `src/third_party/zlib/LICENSE`）
- 版权：Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler
- 用途：属性数据流的 zlib 解压

## spatial-vivid-toolkit（解码逻辑移植来源）

- 来源：https://gitcode.com/UHD-World-Association/spatial-vivid-toolkit
  （`packages/gltf/src/gsdecoder.cpp`、`packages/gltf/include/gsdecoder.h`）
- 许可：MIT License
- 版权：Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
- 用途：gsct 码流格式解析、属性逆量化、ASTC 像素重排逻辑

移植时的改动（相对上游实现）：

1. 移除未使用的 zstd / happly / glm 依赖；
2. 结构体字段由 `size_t` 改为 `uint32_t`（码流字段本就是 32 位，
   原写法依赖「高 32 位恰为 0」的隐式假设）；
3. 去掉文件 I/O 分支，统一为内存输入 / PLY 文件输出；
4. 增加 C API 导出与错误码体系；
5. 修正 ASTC 输出缓冲按单系数带分配的越界问题（上游按整张纹理使用该缓冲）。

该组件的 MIT 许可全文：

```
MIT License

Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
