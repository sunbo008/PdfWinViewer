# PDFium扩展库构建配置
cmake_minimum_required(VERSION 3.20)

# 创建静态库
add_library(pdfium_ex STATIC
    src/pdfium_object_info_impl.cpp
)

# 设置包含目录
target_include_directories(pdfium_ex PUBLIC
    include
    ../pdfium/public
    ../pdfium/core
    ../pdfium/fpdfsdk
    ../pdfium
)

# 设置编译标准（PDFium需要C++20）
target_compile_features(pdfium_ex PRIVATE cxx_std_20)

# 设置编译选项
if(APPLE)
    target_compile_options(pdfium_ex PRIVATE
        -Wall -Wextra
        -fvisibility=hidden
        -Wno-unused-parameter
        -Wno-sign-compare
    )
endif()

# 不需要重新定义FPDF_EXPORT，在头文件中处理

# 链接到PDFium库（如果需要）
if(APPLE AND PDFIUM_STATIC)
    target_link_libraries(pdfium_ex PRIVATE ${PDFIUM_STATIC})
endif()

# 设置目标属性
set_target_properties(pdfium_ex PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
