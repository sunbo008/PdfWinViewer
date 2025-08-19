# PdfWinViewer 使用说明

一个基于 Windows SDK + PDFium 的最小 Win32 查看器（非 Qt）。支持：
- XFA/V8（取决于你的 PDFium 构建）
- 高 DPI 感知（Per‑Monitor V2）
- 文本清晰度优化（FPDF_LCD_TEXT）
- 水平/垂直滚动条
- Ctrl+滚轮缩放（以鼠标位置为锚点）
- 最近浏览（File → Recent…，存储于 %LOCALAPPDATA%\PdfWinViewer\recent.txt）

## 目录结构
```
PdfWinViewer/
  CMakeLists.txt          # CMake 构建脚本
  PdfWinViewer/
    Main.cpp              # Win32 查看器源码
```

## 先决条件
- Windows 10/11，Visual Studio 2022（v143 工具集）
- 已获取 PDFium 源码并完成依赖（depot_tools 等）
- 推荐在“VS 2022 开发者命令提示符”中执行以下命令

## 构建 PDFium（组件构建，生成 dll）
确保 `pdfium/out/XFA/args.gn` 至少包含：
```gn
is_debug = true
pdf_is_standalone = true
pdf_enable_v8 = true
pdf_enable_xfa = true
is_component_build = true   # 关键：生成 pdfium.dll
```
生成与编译：
```bat
cd /d D:\workspace\pdfium_20250814\pdfium
buildtools\win\gn.exe gen out\XFA
..\depot_tools\ninja.bat -C out\XFA pdfium
```
成功后应得到：`out\XFA\pdfium.dll` 与 `out\XFA\pdfium.dll.lib`。

## 构建 PdfWinViewer（CMake）
在同一命令行中执行：
```bat
cd /d D:\workspace\pdfium_20250814\PdfWinViewer
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DPDFIUM_ROOT=D:/workspace/pdfium_20250814/pdfium ^
  -DPDFIUM_OUT=D:/workspace/pdfium_20250814/pdfium/out/XFA
cmake --build build --config Debug --parallel
```
可执行文件位置：`build\Debug\PdfWinViewer.exe`

> CMake 构建后会自动将 `out\XFA\*.dll` 拷贝到可执行目录；若仍有缺失，手动复制 `out\XFA` 下的所有 dll 即可。

## 运行
- 双击 `build\Debug\PdfWinViewer.exe`
- File → Open… 选择 PDF
- Ctrl+滚轮缩放，滚轮或滚动条滚动
- File → Recent… 可快速打开最近文件；“Clear Recent” 清空列表

## 常见问题
- 文本不清晰：已启用 `FPDF_LCD_TEXT`；若仍模糊，检查显示缩放与显卡驱动，或尝试将窗口放大以减少二次缩放。
- XFA 行为异常：确认 `args.gn` 已开启 `pdf_enable_xfa = true` 且在加载后调用了 `FPDF_LoadXFA`（本工程已自动处理）。
- 缺少 dll：从 `pdfium/out/XFA` 复制所有 `*.dll` 到可执行目录。
- 重新构建失败（LNK1168）：关闭正在运行的 `PdfWinViewer.exe` 后重试。

---

# English (Quick Guide)
A minimal Win32 PDF viewer using Windows SDK + PDFium. Features: high‑DPI, LCD text rendering, scrollbars, zoom (Ctrl+Wheel), XFA/V8 (if enabled), recent files.

## Build PDFium (component build)
Enable `is_component_build = true` in `out/XFA/args.gn`, then:
```bat
cd D:\workspace\pdfium_20250814\pdfium
buildtools\win\gn.exe gen out\XFA
..\depot_tools\ninja.bat -C out\XFA pdfium
```
It should produce `out\XFA\pdfium.dll` and `pdfium.dll.lib`.

## Build Viewer (CMake)
```bat
cd D:\workspace\pdfium_20250814\PdfWinViewer
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DPDFIUM_ROOT=D:/workspace/pdfium_20250814/pdfium ^
  -DPDFIUM_OUT=D:/workspace/pdfium_20250814/pdfium/out/XFA
cmake --build build --config Debug --parallel
```
Run: `build\Debug\PdfWinViewer.exe`

DLLs from `out\XFA` are copied post‑build automatically. If anything is missing, copy all `*.dll` manually.
