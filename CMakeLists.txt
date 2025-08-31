cmake_minimum_required(VERSION 3.20)
project(PdfWinViewer LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PDFIUM_ROOT "${CMAKE_SOURCE_DIR}/../pdfium" CACHE PATH "PDFium root (preferred: prebuilt package root with include/ and lib/)")
set(PDFIUM_OUT  "${PDFIUM_ROOT}/out/XFA" CACHE PATH "PDFium output dir (Windows legacy)")

# 公共头目录优先来源：third_party 子模块或 PDFIUM_ROOT/include
set(PDFIUM_PUBLIC_DIR "${CMAKE_SOURCE_DIR}/third_party/pdfium/public")
if (NOT EXISTS "${PDFIUM_PUBLIC_DIR}")
  set(PDFIUM_PUBLIC_DIR "${PDFIUM_ROOT}/public")
endif()

if (WIN32)
  # Windows: 兼容原有查找逻辑（dll + import lib）
  set(PDFIUM_BIN  "${PDFIUM_OUT}" CACHE PATH "PDFium bin dir containing pdfium.dll")
  set(PDFIUM_LIB_HINT1  "${PDFIUM_OUT}")
  set(PDFIUM_LIB_HINT2  "${PDFIUM_ROOT}/lib")
  if (EXISTS "${PDFIUM_LIB_HINT1}/pdfium.dll.lib")
    set(PDFIUM_LIB "${PDFIUM_LIB_HINT1}")
  elseif (EXISTS "${PDFIUM_LIB_HINT2}/pdfium.dll.lib")
    set(PDFIUM_LIB "${PDFIUM_LIB_HINT2}")
  else()
    set(PDFIUM_LIB "${PDFIUM_OUT}")
  endif()
  set(PDFIUM_IMPORT_LIB "${PDFIUM_LIB}/pdfium.dll.lib")
elseif(APPLE)
  # macOS: 仅允许静态链接。必须提供 PDFIUM_STATIC（完整静态库 libpdfium.a 的绝对路径）。
  set(PDFIUM_STATIC "${PDFIUM_STATIC}" CACHE FILEPATH "Absolute path to libpdfium.a (from pdf_is_complete_lib build)")
  if (NOT EXISTS "${PDFIUM_STATIC}")
    message(FATAL_ERROR "必须设置 -DPDFIUM_STATIC=/abs/path/to/libpdfium.a (由 pdf_is_complete_lib 构建产物)。")
  endif()
endif()

if (WIN32)
  add_executable(PdfWinViewer WIN32
    PdfWinViewer/Main.cpp
  )
elseif(APPLE)
  add_executable(PdfWinViewer MACOSX_BUNDLE
    platform/mac/App.mm
  )
endif()

# 日志编译期开关（Debug 默认启用，Release 默认关闭；可通过 -DPDFWV_ENABLE_LOGGING=0/1 覆盖）
option(PDFWV_ENABLE_LOGGING "Enable in-app logging" OFF)
if (WIN32)
  target_compile_definitions(PdfWinViewer PRIVATE UNICODE _UNICODE NOMINMAX PDFWV_ENABLE_LOGGING=$<IF:$<CONFIG:Debug>,1,$<BOOL:${PDFWV_ENABLE_LOGGING}>>)
endif()

target_include_directories(PdfWinViewer PRIVATE
  "${PDFIUM_PUBLIC_DIR}"
  "${PDFIUM_ROOT}/include" # 兼容某些发行包/旧布局
)

if (WIN32)
  target_link_directories(PdfWinViewer PRIVATE "${PDFIUM_LIB}")
  target_link_libraries(PdfWinViewer PRIVATE
    "${PDFIUM_IMPORT_LIB}"
    user32
    gdi32
    comdlg32
    shlwapi
    shell32
    psapi
  )
elseif(APPLE)
  # 仅静态链接
  target_link_libraries(PdfWinViewer PRIVATE "${PDFIUM_STATIC}")
  message(STATUS "Linking against static PDFium: ${PDFIUM_STATIC}")
  # 尝试链接 PDFium 构建产物自带的静态 libc++ 与 libc++abi（pdfium 完整静态库仍引用它们）
  get_filename_component(_PDFIUM_OBJ_DIR "${PDFIUM_STATIC}" DIRECTORY) # .../out/<cfg>/obj
  set(_LIBCXX_A     "${_PDFIUM_OBJ_DIR}/buildtools/third_party/libc++/libc++.a")
  set(_LIBCXXABI_A  "${_PDFIUM_OBJ_DIR}/buildtools/third_party/libc++abi/libc++abi.a")
  if (EXISTS "${_LIBCXX_A}")
    target_link_libraries(PdfWinViewer PRIVATE "${_LIBCXX_A}")
  endif()
  if (EXISTS "${_LIBCXXABI_A}")
    target_link_libraries(PdfWinViewer PRIVATE "${_LIBCXXABI_A}")
  endif()
  # partition_alloc 在 macOS 需要 Security.framework 以检测 MAP_JIT 权限
  target_link_libraries(PdfWinViewer PRIVATE "-framework Security")
  # 追加系统 C++ 运行库
  target_link_libraries(PdfWinViewer PRIVATE c++ c++abi)
  target_link_libraries(PdfWinViewer PRIVATE
    "-framework Cocoa"
    "-framework AppKit"
    "-framework CoreGraphics"
    "-framework Foundation"
    "-framework ImageIO"
    "-framework UniformTypeIdentifiers"
  )

  # 将图标作为资源打入 bundle（更可靠）
  set(APP_ICON "${CMAKE_SOURCE_DIR}/assets/MonkeyPDF.icns")
  if(EXISTS "${APP_ICON}")
    set_source_files_properties("${APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(PdfWinViewer PRIVATE "${APP_ICON}")
  endif()

  # 兼容路径：构建完成后也复制一份，确保存在
  add_custom_command(TARGET PdfWinViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/MonkeyPDF.icns"
            "$<TARGET_FILE_DIR:PdfWinViewer>/../Resources/MonkeyPDF.icns"
  )
  set_target_properties(PdfWinViewer PROPERTIES
    MACOSX_BUNDLE TRUE
    INSTALL_RPATH "@executable_path/../Frameworks;@loader_path"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/mac/Info.plist"
  )
endif()

if (WIN32)
  find_file(PDFIUM_DLL_FILE NAMES pdfium.dll PATHS "${PDFIUM_OUT}" "${PDFIUM_ROOT}/out/XFA" "${PDFIUM_ROOT}/bin" NO_DEFAULT_PATH)
  if (NOT PDFIUM_DLL_FILE)
    message(WARNING "pdfium.dll not found in PDFIUM_OUT or PDFIUM_ROOT/bin; skip copy.")
  else()
    get_filename_component(PDFIUM_DLL_DIR "${PDFIUM_DLL_FILE}" DIRECTORY)
    # 容错式批量复制：使用 PowerShell 一次性复制 *.dll，忽略单个错误并强制返回 0
    add_custom_command(TARGET PdfWinViewer POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E echo "Copying PDFium DLLs from ${PDFIUM_DLL_DIR}..."
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PdfWinViewer>"
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command "\
        $src='${PDFIUM_DLL_DIR}'; \
        $dst='$<TARGET_FILE_DIR:PdfWinViewer>'; \
        if (Test-Path -LiteralPath $src) { \
          Get-ChildItem -LiteralPath $src -Filter *.dll -File -ErrorAction SilentlyContinue | ForEach-Object { \
            try { Copy-Item -LiteralPath $_.FullName -Destination $dst -Force -ErrorAction SilentlyContinue } catch { } \
          } \
        }; \
        exit 0"
    )
  endif()
endif()

# 生成 VS Code 配置（仅在不存在时生成，避免覆盖手动配置）
option(GENERATE_VSCODE "Generate .vscode files on configure" ON)
if(GENERATE_VSCODE)
  # 工作区目录为当前 CMake 源目录的上一级
  get_filename_component(WORKSPACE_DIR "${CMAKE_SOURCE_DIR}" DIRECTORY)
  set(VSCODE_DIR "${WORKSPACE_DIR}/.vscode")
  file(MAKE_DIRECTORY "${VSCODE_DIR}")

  # 计算 VSCode 模板所需构建路径/配置
  if(CMAKE_CONFIGURATION_TYPES)
    # 多配置生成器（Visual Studio / Ninja Multi-Config）
    set(VSCODE_DEFAULT_CONFIG "Debug")
    set(_VSCODE_SUBDIR "Debug")
  else()
    # 单配置生成器（Ninja / Makefiles）
    if(NOT CMAKE_BUILD_TYPE)
      set(VSCODE_DEFAULT_CONFIG "Debug")
    else()
      set(VSCODE_DEFAULT_CONFIG "${CMAKE_BUILD_TYPE}")
    endif()
    set(_VSCODE_SUBDIR "")
  endif()

  if(_VSCODE_SUBDIR STREQUAL "")
    set(VSCODE_PROGRAM_REL "PdfWinViewer/build/PdfWinViewer.exe")
    set(VSCODE_CWD_REL     "PdfWinViewer/build")
  else()
    set(VSCODE_PROGRAM_REL "PdfWinViewer/build/${_VSCODE_SUBDIR}/PdfWinViewer.exe")
    set(VSCODE_CWD_REL     "PdfWinViewer/build/${_VSCODE_SUBDIR}")
  endif()

  set(LAUNCH_JSON "${VSCODE_DIR}/launch.json")
  if(NOT EXISTS "${LAUNCH_JSON}")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/templates/launch.json.in" "${LAUNCH_JSON}" @ONLY)
  else()
    # 增量合并 launch.json（按 name 去重）
    set(LAUNCH_TMP "${VSCODE_DIR}/launch.tmp.json")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/templates/launch.json.in" "${LAUNCH_TMP}" @ONLY)
    execute_process(
      COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tools/merge_json.ps1" "${LAUNCH_JSON}" "${LAUNCH_TMP}" "${LAUNCH_JSON}" name
      RESULT_VARIABLE MERGE_LAUNCH_RES
      OUTPUT_VARIABLE MERGE_LAUNCH_OUT
      ERROR_VARIABLE MERGE_LAUNCH_ERR
    )
    file(REMOVE "${LAUNCH_TMP}")
  endif()

  set(TASKS_JSON "${VSCODE_DIR}/tasks.json")
  if(NOT EXISTS "${TASKS_JSON}")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/templates/tasks.json.in" "${TASKS_JSON}" @ONLY)
  else()
    # 增量合并 tasks.json（按 label 去重）
    set(TASKS_TMP "${VSCODE_DIR}/tasks.tmp.json")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/templates/tasks.json.in" "${TASKS_TMP}" @ONLY)
    execute_process(
      COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tools/merge_json.ps1" "${TASKS_JSON}" "${TASKS_TMP}" "${TASKS_JSON}" label
      RESULT_VARIABLE MERGE_TASKS_RES
      OUTPUT_VARIABLE MERGE_TASKS_OUT
      ERROR_VARIABLE MERGE_TASKS_ERR
    )
    file(REMOVE "${TASKS_TMP}")
  endif()
endif()

# 生成 msbuild 脚本（仅当不存在时生成，使用相对路径，便于移动工程目录）
set(MSBUILD_SCRIPT_DIR "${CMAKE_SOURCE_DIR}")
set(MSBUILD_SCRIPT_A "${MSBUILD_SCRIPT_DIR}/msbuild_build_project_debug_x64.cmd")
if(NOT EXISTS "${MSBUILD_SCRIPT_A}")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/templates/msbuild_build_project_debug_x64.cmd.in" "${MSBUILD_SCRIPT_A}" @ONLY)
endif()