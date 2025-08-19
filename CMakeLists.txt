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
