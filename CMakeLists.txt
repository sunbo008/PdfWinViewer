cmake_minimum_required(VERSION 3.20)
project(PdfWinViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 可通过 -DPDFIUM_ROOT=... -DPDFIUM_OUT=... 覆盖
set(PDFIUM_ROOT "D:/workspace/pdfium_20250814/pdfium" CACHE PATH "PDFium root")
set(PDFIUM_OUT  "${PDFIUM_ROOT}/out/XFA" CACHE PATH "PDFium output dir containing pdfium.dll and import lib")
set(PDFIUM_IMPORT_LIB "${PDFIUM_OUT}/pdfium.dll.lib")

add_executable(PdfWinViewer WIN32
  PdfWinViewer/Main.cpp
)

target_compile_definitions(PdfWinViewer PRIVATE UNICODE _UNICODE NOMINMAX)

target_include_directories(PdfWinViewer PRIVATE
  "${PDFIUM_ROOT}/public"
  "${PDFIUM_ROOT}"
)

target_link_directories(PdfWinViewer PRIVATE "${PDFIUM_OUT}")

target_link_libraries(PdfWinViewer PRIVATE
  "${PDFIUM_IMPORT_LIB}"
  user32
  gdi32
  comdlg32
  shlwapi
  shell32
)

# 拷贝 PDFium 运行时 dll 到目标输出目录
add_custom_command(TARGET PdfWinViewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "Copying PDFium DLLs..."
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PdfWinViewer>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${PDFIUM_OUT}/pdfium.dll
          "$<TARGET_FILE_DIR:PdfWinViewer>"
)

# 可选：拷贝同目录其他依赖 dll（采用通配处理，若失败则忽略）
file(GLOB PDFIUM_DLLS "${PDFIUM_OUT}/*.dll")
foreach(dll ${PDFIUM_DLLS})
  add_custom_command(TARGET PdfWinViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${dll}"
            "$<TARGET_FILE_DIR:PdfWinViewer>"
  )
endforeach()

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
set(MSBUILD_SCRIPT_B "${MSBUILD_SCRIPT_DIR}/msbuild_build_debug_x64.cmd")
if(NOT EXISTS "${MSBUILD_SCRIPT_A}")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/templates/msbuild_build_project_debug_x64.cmd.in" "${MSBUILD_SCRIPT_A}" @ONLY)
endif()
if(NOT EXISTS "${MSBUILD_SCRIPT_B}")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/templates/msbuild_build_debug_x64.cmd.in" "${MSBUILD_SCRIPT_B}" @ONLY)
endif()