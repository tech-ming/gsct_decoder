#
# gsct_decoder 的 iOS 构建配置。
#
# 直接以相对路径引用 ../src 下的 C/C++ 源码（CocoaPods 支持相对 podspec 的路径），
# 避免多文件 C++ 项目使用 forwarder 文件时需要逐文件转发的麻烦。
#
Pod::Spec.new do |s|
  s.name             = 'gsct_decoder'
  # 与 pubspec.yaml 的 version 保持一致，发版时同步修改
  s.version          = '0.1.0'
  s.summary          = 'HarmonyOS 3D photo (gsct) decoder for Flutter'
  s.description      = <<-DESC
Decodes the gsct compressed 3DGS stream embedded in HarmonyOS Remy 3D photos
into a standard 3DGS PLY. Bundles astc-encoder and zlib; no system dependencies.
                       DESC
  s.homepage         = 'https://github.com/tech-ming/gsct_decoder'
  s.license          = { :file => '../LICENSE' }
  s.author           = 'tech-ming'

  s.source           = { :path => '.' }
  s.source_files     = '../src/**/*.{h,hpp,c,cpp}'
  # 命令行工具含 main 函数，不能编入库
  s.exclude_files    = '../src/gsct_cli.cpp'

  s.dependency 'Flutter'
  s.platform         = :ios, '12.0'

  s.library          = 'c++'

  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'DEFINES_MODULE' => 'YES',
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) ASTCENC_DECOMPRESS_ONLY=1 ZLIB_CONST=1',
    'HEADER_SEARCH_PATHS' => '"${PODS_TARGET_SRCROOT}/../src" "${PODS_TARGET_SRCROOT}/../src/third_party/zlib" "${PODS_TARGET_SRCROOT}/../src/third_party/astc-encoder"',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386'
  }
  s.swift_version = '5.0'
end
