#!/usr/bin/env bash
# 构建 OHOS 平台的原生库并放入 ohos/libs/<abi>/
#
# 为什么需要本脚本（而不是交给 hvigor 自动编译）：
# hvigor 的 BuildNativeWithCmake / BuildNativeWithNinja 只在**模块源码集内**
# 存在 C/C++ 文件时才执行（见 hvigor-ohos-plugin 的 CmakeUtil.nativeTaskCondition），
# 而本插件的 C++ 源码与 Android / iOS / 桌面共用、位于模块外的 src/，
# ohos 模块内没有任何 .cpp —— 任务会被静默跳过（无报错，也不产出 .so）。
# 故 OHOS 侧走「预编译库」路线：产物提交在 ohos/libs/<abi>/，
# 由 hvigor 的 ProcessLibs 任务随 har 打包（本项目 sqlcipher_flutter_libs 同款做法）。
#
# 用法：bash ohos/build_libs.sh
# 环境变量 HOS_SDK 可覆盖 SDK native 目录（默认 DevEco Studio 安装位置）

set -euo pipefail

NATIVE_SDK="${HOS_SDK:-E:/DevEco Studio/sdk/default/openharmony/native}"
CMAKE="$NATIVE_SDK/build-tools/cmake/bin/cmake"
TOOLCHAIN="$NATIVE_SDK/build/cmake/ohos.toolchain.cmake"

if [ ! -x "$CMAKE" ]; then
  echo "找不到 cmake：$CMAKE" >&2
  echo "请用 HOS_SDK 指定 SDK native 目录（例如 HOS_SDK=/d/DevEco/sdk/default/openharmony/native）" >&2
  exit 1
fi

PKG_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$PKG_DIR/src"
LIBS_DIR="$PKG_DIR/ohos/libs"

# 让 ninja 可被 cmake 找到（SDK 自带，不依赖系统安装）
export PATH="$NATIVE_SDK/build-tools/cmake/bin:$PATH"

# OHOS_STL=c++_static：hap 内不存在 libc++_shared.so（libflutter.so 与 libsqlcipher.so
# 也都只依赖 libc.so），动态链接 STL 会导致运行时加载失败
for ABI in arm64-v8a armeabi-v7a x86_64; do
  BUILD_DIR="$PKG_DIR/build/ohos_$ABI"
  echo "== 构建 $ABI ==" >&2
  "$CMAKE" -G Ninja -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DOHOS_ARCH="$ABI" \
    -DOHOS_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release >/dev/null
  "$CMAKE" --build "$BUILD_DIR" >/dev/null

  mkdir -p "$LIBS_DIR/$ABI"
  cp "$BUILD_DIR/libgsct_decoder.so" "$LIBS_DIR/$ABI/"
  echo "== 已输出 $LIBS_DIR/$ABI/libgsct_decoder.so ==" >&2
done
