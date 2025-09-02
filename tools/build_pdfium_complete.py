#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PDFium 完整构建脚本 (Python版本)
带配置选择功能，使用系统 libc++ 避免符号冲突
"""

import os
import sys
import subprocess
import platform
import shutil
from pathlib import Path
from typing import Tuple, Optional
import argparse
import logging

# 颜色输出类
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    CYAN = '\033[0;36m'
    NC = '\033[0m'  # No Color
    
    @classmethod
    def colored(cls, text: str, color: str) -> str:
        """返回带颜色的文本"""
        return f"{color}{text}{cls.NC}"

# 日志输出类
class Logger:
    @staticmethod
    def info(message: str):
        print(Colors.colored(f"ℹ️  {message}", Colors.BLUE))
    
    @staticmethod
    def success(message: str):
        print(Colors.colored(f"✅ {message}", Colors.GREEN))
    
    @staticmethod
    def warning(message: str):
        print(Colors.colored(f"⚠️  {message}", Colors.YELLOW))
    
    @staticmethod
    def error(message: str):
        print(Colors.colored(f"❌ {message}", Colors.RED))
    
    @staticmethod
    def config(message: str):
        print(Colors.colored(f"⚙️  {message}", Colors.CYAN))

# 构建配置类
class BuildConfig:
    def __init__(self):
        self.build_type = "Debug"
        self.enable_v8 = True
        self.enable_xfa = True
        self.target_os = self._detect_os()
        self.target_cpu = self._detect_cpu()
    
    def _detect_os(self) -> str:
        """自动检测操作系统"""
        system = platform.system().lower()
        if system == "darwin":
            return "mac"
        elif system == "linux":
            return "linux"
        elif system in ["windows", "cygwin"]:
            return "win"
        else:
            Logger.error(f"不支持的操作系统: {system}")
            sys.exit(1)
    
    def _detect_cpu(self) -> str:
        """自动检测CPU架构"""
        machine = platform.machine().lower()
        if machine in ["x86_64", "amd64"]:
            return "x64"
        elif machine in ["arm64", "aarch64"]:
            return "arm64"
        else:
            Logger.warning(f"未知架构 {machine}，默认使用 x64")
            return "x64"
    
    def get_estimated_size(self) -> str:
        """获取预估库大小"""
        if self.build_type == "Debug":
            if self.enable_v8 and self.enable_xfa:
                return "~150-200MB"
            elif self.enable_v8 or self.enable_xfa:
                return "~100-150MB"
            else:
                return "~50-100MB"
        else:  # Release
            if self.enable_v8 and self.enable_xfa:
                return "~80-120MB"
            elif self.enable_v8 or self.enable_xfa:
                return "~50-80MB"
            else:
                return "~20-50MB"

# 主构建器类
class PDFiumBuilder:
    def __init__(self):
        # 路径设置
        self.script_dir = Path(__file__).parent.absolute()
        self.project_root = self.script_dir.parent
        self.third_party_dir = self.project_root / "third_party"
        self.pdfium_dir = self.third_party_dir / "pdfium"
        
        # 构建配置
        self.config = BuildConfig()
        
        Logger.info(f"检测到平台: {self.config.target_os} ({self.config.target_cpu})")
    
    def show_header(self):
        """显示脚本头部信息"""
        print()
        print("🚀 PDFium 完整构建脚本 (Python版本)")
        print(f"项目根目录: {self.project_root}")
        print(f"第三方库目录: {self.third_party_dir}")
        print()
    
    def show_feature_info(self):
        """显示功能说明"""
        print("📖 构建配置说明")
        print("=" * 44)
        print()
        print("🏗️  构建类型:")
        print("   • Debug   - 包含调试信息，便于开发调试 (~150-200MB)")
        print("   • Release - 优化构建，体积小性能好 (~50-120MB)")
        print()
        print("⚡ JavaScript 支持 (V8):")
        print("   • 启用 - 支持 PDF 中的 JavaScript 代码执行")
        print("   • 禁用 - 不支持 JavaScript，库体积减少 ~30-50%")
        print()
        print("📝 XFA 表单支持:")
        print("   • 启用 - 支持 Adobe XFA (XML Forms Architecture) 表单")
        print("   • 禁用 - 仅支持标准 PDF 表单，库体积减少 ~20-30%")
        print()
        print("💡 推荐配置:")
        print("   • 开发调试: Debug + V8 + XFA (完整功能)")
        print("   • 生产部署: Release + 按需选择 V8/XFA")
        print("   • 最小体积: Release + 禁用 V8 + 禁用 XFA")
        print()
    
    def check_build_status(self) -> Tuple[bool, bool, bool]:
        """检查构建状态"""
        debug_lib = self.pdfium_dir / "out" / "Debug" / "obj" / "libpdfium.a"
        release_lib = self.pdfium_dir / "out" / "Release" / "obj" / "libpdfium.a"
        main_app = self.project_root / "build" / "PdfWinViewer.app" / "Contents" / "MacOS" / "PdfWinViewer"
        
        return debug_lib.exists(), release_lib.exists(), main_app.exists()
    
    def show_menu(self) -> int:
        """显示选择菜单并获取用户选择"""
        has_debug, has_release, has_main = self.check_build_status()
        
        print()
        print("📋 构建选项菜单")
        print("=" * 44)
        print("1) 配置并完全构建 (推荐)")
        print("   🔧 可选择: Debug/Release + V8/XFA 功能")
        print("   📦 完整流程: 清理 → 配置 → 构建PDFium → 构建主项目")
        print()
        print("2) 使用默认配置快速构建")
        print("   ⚡ 默认配置: Debug + V8 + XFA (完整功能)")
        print("   🚀 适合: 快速测试和开发")
        print()
        print("3) 仅清理构建产物")
        print("   🧹 清理: build/ 和 pdfium/out/ 目录")
        print("   💡 用于: 解决构建问题或重新开始")
        print()
        print("4) 仅构建 PDFium 静态库")
        print("   🔧 可配置: Debug/Release + V8/XFA 选项")
        print("   📚 输出: libpdfium.a 静态库文件")
        print()
        print("5) 仅构建主项目")
        print("   🏗️  前提: 需要已存在的 PDFium 静态库")
        print("   📱 输出: PdfWinViewer.app 应用程序")
        print()
        print("6) 退出")
        print()
        
        # 显示当前构建状态
        if has_debug:
            try:
                lib_size = (self.pdfium_dir / "out" / "Debug" / "obj" / "libpdfium.a").stat().st_size
                lib_size_mb = lib_size // (1024 * 1024)
                Logger.success(f"Debug PDFium 静态库已存在 ({lib_size_mb}MB)")
            except:
                Logger.success("Debug PDFium 静态库已存在")
        
        if has_release:
            try:
                lib_size = (self.pdfium_dir / "out" / "Release" / "obj" / "libpdfium.a").stat().st_size
                lib_size_mb = lib_size // (1024 * 1024)
                Logger.success(f"Release PDFium 静态库已存在 ({lib_size_mb}MB)")
            except:
                Logger.success("Release PDFium 静态库已存在")
        
        if has_main:
            Logger.success("主项目可执行文件已存在")
        
        if not any([has_debug, has_release, has_main]):
            Logger.warning("未发现任何构建产物")
        
        print()
        while True:
            try:
                choice = int(input("请选择操作 (1-6): "))
                if 1 <= choice <= 6:
                    return choice
                else:
                    Logger.error("请输入 1-6 之间的数字")
            except ValueError:
                Logger.error("请输入有效的数字")
            except KeyboardInterrupt:
                print()
                Logger.info("用户取消操作")
                sys.exit(0)
    
    def choose_build_type(self):
        """选择构建类型"""
        print()
        Logger.config("选择构建类型:")
        print("1) Debug   - 包含调试信息，较慢但便于调试")
        print("2) Release - 优化构建，体积小速度快")
        print()
        
        while True:
            try:
                choice = input("请选择构建类型 (1-2) [默认: 1]: ").strip()
                if not choice:
                    choice = "1"
                
                if choice == "1":
                    self.config.build_type = "Debug"
                    Logger.success("选择构建类型: Debug")
                    break
                elif choice == "2":
                    self.config.build_type = "Release"
                    Logger.success("选择构建类型: Release")
                    break
                else:
                    Logger.error("请输入 1 或 2")
            except KeyboardInterrupt:
                print()
                Logger.info("用户取消操作")
                sys.exit(0)
    
    def choose_pdfium_features(self):
        """选择PDFium功能"""
        print()
        Logger.config("选择 PDFium 功能配置:")
        print()
        
        # V8 JavaScript 支持
        print("🔧 JavaScript 支持 (V8):")
        print("  启用: 支持 PDF 中的 JavaScript 代码执行")
        print("  禁用: 不支持 JavaScript，库体积更小")
        
        while True:
            try:
                choice = input("是否启用 V8 JavaScript 支持? (y/n) [默认: y]: ").strip().lower()
                if not choice:
                    choice = "y"
                
                if choice in ["y", "yes"]:
                    self.config.enable_v8 = True
                    Logger.success("启用 V8 JavaScript 支持")
                    break
                elif choice in ["n", "no"]:
                    self.config.enable_v8 = False
                    Logger.success("禁用 V8 JavaScript 支持")
                    break
                else:
                    Logger.error("请输入 y 或 n")
            except KeyboardInterrupt:
                print()
                Logger.info("用户取消操作")
                sys.exit(0)
        
        print()
        
        # XFA 表单支持
        print("📝 XFA 表单支持:")
        print("  启用: 支持 Adobe XFA (XML Forms Architecture) 表单")
        print("  禁用: 仅支持标准 PDF 表单，库体积更小")
        
        while True:
            try:
                choice = input("是否启用 XFA 表单支持? (y/n) [默认: y]: ").strip().lower()
                if not choice:
                    choice = "y"
                
                if choice in ["y", "yes"]:
                    self.config.enable_xfa = True
                    Logger.success("启用 XFA 表单支持")
                    break
                elif choice in ["n", "no"]:
                    self.config.enable_xfa = False
                    Logger.success("禁用 XFA 表单支持")
                    break
                else:
                    Logger.error("请输入 y 或 n")
            except KeyboardInterrupt:
                print()
                Logger.info("用户取消操作")
                sys.exit(0)
    
    def show_config_summary(self):
        """显示配置摘要"""
        print()
        Logger.config("构建配置摘要:")
        print("=" * 44)
        print(f"🏗️  构建类型: {self.config.build_type}")
        print(f"🖥️  目标平台: {self.config.target_os} ({self.config.target_cpu})")
        print(f"⚡ JavaScript (V8): {'启用' if self.config.enable_v8 else '禁用'}")
        print(f"📝 XFA 表单: {'启用' if self.config.enable_xfa else '禁用'}")
        print()
        
        estimated_size = self.config.get_estimated_size()
        Logger.info(f"预估库大小: {estimated_size}")
        print()
        
        while True:
            try:
                choice = input("确认使用此配置? (y/n) [默认: y]: ").strip().lower()
                if not choice:
                    choice = "y"
                
                if choice in ["y", "yes"]:
                    Logger.success("配置确认，开始构建...")
                    return
                elif choice in ["n", "no"]:
                    Logger.info("重新配置...")
                    self.choose_build_type()
                    self.choose_pdfium_features()
                    self.show_config_summary()
                    return
                else:
                    Logger.error("请输入 y 或 n")
            except KeyboardInterrupt:
                print()
                Logger.info("用户取消操作")
                sys.exit(0)
    
    def run_command(self, cmd: list, cwd: Optional[Path] = None, check: bool = True) -> subprocess.CompletedProcess:
        """运行命令"""
        Logger.info(f"执行命令: {' '.join(cmd)}")
        if cwd:
            Logger.info(f"工作目录: {cwd}")
        
        try:
            result = subprocess.run(
                cmd,
                cwd=cwd,
                check=check,
                capture_output=False,
                text=True
            )
            return result
        except subprocess.CalledProcessError as e:
            Logger.error(f"命令执行失败: {e}")
            if check:
                sys.exit(1)
            return e
        except FileNotFoundError:
            Logger.error(f"命令未找到: {cmd[0]}")
            sys.exit(1)
    
    def check_dependencies(self):
        """检查构建依赖"""
        Logger.info("检查构建依赖...")
        
        # 检查 depot_tools
        try:
            subprocess.run(["fetch", "--help"], capture_output=True, check=True)
            Logger.success("depot_tools 已安装")
        except (subprocess.CalledProcessError, FileNotFoundError):
            Logger.error("depot_tools 未安装或未添加到 PATH")
            print("请先安装 depot_tools:")
            print("  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git")
            print("  export PATH=$PATH:/path/to/depot_tools")
            sys.exit(1)
        
        # 检查 cmake
        try:
            subprocess.run(["cmake", "--version"], capture_output=True, check=True)
            Logger.success("CMake 已安装")
        except (subprocess.CalledProcessError, FileNotFoundError):
            Logger.error("CMake 未安装，请先安装 CMake")
            sys.exit(1)
    
    def clean_build_artifacts(self):
        """清理构建产物"""
        Logger.info("清理所有构建产物...")
        
        # 清理主项目构建目录
        build_dir = self.project_root / "build"
        if build_dir.exists():
            Logger.info("  删除 build/ 目录...")
            shutil.rmtree(build_dir)
        
        # 清理PDFium构建产物
        pdfium_out = self.pdfium_dir / "out"
        if pdfium_out.exists():
            Logger.info("  删除 PDFium out/ 目录...")
            shutil.rmtree(pdfium_out)
        
        # 清理CMake缓存文件
        Logger.info("  清理CMake缓存文件...")
        for pattern in ["CMakeCache.txt", "cmake_install.cmake"]:
            for file in self.project_root.rglob(pattern):
                file.unlink(missing_ok=True)
        
        for cmake_files_dir in self.project_root.rglob("CMakeFiles"):
            if cmake_files_dir.is_dir():
                shutil.rmtree(cmake_files_dir, ignore_errors=True)
        
        Logger.success("清理完成!")
    
    def generate_pdfium_config(self, build_dir: Path):
        """生成PDFium配置文件"""
        is_debug = "true" if self.config.build_type == "Debug" else "false"
        enable_v8 = "true" if self.config.enable_v8 else "false"
        enable_xfa = "true" if self.config.enable_xfa else "false"
        
        config_content = f"""# PDFium 静态库构建配置 - 使用系统 libc++
# 构建类型: {self.config.build_type}
# V8 支持: {enable_v8}
# XFA 支持: {enable_xfa}

# 基本构建配置
is_debug = {is_debug}
symbol_level = 2
is_official_build = false

# 优化配置
strip_debug_info = false
use_debug_fission = false
enable_full_stack_frames_for_profiling = true
use_thin_lto = false
optimize_for_size = false

# 静态库配置 - 核心要求
is_component_build = false
pdf_is_standalone = true
pdf_is_complete_lib = true
use_static_libs = true

# 使用系统 libc++ 避免符号冲突
use_custom_libcxx = false

# 功能配置
pdf_use_skia = false
pdf_enable_xfa = {enable_xfa}
pdf_enable_v8 = {enable_v8}
pdf_use_partition_alloc = false

# 平台配置
target_os = "{self.config.target_os}"
target_cpu = "{self.config.target_cpu}"

# 强制静态库打包 - 关键配置
pdf_bundle_freetype = true
pdf_bundle_libpng = true
pdf_bundle_zlib = true
pdf_bundle_libopenjpeg2 = true

# 编译器配置
treat_warnings_as_errors = false

# 强制使用 C++20 标准
use_cxx17 = false
use_cxx23 = false
# PDFium 默认使用 C++20，这里明确确保

# 忽略特定警告
extra_cflags = [
  "-Wno-ignored-attributes",
]
"""
        
        config_file = build_dir / "args.gn"
        config_file.write_text(config_content, encoding='utf-8')
        Logger.success(f"构建配置已创建 ({config_file})")
    
    def build_pdfium(self):
        """构建PDFium"""
        Logger.info("开始构建 PDFium...")
        
        self.check_dependencies()
        
        # 创建第三方库目录
        self.third_party_dir.mkdir(parents=True, exist_ok=True)
        
        # 检查是否已存在 PDFium
        if self.pdfium_dir.exists():
            Logger.info("PDFium 目录已存在，跳过下载")
        else:
            Logger.info("下载 PDFium 源码...")
            self.run_command(
                ["gclient", "config", "--unmanaged", "https://pdfium.googlesource.com/pdfium.git"],
                cwd=self.third_party_dir
            )
        
        # 同步依赖
        Logger.info("同步 PDFium 依赖...")
        self.run_command(
            ["gclient", "sync", "-v", "--nohooks"],
            cwd=self.third_party_dir
        )
        
        # 创建构建配置
        Logger.info("配置 PDFium 构建参数...")
        build_dir = self.pdfium_dir / "out" / self.config.build_type
        build_dir.mkdir(parents=True, exist_ok=True)
        
        self.generate_pdfium_config(build_dir)
        
        # 生成构建文件
        Logger.info("生成构建文件...")
        gn_path = self.pdfium_dir / "buildtools" / self.config.target_os / "gn"
        self.run_command([str(gn_path), "gen", str(build_dir)], cwd=self.pdfium_dir)
        
        # 构建 PDFium
        feature_desc = ""
        if self.config.enable_v8 and self.config.enable_xfa:
            feature_desc = " (含 V8 + XFA)"
        elif self.config.enable_v8:
            feature_desc = " (含 V8)"
        elif self.config.enable_xfa:
            feature_desc = " (含 XFA)"
        else:
            feature_desc = " (精简版)"
        
        Logger.info(f"构建 PDFium {self.config.build_type} 静态库{feature_desc}（预计需要 10-30 分钟）...")
        self.run_command(["ninja", "-C", str(build_dir), "pdfium"], cwd=self.pdfium_dir)
        
        # 验证构建结果
        pdfium_lib = build_dir / "obj" / "libpdfium.a"
        if pdfium_lib.exists():
            lib_size = pdfium_lib.stat().st_size
            lib_size_mb = lib_size // (1024 * 1024)
            Logger.success("PDFium 静态库构建成功!")
            Logger.info(f"   文件: {pdfium_lib}")
            Logger.info(f"   大小: {lib_size_mb}MB")
            Logger.info(f"   类型: {self.config.build_type}{feature_desc}")
        else:
            Logger.error("PDFium 静态库构建失败!")
            sys.exit(1)
    
    def build_main_project(self):
        """构建主项目"""
        Logger.info("开始构建主项目...")
        
        # 检查 PDFium 静态库是否存在
        pdfium_lib = self.pdfium_dir / "out" / self.config.build_type / "obj" / "libpdfium.a"
        if not pdfium_lib.exists():
            Logger.error(f"PDFium 静态库不存在: {pdfium_lib}")
            Logger.error("请先构建 PDFium")
            sys.exit(1)
        
        # 构建主项目
        Logger.info("配置主项目...")
        build_dir = self.project_root / "build"
        build_dir.mkdir(parents=True, exist_ok=True)
        
        # 配置 CMake
        Logger.info(f"使用 PDFium 库: {pdfium_lib}")
        self.run_command([
            "cmake", "..",
            f"-DCMAKE_BUILD_TYPE={self.config.build_type}",
            f"-DPDFIUM_STATIC={pdfium_lib}"
        ], cwd=build_dir)
        
        # 构建项目
        Logger.info("编译主项目...")
        self.run_command([
            "cmake", "--build", ".",
            "--config", self.config.build_type
        ], cwd=build_dir)
        
        Logger.success("主项目构建完成!")
    
    def detect_existing_pdfium_type(self) -> Optional[str]:
        """检测已存在的PDFium库类型"""
        debug_lib = self.pdfium_dir / "out" / "Debug" / "obj" / "libpdfium.a"
        release_lib = self.pdfium_dir / "out" / "Release" / "obj" / "libpdfium.a"
        
        if debug_lib.exists():
            Logger.info("检测到 Debug PDFium 库，使用 Debug 模式")
            return "Debug"
        elif release_lib.exists():
            Logger.info("检测到 Release PDFium 库，使用 Release 模式")
            return "Release"
        else:
            Logger.error("未找到 PDFium 静态库，请先构建 PDFium")
            return None
    
    def run(self):
        """主运行函数"""
        self.show_header()
        
        choice = self.show_menu()
        
        if choice == 1:
            Logger.info("选择：配置并完全构建")
            self.show_feature_info()
            self.choose_build_type()
            self.choose_pdfium_features()
            self.show_config_summary()
            self.clean_build_artifacts()
            self.build_pdfium()
            self.build_main_project()
        
        elif choice == 2:
            Logger.info("选择：使用默认配置快速构建")
            Logger.info("使用默认配置: Debug + V8 + XFA")
            self.build_pdfium()
            self.build_main_project()
        
        elif choice == 3:
            Logger.info("选择：仅清理构建产物")
            self.clean_build_artifacts()
            Logger.success("清理完成！")
            return
        
        elif choice == 4:
            Logger.info("选择：仅构建 PDFium 静态库")
            self.show_feature_info()
            self.choose_build_type()
            self.choose_pdfium_features()
            self.show_config_summary()
            self.build_pdfium()
        
        elif choice == 5:
            Logger.info("选择：仅构建主项目")
            build_type = self.detect_existing_pdfium_type()
            if build_type:
                self.config.build_type = build_type
                self.build_main_project()
            else:
                sys.exit(1)
        
        elif choice == 6:
            Logger.info("退出构建脚本")
            return
        
        # 显示构建完成信息
        print()
        Logger.success("🎉 构建完成!")
        pdfium_lib_path = self.pdfium_dir / "out" / self.config.build_type / "obj" / "libpdfium.a"
        Logger.info(f"📍 PDFium 静态库: {pdfium_lib_path}")
        Logger.info(f"📍 主项目可执行文件: {self.project_root / 'build'}/")
        print()
        Logger.info("💡 配置信息:")
        print(f"   构建类型: {self.config.build_type}")
        print(f"   V8 支持: {'启用' if self.config.enable_v8 else '禁用'}")
        print(f"   XFA 支持: {'启用' if self.config.enable_xfa else '禁用'}")

def main():
    """主函数"""
    parser = argparse.ArgumentParser(description="PDFium 完整构建脚本")
    parser.add_argument("--debug", action="store_true", help="启用调试模式")
    parser.add_argument("--clean", action="store_true", help="仅清理构建产物")
    
    args = parser.parse_args()
    
    if args.debug:
        logging.basicConfig(level=logging.DEBUG)
    
    builder = PDFiumBuilder()
    
    if args.clean:
        builder.clean_build_artifacts()
        return
    
    try:
        builder.run()
    except KeyboardInterrupt:
        print()
        Logger.info("构建被用户中断")
        sys.exit(0)
    except Exception as e:
        Logger.error(f"构建过程中发生错误: {e}")
        if args.debug:
            import traceback
            traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()