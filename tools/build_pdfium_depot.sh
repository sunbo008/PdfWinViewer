#!/bin/bash
# PDFium 简化构建脚本 - 使用 depot_tools
# 解决复杂依赖问题的最佳方案

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() { echo -e "${BLUE}ℹ️  $1${NC}"; }
print_success() { echo -e "${GREEN}✅ $1${NC}"; }
print_warning() { echo -e "${YELLOW}⚠️  $1${NC}"; }
print_error() { echo -e "${RED}❌ $1${NC}"; }

echo ""
echo "🚀 PDFium 简化构建脚本（使用 depot_tools）"
echo "============================================"

# 项目路径
PROJECT_ROOT="/Users/wps/work/WorkSpace/github/PdfWinViewer"
PDFIUM_DIR="${PROJECT_ROOT}/third_party/pdfium"
DEPOT_TOOLS="/Users/wps/work/WorkSpace/depot_tools"
TARGET_LIB="${PDFIUM_DIR}/out/Debug/obj/libpdfium.a"

print_info "项目根目录: ${PROJECT_ROOT}"
print_info "PDFium 目录: ${PDFIUM_DIR}"
print_info "depot_tools: ${DEPOT_TOOLS}"
print_info "目标库文件: ${TARGET_LIB}"
echo ""

# 步骤1: 验证环境
print_info "步骤 1/6: 验证环境..."

if [ ! -d "$DEPOT_TOOLS" ]; then
    print_error "depot_tools 目录不存在: $DEPOT_TOOLS"
    exit 1
fi

if [ ! -d "$PDFIUM_DIR" ]; then
    print_error "PDFium 目录不存在: $PDFIUM_DIR"
    exit 1
fi

if [ ! -f "$PDFIUM_DIR/BUILD.gn" ]; then
    print_error "PDFium BUILD.gn 文件不存在"
    exit 1
fi

print_success "环境验证通过"

# 步骤2: 清理旧构建文件
print_info "步骤 2/6: 清理旧构建文件..."

# 清理构建目录
rm -rf "${PROJECT_ROOT}/build/pdfium_external"
rm -rf "${PDFIUM_DIR}/out"

# 清理临时文件
rm -f "${PROJECT_ROOT}/third_party/.gclient"
rm -f "${PROJECT_ROOT}/third_party/.gclient_entries"

print_success "清理完成"

# 步骤3: 设置环境变量
print_info "步骤 3/6: 设置环境变量..."

export PATH="${DEPOT_TOOLS}:$PATH"
export DEPOT_TOOLS_UPDATE=0
export DEPOT_TOOLS_WIN_TOOLCHAIN=0

print_success "环境变量设置完成"

# 步骤4: 创建最小 depot_tools 环境
print_info "步骤 4/6: 创建最小 depot_tools 环境..."

cd "${PROJECT_ROOT}/third_party"

# 创建最小的 .gclient 文件
cat > .gclient << 'EOF'
solutions = []
EOF

# 创建空的 .gclient_entries 文件
touch .gclient_entries

print_success "depot_tools 环境创建完成"

# 步骤5: 配置 PDFium
print_info "步骤 5/6: 配置 PDFium 构建..."

cd pdfium

# 创建构建目录
mkdir -p out/Debug

# 创建 args.gn 配置文件
cat > out/Debug/args.gn << 'EOF'
# PDFium 静态库构建配置
is_debug = true
symbol_level = 2
is_official_build = false

# 静态库配置
is_component_build = false
pdf_is_standalone = true
pdf_is_complete_lib = true
use_static_libs = true

# 禁用复杂依赖
use_custom_libcxx = false
use_system_freetype = false
use_system_libjpeg = false
use_system_libpng = false
use_system_zlib = false

# 功能配置 - 禁用复杂功能
pdf_use_skia = false
pdf_enable_xfa = false
pdf_enable_v8 = false
pdf_use_partition_alloc = false

# 平台配置
target_os = "mac"
target_cpu = "arm64"

# 静态库打包
pdf_bundle_freetype = true
pdf_bundle_libjpeg = true
pdf_bundle_libpng = true
pdf_bundle_zlib = true
pdf_bundle_lcms2 = true
pdf_bundle_libopenjpeg2 = true

# 编译器配置
treat_warnings_as_errors = false
use_thin_lto = false
use_lld = false
EOF

print_success "PDFium 配置完成"

# 步骤6: 执行构建
print_info "步骤 6/6: 开始构建 PDFium..."

# 尝试生成构建文件
print_info "生成构建文件..."
if "${DEPOT_TOOLS}/gn" gen out/Debug; then
    print_success "构建文件生成成功"
else
    print_warning "标准 GN 失败，尝试其他方法..."
    
    # 如果失败，尝试强制跳过检查
    print_info "尝试跳过 checkout 检查..."
    
    # 创建假的 DEPS 文件（如果不存在）
    if [ ! -f "DEPS" ]; then
        echo "# Minimal DEPS file for external build" > DEPS.backup
    fi
    
    # 再次尝试
    if ! "${DEPOT_TOOLS}/gn" gen out/Debug; then
        print_error "GN 生成失败，请检查 depot_tools 配置"
        exit 1
    fi
fi

# 执行构建
print_info "开始编译（预计 5-15 分钟）..."
"${DEPOT_TOOLS}/ninja" -C out/Debug pdfium

# 验证结果
if [ -f "$TARGET_LIB" ]; then
    FILE_SIZE=$(stat -f%z "$TARGET_LIB" 2>/dev/null || stat -c%s "$TARGET_LIB")
    FILE_SIZE_MB=$((FILE_SIZE / 1024 / 1024))
    
    echo ""
    print_success "🎉 PDFium 构建成功！"
    print_info "库文件: $TARGET_LIB"
    print_info "大小: ${FILE_SIZE_MB}MB"
    
    # 检查符号
    if nm "$TARGET_LIB" | grep -q "FPDF_InitLibrary"; then
        print_success "符号表验证通过"
    else
        print_warning "符号表可能不完整"
    fi
    
    echo ""
    print_info "现在可以构建主项目了:"
    echo "  cd ${PROJECT_ROOT}"
    echo "  mkdir -p build && cd build"
    echo "  cmake .. -DCMAKE_BUILD_TYPE=Debug \\"
    echo "           -DPDFIUM_STATIC=\"$TARGET_LIB\""
    echo "  cmake --build . --config Debug"
    
else
    print_error "构建失败，静态库文件未生成"
    print_info "请检查上面的错误信息"
    exit 1
fi

echo ""
print_success "🎉 PDFium 构建完成！"

