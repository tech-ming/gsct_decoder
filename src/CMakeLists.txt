# Copyright (c) 2026. SPDX-License-Identifier: MIT
#
# gsct_decoder 原生库构建入口。
#
# 被三种构建系统共同引用：
#   - Android：android/build.gradle 的 externalNativeBuild.cmake
#   - OHOS   ：ohos/build-profile.json5 的 externalNativeOptions
#   - iOS    ：ios/gsct_decoder.podspec 的 source_files
# 桌面验证：cmake -S src -B build -DGSCT_BUILD_CLI=ON -DGSCT_LIB_TYPE=STATIC

cmake_minimum_required(VERSION 3.10)

project(gsct_decoder_library VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(GSCT_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(GSCT_THIRD_PARTY ${GSCT_SRC}/third_party)

# 库类型：插件构建用 SHARED，桌面验证可传 STATIC 免去 DLL 依赖
if(NOT DEFINED GSCT_LIB_TYPE)
    set(GSCT_LIB_TYPE SHARED)
endif()

# ---------------- zlib（仅取 inflate 所需文件） ----------------
set(ZLIB_SOURCES
    ${GSCT_THIRD_PARTY}/zlib/adler32.c
    ${GSCT_THIRD_PARTY}/zlib/compress.c
    ${GSCT_THIRD_PARTY}/zlib/crc32.c
    ${GSCT_THIRD_PARTY}/zlib/deflate.c
    ${GSCT_THIRD_PARTY}/zlib/infback.c
    ${GSCT_THIRD_PARTY}/zlib/inffast.c
    ${GSCT_THIRD_PARTY}/zlib/inflate.c
    ${GSCT_THIRD_PARTY}/zlib/inftrees.c
    ${GSCT_THIRD_PARTY}/zlib/trees.c
    ${GSCT_THIRD_PARTY}/zlib/uncompr.c
    ${GSCT_THIRD_PARTY}/zlib/zutil.c
)

# ---------------- astc-encoder（仅解码路径） ----------------
file(GLOB ASTC_SOURCES ${GSCT_THIRD_PARTY}/astc-encoder/*.cpp)

add_library(gsct_decoder ${GSCT_LIB_TYPE}
    ${GSCT_SRC}/gsct_decoder.cpp
    ${GSCT_SRC}/gsct_api.cpp
    ${ZLIB_SOURCES}
    ${ASTC_SOURCES}
)

target_include_directories(gsct_decoder
    PUBLIC ${GSCT_SRC}
    PRIVATE
        ${GSCT_THIRD_PARTY}/zlib
        ${GSCT_THIRD_PARTY}/astc-encoder
)

# ASTCENC_DECOMPRESS_ONLY 裁掉编码路径（体积减少约 180KB）；
# 该宏要求 context 创建时必须带 ASTCENC_FLG_DECOMPRESS_ONLY，见 gsct_decoder.cpp。
target_compile_definitions(gsct_decoder PRIVATE
    ASTCENC_DECOMPRESS_ONLY
    ZLIB_CONST
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gsct_decoder PRIVATE
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
        -Wno-unused-parameter
    )
    if(NOT CMAKE_BUILD_TYPE MATCHES Debug)
        target_compile_options(gsct_decoder PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-O3>)
    endif()
endif()

if(MSVC)
    target_compile_options(gsct_decoder PRIVATE /utf-8 /O2)
endif()

# Android 16KB 页对齐（Android 15+ 要求）
if(ANDROID)
    target_link_options(gsct_decoder PRIVATE "-Wl,-z,max-page-size=16384")
endif()

# 桌面平台需要显式链接线程库；Android/iOS/OHOS 的 pthread 在 libc 内
if(UNIX AND NOT ANDROID AND NOT OHOS AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(gsct_decoder PRIVATE Threads::Threads)
endif()

# ---------------- 桌面验证程序 ----------------
option(GSCT_BUILD_CLI "构建命令行验证程序" OFF)
if(GSCT_BUILD_CLI)
    add_executable(gsct_cli ${GSCT_SRC}/gsct_cli.cpp)
    target_link_libraries(gsct_cli PRIVATE gsct_decoder)
    if(MSVC)
        target_compile_options(gsct_cli PRIVATE /utf-8)
    endif()
endif()
