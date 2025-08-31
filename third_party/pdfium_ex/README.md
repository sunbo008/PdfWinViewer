# PDFium扩展库 (pdfium_ex)

## 概述

PDFium扩展库是为PdfWinViewer项目开发的扩展模块，提供访问PDF文档中真实对象数据的功能。通过包装PDFium内部API，实现获取PDF对象的原始字典内容、对象编号等高级功能。

## 设计原则

1. **非侵入式**：不修改原始PDFium库代码，确保升级兼容性
2. **独立管理**：所有扩展功能集中在独立目录中
3. **向下兼容**：提供降级机制，确保基础功能可用
4. **类型安全**：使用C接口确保ABI稳定性

## 目录结构

```
pdfium_ex/
├── include/
│   └── pdfium_object_info.h       # 公共API头文件
├── src/
│   ├── pdfium_object_info_impl.cpp # 主要实现
│   └── pdfium_internal_access.cpp  # 内部访问包装
├── CMakeLists.txt                   # 构建配置
└── README.md                        # 本文档
```

## API概览

### 核心结构体

```c
typedef struct PDFIUM_EX_OBJECT_INFO {
    uint32_t obj_num;           // 真实对象编号
    uint32_t gen_num;           // 生成编号
    int obj_type;               // 对象类型
    char* raw_dict_content;     // 原始字典内容
    size_t dict_length;         // 字典长度
    int is_indirect;            // 是否为间接对象
    int has_stream;             // 是否包含数据流
} PDFIUM_EX_OBJECT_INFO;
```

### 主要API函数

- `PdfiumEx_GetPageObjectInfo()` - 获取页面对象信息
- `PdfiumEx_ReleaseObjectInfo()` - 释放对象信息
- `PdfiumEx_GetRawObjectContent()` - 获取原始对象内容
- `PdfiumEx_GetPageObjectNumber()` - 获取对象编号
- `PdfiumEx_IsIndirectPageObject()` - 检查是否为间接对象

## 使用方法

### 1. 包含头文件

```c
#include "third_party/pdfium_ex/include/pdfium_object_info.h"
```

### 2. 获取对象信息

```c
FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
PDFIUM_EX_OBJECT_INFO* info = PdfiumEx_GetPageObjectInfo(obj);

if (info) {
    if (info->is_indirect) {
        printf("%u %u obj\\n", info->obj_num, info->gen_num);
        printf("<<\\n%s\\n>>\\n", info->raw_dict_content);
        printf("endobj\\n");
    }
    
    PdfiumEx_ReleaseObjectInfo(info);
}
```

## 技术限制

### 当前限制

1. **页面对象映射**：PDFium的页面对象(CPDF_PageObject)与文档对象(CPDF_Object)之间没有直接映射关系
2. **内联对象**：大多数页面对象是内联的，没有独立的对象编号
3. **API设计**：PDFium的公共API不提供访问内部对象结构的接口

### 解决方案

1. **资源字典查找**：通过页面的Resources字典查找对象引用
2. **内容流分析**：分析页面的Contents流来建立对象关联
3. **类型特化处理**：针对不同对象类型实现特化的查找逻辑

## 未来改进

1. **深度对象分析**：实现更精确的页面对象到文档对象的映射
2. **流数据访问**：支持获取对象的原始流数据
3. **引用解析**：递归解析对象引用关系
4. **性能优化**：缓存对象映射关系，提高查找效率

## 编译要求

- C++17或更高版本
- CMake 3.20+
- 访问PDFium内部头文件

## 许可证

本扩展库遵循与主项目相同的许可证协议。
