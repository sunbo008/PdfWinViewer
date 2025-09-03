#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PDFium 完整构建脚本 (Python版本)
带配置选择功能，使用系统 libc++ 避免符号冲突
自动下载和配置 depot_tools

使用方法:
  python3 build_pdfium_complete.py                    # 交互式构建
  python3 build_pdfium_complete.py --clean           # 仅清理构建产物
  python3 build_pdfium_complete.py --setup-depot-tools  # 仅设置 depot_tools
  python3 build_pdfium_complete.py --update-depot-tools # 仅更新 depot_tools
  python3 build_pdfium_complete.py --clean-depot-tools  # 彻底清理 depot_tools
  python3 build_pdfium_complete.py --debug           # 启用调试模式

特性:
  ✅ 自动检测并通过 git 克隆 depot_tools
  ✅ 支持 depot_tools 更新到最新版本
  ✅ 支持彻底清理 depot_tools (包括 PATH 配置)
  ✅ 安装到 tools 目录，避免权限问题
  ✅ 自动配置 PATH 环境变量 (支持 macOS/Linux/Windows)
  ✅ 使用 PDFium 自带的 gn 工具，无需系统级依赖
  ✅ 交互式构建配置选择
  ✅ 支持 Debug/Release 构建
  ✅ 可配置 V8 JavaScript 和 XFA 表单支持
  ✅ 使用系统 libc++ 避免符号冲突
  ✅ 自动生成完整的 libpdfium.a 静态库
"""

import os
import sys
import subprocess
import platform
import shutil
import time
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

# depot_tools 管理类
class DepotToolsManager:
    def __init__(self):
        self.home_dir = Path.home()
        # 将 depot_tools 安装到 tools 目录下，避免权限问题
        self.script_dir = Path(__file__).parent.absolute()
        self.depot_tools_dir = self.script_dir / "depot_tools"
        self.depot_tools_git_url = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"
        
        # 调试信息
        Logger.info(f"家目录: {self.home_dir}")
        Logger.info(f"脚本目录: {self.script_dir}")
        Logger.info(f"depot_tools 目标目录: {self.depot_tools_dir}")
    
    def is_depot_tools_available(self) -> bool:
        """检查 depot_tools 是否可用"""
        try:
            # 检查 PATH 中是否有 depot_tools
            result = subprocess.run(["fetch", "--help"], capture_output=True, check=True)
            return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            # 检查本地 depot_tools 目录
            if self.depot_tools_dir.exists():
                # 检查是否有可执行文件
                fetch_path = self.depot_tools_dir / "fetch"
                if platform.system().lower() == "windows":
                    fetch_path = self.depot_tools_dir / "fetch.bat"
                
                if fetch_path.exists():
                    return True
        return False
    
    def download_depot_tools(self):
        """通过 git 克隆 depot_tools"""
        Logger.info("开始克隆 depot_tools...")
        
        # 检查 git 是否可用
        try:
            subprocess.run(["git", "--version"], capture_output=True, check=True)
            Logger.success("Git 已安装")
        except (subprocess.CalledProcessError, FileNotFoundError):
            Logger.error("Git 未安装，请先安装 Git")
            Logger.info("macOS: brew install git")
            Logger.info("Ubuntu/Debian: sudo apt-get install git")
            Logger.info("Windows: 从 https://git-scm.com/ 下载安装")
            sys.exit(1)
        
        # 如果目标目录已存在，先删除
        if self.depot_tools_dir.exists():
            Logger.info(f"删除已存在的目录: {self.depot_tools_dir}")
            shutil.rmtree(self.depot_tools_dir)
        
        # 克隆 depot_tools
        Logger.info(f"从 {self.depot_tools_git_url} 克隆...")
        try:
            result = subprocess.run([
                "git", "clone", "--depth", "1", 
                self.depot_tools_git_url, 
                str(self.depot_tools_dir)
            ], capture_output=True, text=True, check=True)
            Logger.success("克隆完成")
        except subprocess.CalledProcessError as e:
            Logger.error(f"Git 克隆失败: {e}")
            Logger.error(f"错误输出: {e.stderr}")
            sys.exit(1)
        
        # 设置执行权限 (Unix 系统)
        if platform.system().lower() != "windows":
            self._set_executable_permissions()
        
        Logger.success(f"depot_tools 已克隆到: {self.depot_tools_dir}")
    
    def find_depot_tools_paths(self) -> list:
        """从当前 PATH 中查找所有 depot_tools 目录路径"""
        found_paths = []
        path_entries = os.environ.get("PATH", "").split(os.pathsep)
        for entry in path_entries:
            try:
                if not entry:
                    continue
                p = Path(entry).resolve()
                if p.exists() and p.is_dir() and p.name.lower() == "depot_tools":
                    found_paths.append(p)
            except Exception:
                # 忽略解析失败的 PATH 条目
                pass
        # 也包含当前脚本默认安装位置
        if self.depot_tools_dir.exists():
            found_paths.append(self.depot_tools_dir.resolve())
        # 去重
        unique_paths = []
        seen = set()
        for p in found_paths:
            s = str(p)
            if s not in seen:
                seen.add(s)
                unique_paths.append(p)
        return unique_paths
    
    def _show_current_depot_tools_in_path(self):
        """显示当前PATH中的所有depot_tools条目"""
        path_entries = os.environ.get("PATH", "").split(os.pathsep)
        depot_entries = [entry for entry in path_entries if 'depot_tools' in entry.lower()]
        
        if depot_entries:
            Logger.info(f"当前PATH中发现 {len(depot_entries)} 个 depot_tools 条目:")
            for i, entry in enumerate(depot_entries, 1):
                Logger.info(f"  {i}. {entry}")
        else:
            Logger.info("当前PATH中未发现 depot_tools 条目")
    
    def _clean_current_process_path(self):
        """尝试清理当前进程的PATH环境变量（注意：这只影响Python进程，不影响shell）"""
        Logger.info("尝试清理当前Python进程的PATH环境变量...")
        
        original_path = os.environ.get("PATH", "")
        path_entries = original_path.split(os.pathsep)
        
        # 过滤掉包含depot_tools的条目
        clean_entries = [entry for entry in path_entries if 'depot_tools' not in entry.lower()]
        
        # 去重，保持顺序
        seen = set()
        unique_entries = []
        for entry in clean_entries:
            if entry not in seen and entry.strip():
                seen.add(entry)
                unique_entries.append(entry)
        
        new_path = os.pathsep.join(unique_entries)
        
        if new_path != original_path:
            os.environ["PATH"] = new_path
            removed_count = len(path_entries) - len(unique_entries)
            Logger.info(f"已从Python进程PATH中移除 {removed_count} 个条目")
            Logger.warning("⚠️  注意：这只影响Python进程，不影响当前shell")
        else:
            Logger.info("当前Python进程PATH无需清理")
    
    def clean_depot_tools(self):
        """彻底清理 depot_tools，包括通过 PATH 查找并删除目录、移除 PATH 配置"""
        Logger.info("开始彻底清理 depot_tools...")
        
        # 0. 显示当前PATH中的depot_tools条目
        self._show_current_depot_tools_in_path()
        
        # 1) 从 PATH 中查找 depot_tools 目录并尝试删除
        found_paths = self.find_depot_tools_paths()
        if not found_paths:
            Logger.info("未在 PATH 中发现 depot_tools 目录")
        else:
            Logger.info(f"发现 {len(found_paths)} 个 depot_tools 目录")
            for p in found_paths:
                try:
                    if p.exists() and p.is_dir() and p.name.lower() == "depot_tools":
                        Logger.info(f"删除 depot_tools 目录: {p}")
                        shutil.rmtree(p)
                        Logger.success(f"已删除: {p}")
                except Exception as e:
                    Logger.warning(f"删除失败 {p}: {e}")
        
        # 兼容：再次尝试删除脚本默认目录
        if self.depot_tools_dir.exists():
            try:
                Logger.info(f"删除默认 depot_tools 目录: {self.depot_tools_dir}")
                shutil.rmtree(self.depot_tools_dir)
                Logger.success("默认 depot_tools 目录已删除")
            except Exception as e:
                Logger.warning(f"默认目录删除失败: {e}")
        
        # 2. 从 shell 配置文件中移除 PATH 配置
        Logger.info("从 shell 配置文件中移除 depot_tools PATH 配置...")
        
        system = platform.system().lower()
        if system == "darwin":  # macOS
            self._remove_from_macos_path()
        elif system == "linux":
            self._remove_from_linux_path()
        elif system == "windows":
            self._remove_from_windows_path()
        else:
            Logger.warning(f"不支持的操作系统: {system}")
            Logger.info("请手动从 shell 配置文件中移除以下行:")
            Logger.info(f"  export PATH=\"{self.depot_tools_dir}:$PATH\"")
        
        # 3. 清理当前进程的PATH环境变量
        self._clean_current_process_path()
        
        # 4. 验证清理结果
        Logger.info("验证清理结果...")
        remaining_paths = self.find_depot_tools_paths()
        if remaining_paths:
            Logger.warning(f"仍有 {len(remaining_paths)} 个 depot_tools 目录残留:")
            for p in remaining_paths:
                Logger.warning(f"  {p}")
        else:
            Logger.success("所有 depot_tools 目录已清理完成")
        
        Logger.success("depot_tools 彻底清理完成!")
        
        # 5. 检查当前shell PATH状态并提供解决方案
        Logger.info("检查当前shell PATH状态...")
        
        # 重新检查当前进程的PATH（因为_clean_current_process_path可能没有实际影响shell）
        current_path = os.environ.get("PATH", "")
        current_depot_entries = [entry for entry in current_path.split(os.pathsep) if 'depot_tools' in entry.lower()]
        
        if current_depot_entries:
            Logger.warning("⚠️  当前shell进程的PATH仍包含depot_tools条目")
            Logger.info(f"   发现 {len(current_depot_entries)} 个条目:")
            for i, entry in enumerate(current_depot_entries, 1):
                Logger.info(f"   {i}. {entry}")
            
            Logger.info("💡 这是正常现象，原因：")
            Logger.info("   • 当前shell继承了启动时的PATH")
            Logger.info("   • Python进程环境变量清理不影响父shell")
            
            Logger.info("🔧 解决方案（任选一种）：")
            Logger.info("   1. 重新打开终端窗口（推荐）⭐")
            Logger.info("   2. 运行：exec $SHELL -l")
            Logger.info("   3. 运行：source ~/.zshrc")
            Logger.info("   4. 手动清理：export PATH=$(echo $PATH | tr ':' '\\n' | grep -v depot_tools | tr '\\n' ':' | sed 's/:$//')")
            
            # 验证新shell是否干净
            try:
                result = subprocess.run(
                    ['zsh', '-c', 'echo $PATH | tr ":" "\\n" | grep depot_tools | wc -l'],
                    capture_output=True, text=True, check=True
                )
                new_shell_count = int(result.stdout.strip())
                if new_shell_count == 0:
                    Logger.success("✅ 验证：新shell环境已清理干净")
                    Logger.info("   配置文件清理成功，新终端将完全干净")
                else:
                    Logger.error(f"❌ 警告：新shell仍有 {new_shell_count} 个depot_tools条目")
                    Logger.error("   可能存在其他配置文件，请手动检查")
            except Exception as e:
                Logger.warning(f"无法验证新shell环境: {e}")
        else:
            Logger.success("✅ 当前shell PATH已完全清理")
        
        return True
    
    def _remove_from_macos_path(self):
        """从 macOS shell 配置文件中移除 depot_tools PATH"""
        # macOS 可能的shell配置文件列表
        potential_files = [
            "~/.zshrc",        # zsh运行命令
            "~/.zprofile",     # zsh登录配置 
            "~/.zshenv",       # zsh环境变量
            "~/.bash_profile", # bash登录配置
            "~/.bashrc",       # bash运行命令
            "~/.profile",      # 通用shell配置
            "~/.bash_login",   # bash登录脚本
        ]
        
        shell_rc_files = []
        for file_path in potential_files:
            expanded_path = os.path.expanduser(file_path)
            if os.path.exists(expanded_path):
                shell_rc_files.append(expanded_path)
        
        if shell_rc_files:
            Logger.info(f"检查 {len(shell_rc_files)} 个配置文件...")
            for rc_file in shell_rc_files:
                self._remove_any_depot_tools_from_file(rc_file, f"~/{os.path.basename(rc_file)}")
        else:
            Logger.info("未找到需要清理的shell配置文件")
    
    def _remove_from_linux_path(self):
        """从 Linux shell 配置文件中移除 depot_tools PATH"""
        # Linux 可能的shell配置文件列表
        potential_files = [
            "~/.bashrc",       # bash运行命令
            "~/.bash_profile", # bash登录配置
            "~/.profile",      # 通用shell配置
            "~/.bash_login",   # bash登录脚本
            "~/.zshrc",        # zsh运行命令（如果使用zsh）
            "~/.zprofile",     # zsh登录配置
            "~/.zshenv",       # zsh环境变量
        ]
        
        shell_rc_files = []
        for file_path in potential_files:
            expanded_path = os.path.expanduser(file_path)
            if os.path.exists(expanded_path):
                shell_rc_files.append(expanded_path)
        
        if shell_rc_files:
            Logger.info(f"检查 {len(shell_rc_files)} 个配置文件...")
            for rc_file in shell_rc_files:
                self._remove_any_depot_tools_from_file(rc_file, f"~/{os.path.basename(rc_file)}")
        else:
            Logger.info("未找到需要清理的shell配置文件")
    
    def _remove_from_windows_path(self):
        """从 Windows 环境变量中移除 depot_tools PATH"""
        Logger.info("Windows 系统需要手动配置环境变量")
        Logger.info("请按以下步骤操作:")
        Logger.info("1. 右键点击'此电脑' -> '属性'")
        Logger.info("2. 点击'高级系统设置'")
        Logger.info("3. 点击'环境变量'")
        Logger.info("4. 在'系统变量'中找到'Path'，点击'编辑'")
        Logger.info("5. 找到并删除以下路径:")
        Logger.info(f"   {self.depot_tools_dir}")
        Logger.info("6. 点击'确定'保存所有对话框")
        Logger.info("7. 重新打开命令提示符或 PowerShell")
    
    def _remove_path_from_file(self, file_path: str, path_line: str, display_name: str):
        """从配置文件中移除特定 PATH 行（保留以向后兼容）"""
        try:
            if not os.path.exists(file_path):
                Logger.info(f"{display_name} 不存在，跳过")
                return
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            if path_line in content:
                new_content = content.replace(path_line + "\n", "")
                new_content = new_content.replace(path_line, "")
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(new_content)
                Logger.success(f"已从 {display_name} 中移除 depot_tools PATH 配置")
            else:
                Logger.info(f"{display_name} 中未找到指定 PATH 配置")
        except Exception as e:
            Logger.warning(f"无法修改 {display_name}: {e}")
    
    def _remove_any_depot_tools_from_file(self, file_path: str, display_name: str):
        """从配置文件中移除所有包含 depot_tools 的 PATH 配置行"""
        try:
            if not os.path.exists(file_path):
                Logger.info(f"{display_name} 不存在，跳过")
                return
            
            with open(file_path, 'r', encoding='utf-8') as f:
                lines = f.readlines()
            
            original_len = len(lines)
            removed_lines = []
            
            # 过滤掉包含depot_tools的行，并记录被移除的行
            filtered_lines = []
            for line in lines:
                if 'depot_tools' in line.lower():
                    removed_lines.append(line.strip())
                else:
                    filtered_lines.append(line)
            
            # 额外清理：去除重复的PATH导出行
            cleaned_lines = self._deduplicate_path_exports(filtered_lines)
            
            if len(cleaned_lines) != original_len:
                # 创建备份
                backup_path = f"{file_path}.backup_{int(time.time())}"
                shutil.copy2(file_path, backup_path)
                Logger.info(f"已创建备份: {backup_path}")
                
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.writelines(cleaned_lines)
                
                removed_count = original_len - len(cleaned_lines)
                Logger.success(f"已从 {display_name} 中移除 {removed_count} 行配置")
                
                if removed_lines:
                    Logger.info("移除的行:")
                    for line in removed_lines[:3]:  # 只显示前3行
                        Logger.info(f"  - {line}")
                    if len(removed_lines) > 3:
                        Logger.info(f"  ... 还有 {len(removed_lines) - 3} 行")
            else:
                Logger.info(f"{display_name} 中未找到需要清理的配置")
                
        except Exception as e:
            Logger.warning(f"无法修改 {display_name}: {e}")
    
    def _deduplicate_path_exports(self, lines):
        """去除重复的PATH导出行，保留最后一个"""
        seen_exports = {}
        result = []
        
        for line in lines:
            stripped = line.strip()
            if stripped.startswith('export PATH=') or stripped.startswith('PATH='):
                # 提取PATH的基本模式
                if '=' in stripped:
                    var_part = stripped.split('=', 1)[0]
                    seen_exports[var_part] = line  # 保存最新的
                else:
                    result.append(line)
            else:
                result.append(line)
        
        # 将去重后的PATH导出行添加到结果中
        for export_line in seen_exports.values():
            result.append(export_line)
        
        return result
    
    def update_depot_tools(self):
        """更新 depot_tools 到最新版本"""
        if not self.depot_tools_dir.exists():
            Logger.warning("depot_tools 目录不存在，无法更新")
            return False
        
        Logger.info("更新 depot_tools 到最新版本...")
        try:
            result = subprocess.run([
                "git", "pull", "origin", "main"
            ], cwd=self.depot_tools_dir, capture_output=True, text=True, check=True)
            Logger.success("depot_tools 更新完成")
            return True
        except subprocess.CalledProcessError as e:
            Logger.error(f"更新失败: {e}")
            Logger.error(f"错误输出: {e.stderr}")
            return False
    
    def _set_executable_permissions(self):
        """设置可执行权限 (Unix 系统)"""
        try:
            for file in self.depot_tools_dir.glob("*"):
                if file.is_file() and not file.suffix:
                    file.chmod(0o755)
        except Exception as e:
            Logger.warning(f"设置权限时出现问题: {e}")
    
    def add_to_path_permanently(self):
        """将 depot_tools 永久添加到 PATH 环境变量"""
        Logger.info("配置 depot_tools 到 PATH 环境变量...")
        
        system = platform.system().lower()
        
        if system == "darwin":  # macOS
            self._configure_macos_path()
        elif system == "linux":
            self._configure_linux_path()
        elif system == "windows":
            self._configure_windows_path()
        else:
            Logger.warning(f"不支持的操作系统: {system}")
            Logger.info("请手动将以下路径添加到 PATH 环境变量:")
            Logger.info(f"  {self.depot_tools_dir}")
            return
        
        # 同步更新当前进程 PATH，保证本次执行内可用
        self.activate_path_now()
        
        Logger.success("depot_tools 已永久添加到 PATH 环境变量")
        Logger.info("请重新打开终端或运行 'source ~/.zshrc' (macOS/Linux) 使配置生效")

    def activate_path_now(self):
        """将 depot_tools 目录加入当前进程 PATH，立刻生效（仅对当前进程）"""
        try:
            cur = os.environ.get("PATH", "")
            depot = str(self.depot_tools_dir)
            if depot not in cur.split(os.pathsep):
                os.environ["PATH"] = depot + os.pathsep + cur
                Logger.info(f"已将 {depot} 添加到当前进程 PATH")
        except Exception as e:
            Logger.warning(f"无法更新当前进程 PATH: {e}")
    
    def _configure_macos_path(self):
        """配置 macOS 的 PATH"""
        # 检查是否使用 zsh 或 bash
        shell_rc_files = []
        if os.path.exists(os.path.expanduser("~/.zshrc")):
            shell_rc_files.append("~/.zshrc")
        if os.path.exists(os.path.expanduser("~/.bash_profile")):
            shell_rc_files.append("~/.bash_profile")
        if os.path.exists(os.path.expanduser("~/.profile")):
            shell_rc_files.append("~/.profile")
        
        # 使用绝对路径，因为 depot_tools 现在在 tools 目录下
        depot_tools_path = f'export PATH="{self.depot_tools_dir}:$PATH"'
        
        for rc_file in shell_rc_files:
            rc_path = os.path.expanduser(rc_file)
            self._add_path_to_file(rc_path, depot_tools_path, f"~/{rc_file.split('/')[-1]}")
    
    def _configure_linux_path(self):
        """配置 Linux 的 PATH"""
        # 检查常见的 shell 配置文件
        shell_rc_files = []
        if os.path.exists(os.path.expanduser("~/.bashrc")):
            shell_rc_files.append("~/.bashrc")
        if os.path.exists(os.path.expanduser("~/.profile")):
            shell_rc_files.append("~/.profile")
        if os.path.exists(os.path.expanduser("~/.zshrc")):
            shell_rc_files.append("~/.zshrc")
        
        # 使用绝对路径，因为 depot_tools 现在在 tools 目录下
        depot_tools_path = f'export PATH="{self.depot_tools_dir}:$PATH"'
        
        for rc_file in shell_rc_files:
            rc_path = os.path.expanduser(rc_file)
            self._add_path_to_file(rc_path, depot_tools_path, f"~/{rc_file.split('/')[-1]}")
    
    def _configure_windows_path(self):
        """配置 Windows 的 PATH"""
        Logger.info("Windows 系统需要手动配置环境变量")
        Logger.info("请按以下步骤操作:")
        Logger.info("1. 右键点击 '此电脑' -> '属性'")
        Logger.info("2. 点击 '高级系统设置'")
        Logger.info("3. 点击 '环境变量'")
        Logger.info("4. 在 '系统变量' 中找到 'Path'，点击 '编辑'")
        Logger.info("5. 点击 '新建'，添加以下路径:")
        Logger.info(f"   {self.depot_tools_dir}")
        Logger.info("6. 点击 '确定' 保存所有对话框")
        Logger.info("7. 重新打开命令提示符或 PowerShell")
    
    def _add_path_to_file(self, file_path: str, path_line: str, display_name: str):
        """向配置文件添加 PATH 行"""
        try:
            # 检查是否已经存在
            if os.path.exists(file_path):
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                    if path_line in content:
                        Logger.info(f"{display_name} 中已存在 depot_tools 路径")
                        return
            
            # 添加路径配置
            with open(file_path, 'a', encoding='utf-8') as f:
                f.write(f"\n# depot_tools PATH 配置\n{path_line}\n")
            
            Logger.success(f"已添加到 {display_name}")
            
        except Exception as e:
            Logger.warning(f"无法修改 {display_name}: {e}")
    
    def setup_depot_tools(self):
        """设置 depot_tools"""
        if self.is_depot_tools_available():
            Logger.success("depot_tools 已可用")
            
            # 询问是否更新
            while True:
                try:
                    choice = input("是否更新 depot_tools 到最新版本? (y/n) [默认: n]: ").strip().lower()
                    if not choice:
                        choice = "n"
                    
                    if choice in ["y", "yes"]:
                        if self.update_depot_tools():
                            Logger.success("depot_tools 更新完成")
                        else:
                            Logger.warning("depot_tools 更新失败，但当前版本仍可使用")
                        break
                    elif choice in ["n", "no"]:
                        Logger.info("使用当前版本的 depot_tools")
                        break
                    else:
                        Logger.error("请输入 y 或 n")
                except KeyboardInterrupt:
                    print()
                    Logger.info("用户取消操作")
                    break
            
            return True
        
        Logger.warning("depot_tools 未安装或未添加到 PATH")
        
        # 询问用户是否自动下载
        while True:
            try:
                choice = input("是否自动克隆并配置 depot_tools? (y/n) [默认: y]: ").strip().lower()
                if not choice:
                    choice = "y"
                
                if choice in ["y", "yes"]:
                    break
                elif choice in ["n", "no"]:
                    Logger.info("请手动安装 depot_tools:")
                    Logger.info("  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git")
                    Logger.info("  export PATH=$PATH:/path/to/depot_tools")
                    return False
                else:
                    Logger.error("请输入 y 或 n")
            except KeyboardInterrupt:
                print()
                Logger.info("用户取消操作")
                sys.exit(0)
        
        # 克隆 depot_tools
        self.download_depot_tools()
        
        # 添加到 PATH
        self.add_to_path_permanently()
        
        # 验证安装
        if self.is_depot_tools_available():
            Logger.success("depot_tools 设置完成!")
            return True
        else:
            Logger.warning("depot_tools 设置完成，但可能需要重新打开终端")
            Logger.info("请重新运行此脚本或手动运行: source ~/.zshrc (macOS) 或 source ~/.bashrc (Linux)")
            return False

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
        
        # depot_tools 管理器
        self.depot_manager = DepotToolsManager()
        
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
        print("6) 安装/更新 depot_tools")
        print("   🔧 自动克隆/更新并配置 PATH")
        print()
        print("7) 清理 depot_tools (删除目录并清理 PATH)")
        print()
        print("8) 退出")
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
                choice = int(input("请选择操作 (1-8): "))
                if 1 <= choice <= 8:
                    return choice
                else:
                    Logger.error("请输入 1-8 之间的数字")
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
        
        # 检查并设置 depot_tools（用于gclient同步）
        if not self.depot_manager.setup_depot_tools():
            Logger.error("depot_tools 设置失败，无法继续构建")
            sys.exit(1)
        
        # 检查 cmake
        try:
            subprocess.run(["cmake", "--version"], capture_output=True, check=True)
            Logger.success("CMake 已安装")
        except (subprocess.CalledProcessError, FileNotFoundError):
            Logger.error("CMake 未安装，请先安装 CMake")
            sys.exit(1)
    
    def get_pdfium_gn_path(self) -> Path:
        """获取PDFium自带的gn工具路径"""
        # 根据平台确定gn可执行文件名
        gn_name = "gn.exe" if self.config.target_os == "win" else "gn"
        gn_path = self.pdfium_dir / "buildtools" / self.config.target_os / gn_name
        
        if not gn_path.exists():
            Logger.error(f"PDFium gn工具不存在: {gn_path}")
            Logger.error("请确保PDFium源码完整下载并同步")
            sys.exit(1)
        
        if not gn_path.is_file():
            Logger.error(f"PDFium gn工具不是可执行文件: {gn_path}")
            sys.exit(1)
            
        # 检查gn工具是否可执行
        try:
            result = subprocess.run([str(gn_path), "--version"], 
                                 capture_output=True, check=True, text=True)
            Logger.success(f"PDFium gn工具可用 (版本: {result.stdout.strip()})")
            return gn_path
        except subprocess.CalledProcessError as e:
            Logger.error(f"PDFium gn工具无法执行: {e}")
            Logger.error("可能需要重新同步PDFium依赖")
            sys.exit(1)
        except FileNotFoundError:
            Logger.error(f"PDFium gn工具无法找到: {gn_path}")
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

# PDFium 独立编译配置
pdf_is_standalone = true    # 确保 PDFium 作为独立模块编译
pdf_is_complete_lib = true  # 生成单一的完整静态库 libpdfium.a
is_component_build = false # Disable component build (Though it should work)

# 静态链接依赖库
use_system_freetype = false # 强制使用内置的 freetype（避免外部依赖）
use_system_libjpeg = false  # 同上，使用内置 libjpeg
use_system_libpng = false
use_system_zlib = false

# 使用系统 libc++ 避免符号冲突
use_custom_libcxx = false

# 功能配置
pdf_use_skia = false
pdf_enable_xfa = {enable_xfa}
pdf_enable_v8 = {enable_v8}


# 平台配置
target_os = "{self.config.target_os}"
target_cpu = "{self.config.target_cpu}"

# 强制使用 C++20 标准
use_cxx23 = false
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
            ["gclient", "sync"],
            cwd=self.third_party_dir
        )
        
        # 验证PDFium gn工具可用性
        Logger.info("验证PDFium构建工具...")
        self.get_pdfium_gn_path()  # 这会检查gn工具并显示版本信息
        
        # 创建构建配置
        Logger.info("配置 PDFium 构建参数...")
        build_dir = self.pdfium_dir / "out" / self.config.build_type
        build_dir.mkdir(parents=True, exist_ok=True)
        
        self.generate_pdfium_config(build_dir)
        
        # 生成构建文件
        Logger.info("生成构建文件...")
        gn_path = self.get_pdfium_gn_path()
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
            Logger.info("选择：安装/更新 depot_tools")
            self.depot_manager.setup_depot_tools()
            # 不同平台下尝试激活 PATH（当前进程已更新，另外提示用户）
            system = platform.system().lower()
            if system in ("darwin", "linux"):
                Logger.info("尝试激活 shell 配置: source ~/.zshrc 或 ~/.bashrc (若存在)")
                # 尝试 source 常见 rc 文件以便当前子进程获得新 PATH（注意：对父 shell 不生效）
                rc_candidates = ["~/.zshrc", "~/.bashrc", "~/.profile", "~/.bash_profile"]
                for rc in rc_candidates:
                    rc_expanded = os.path.expanduser(rc)
                    if os.path.exists(rc_expanded):
                        try:
                            subprocess.run(["/bin/zsh", "-c", f"source {rc_expanded} >/dev/null 2>&1 || true"], check=False)
                        except Exception:
                            pass
            elif system == "windows":
                Logger.info("Windows 需重新打开命令行窗口以生效 PATH")
            return
        
        elif choice == 7:
            Logger.info("选择：清理 depot_tools")
            self.depot_manager.clean_depot_tools()
            return
        
        elif choice == 8:
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
        
        # 显示 depot_tools 状态
        print()
        Logger.info("🔧 depot_tools 状态:")
        if self.depot_manager.is_depot_tools_available():
            Logger.success("depot_tools 已可用")
            Logger.info(f"   安装路径: {self.depot_manager.depot_tools_dir}")
        else:
            Logger.warning("depot_tools 可能未正确配置到 PATH")
            Logger.info("   请重新打开终端或运行: source ~/.zshrc (macOS) 或 source ~/.bashrc (Linux)")

def main():
    """主函数"""
    parser = argparse.ArgumentParser(description="PDFium 完整构建脚本")
    parser.add_argument("--debug", action="store_true", help="启用调试模式")
    parser.add_argument("--clean", action="store_true", help="仅清理构建产物")
    parser.add_argument("--setup-depot-tools", action="store_true", help="仅设置 depot_tools")
    parser.add_argument("--update-depot-tools", action="store_true", help="仅更新 depot_tools")
    parser.add_argument("--clean-depot-tools", action="store_true", help="彻底清理 depot_tools")
    
    args = parser.parse_args()
    
    if args.debug:
        logging.basicConfig(level=logging.DEBUG)
    
    builder = PDFiumBuilder()
    
    if args.clean:
        builder.clean_build_artifacts()
        return
    
    if args.setup_depot_tools:
        Logger.info("仅设置 depot_tools...")
        if builder.depot_manager.setup_depot_tools():
            Logger.success("depot_tools 设置完成!")
        else:
            Logger.error("depot_tools 设置失败!")
            sys.exit(1)
        return
    
    if args.update_depot_tools:
        Logger.info("仅更新 depot_tools...")
        if builder.depot_manager.update_depot_tools():
            Logger.success("depot_tools 更新完成!")
        else:
            Logger.error("depot_tools 更新失败!")
            sys.exit(1)
        return
    
    if args.clean_depot_tools:
        Logger.info("彻底清理 depot_tools...")
        if builder.depot_manager.clean_depot_tools():
            Logger.success("depot_tools 清理完成!")
        else:
            Logger.error("depot_tools 清理失败!")
            sys.exit(1)
        return
    
    if args.setup_depot_tools:
        Logger.info("仅设置 depot_tools...")
        if builder.depot_manager.setup_depot_tools():
            Logger.success("depot_tools 设置完成!")
        else:
            Logger.error("depot_tools 设置失败!")
            sys.exit(1)
        return
    
    if args.update_depot_tools:
        Logger.info("仅更新 depot_tools...")
        if builder.depot_manager.update_depot_tools():
            Logger.success("depot_tools 更新完成!")
        else:
            Logger.error("depot_tools 更新失败!")
            sys.exit(1)
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