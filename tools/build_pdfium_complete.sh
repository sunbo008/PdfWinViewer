#!/bin/bash
# PDFium 完整构建脚本 - 从下载到构建完成
# 使用系统 libc++ 避免符号冲突

set -e  # 遇到错误立即退出

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"

echo "🚀 PDFium 完整构建脚本"
echo "项目根目录: $PROJECT_ROOT"
echo "第三方库目录: $THIRD_PARTY_DIR"

# 检查 depot_tools
if ! command -v fetch &> /dev/null; then
    echo "❌ 错误: depot_tools 未安装或未添加到 PATH"
    echo "请先安装 depot_tools:"
    echo "  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git"
    echo "  export PATH=\$PATH:/path/to/depot_tools"
    exit 1
fi

echo "✅ depot_tools 已安装"

# 创建第三方库目录
mkdir -p "$THIRD_PARTY_DIR"
cd "$THIRD_PARTY_DIR"

# 检查是否已存在 PDFium
if [ -d "pdfium" ]; then
    echo "📁 PDFium 目录已存在，跳过下载"
else
    echo "📥 下载 PDFium 源码..."
    gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git
fi

# 同步依赖
echo "🔄 同步 PDFium 依赖..."
gclient sync -v --nohooks
cd pdfium

# 创建构建配置
echo "⚙️  配置 PDFium 构建参数..."
mkdir -p out/Debug

cat > out/Debug/args.gn << 'EOF'
# PDFium 静态库构建配置 - 使用系统 libc++

# 调试友好的 Release 配置
is_debug = false  # Release 模式，避免断言和链接问题
symbol_level = 2  # 包含完整调试符号
is_official_build = false

# 调试优化配置
strip_debug_info = false      # 保留调试信息
use_debug_fission = false     # 不分离调试信息
enable_full_stack_frames_for_profiling = true  # 完整栈帧信息

# 编译器调试设置
use_thin_lto = false         # 禁用 LTO，保留调试信息
optimize_for_size = false    # 不为大小优化

# 静态库配置 - 核心要求
is_component_build = false
pdf_is_standalone = true
pdf_is_complete_lib = true
use_static_libs = true

# 使用系统 libc++ 避免符号冲突
use_custom_libcxx = false    # 关键：使用系统 libc++

# 功能配置 - 简化依赖
pdf_use_skia = false
pdf_enable_xfa = true
pdf_enable_v8 = true
pdf_use_partition_alloc = false

# 平台配置
target_os = "mac"
target_cpu = "arm64"

# 强制静态库打包 - 关键配置
pdf_bundle_freetype = true
pdf_bundle_libjpeg = true
pdf_bundle_libpng = true
pdf_bundle_zlib = true
pdf_bundle_libopenjpeg2 = true

# 编译器配置
treat_warnings_as_errors = false
EOF

echo "✅ 构建配置已创建"

# 生成构建文件
echo "🔧 生成构建文件..."
./buildtools/mac/gn gen out/Debug

# 构建 PDFium
echo "🔨 构建 PDFium 静态库..."
ninja -C out/Debug pdfium

# 验证构建结果
PDFIUM_LIB="out/Debug/obj/libpdfium.a"
if [ -f "$PDFIUM_LIB" ]; then
    LIB_SIZE=$(stat -f%z "$PDFIUM_LIB" 2>/dev/null || echo "unknown")
    echo "✅ PDFium 静态库构建成功!"
    echo "   文件: $PDFIUM_LIB"
    echo "   大小: $LIB_SIZE bytes"
else
    echo "❌ PDFium 静态库构建失败!"
    exit 1
fi

# 返回项目根目录
cd "$PROJECT_ROOT"

# 构建主项目
echo "🏗️  构建主项目..."
mkdir -p build
cd build

# 配置 CMake
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 构建项目
cmake --build . --config Debug

echo ""
echo "🎉 构建完成!"
echo "📍 PDFium 静态库: $THIRD_PARTY_DIR/pdfium/$PDFIUM_LIB"
echo "📍 主项目可执行文件: $PROJECT_ROOT/build/"
echo ""
echo "💡 下次构建只需运行:"
echo "   cd $THIRD_PARTY_DIR/pdfium && ninja -C out/Debug pdfium"
echo "   cd $PROJECT_ROOT/build && cmake --build . --config Debug"
