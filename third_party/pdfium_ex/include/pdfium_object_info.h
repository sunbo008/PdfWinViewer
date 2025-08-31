// PDFium扩展库 - 真实PDF对象数据访问API
// 版权所有 (C) 2024 PdfWinViewer项目
// 
// 本扩展库通过包装PDFium内部API，提供访问PDF文档真实对象数据的功能
// 不修改原始PDFium代码，确保升级兼容性

#ifndef PDFIUM_EX_OBJECT_INFO_H_
#define PDFIUM_EX_OBJECT_INFO_H_

#include <stdint.h>
#include <stddef.h>
#include "fpdfview.h"

// 避免FPDF_EXPORT宏冲突
#ifdef FPDF_EXPORT
#undef FPDF_EXPORT
#endif
#define FPDF_EXPORT __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

// PDF对象真实信息结构体
typedef struct PDFIUM_EX_OBJECT_INFO {
    uint32_t obj_num;           // 真实对象编号（0表示内联对象）
    uint32_t gen_num;           // 生成编号
    int obj_type;               // 对象类型 (FPDF_PAGEOBJ_*)
    char* raw_dict_content;     // 原始字典内容（PDF格式）
    size_t dict_length;         // 字典长度
    int is_indirect;            // 是否为间接对象
    int has_stream;             // 是否包含数据流
} PDFIUM_EX_OBJECT_INFO;

// PDF对象树节点结构
typedef struct PDFIUM_EX_OBJECT_TREE_NODE {
    uint32_t obj_num;
    uint32_t gen_num;
    char* raw_content;
    size_t content_length;
    struct PDFIUM_EX_OBJECT_TREE_NODE** children;
    int child_count;
    int max_children;
    int depth;
} PDFIUM_EX_OBJECT_TREE_NODE;

// 获取页面对象的真实PDF信息
FPDF_EXPORT PDFIUM_EX_OBJECT_INFO* FPDF_CALLCONV 
PdfiumEx_GetPageObjectInfo(FPDF_PAGEOBJECT page_object);

// 获取页面对象的真实PDF信息（高级版本，需要页面上下文）
FPDF_EXPORT PDFIUM_EX_OBJECT_INFO* FPDF_CALLCONV 
PdfiumEx_GetPageObjectInfoEx(FPDF_PAGE page, FPDF_PAGEOBJECT page_object);

// 释放对象信息
FPDF_EXPORT void FPDF_CALLCONV 
PdfiumEx_ReleaseObjectInfo(PDFIUM_EX_OBJECT_INFO* obj_info);

// 通过对象编号获取完整的PDF对象原始内容
FPDF_EXPORT char* FPDF_CALLCONV 
PdfiumEx_GetRawObjectContent(FPDF_DOCUMENT document, uint32_t obj_num, uint32_t gen_num);

// 获取页面对象关联的PDF对象编号
FPDF_EXPORT uint32_t FPDF_CALLCONV 
PdfiumEx_GetPageObjectNumber(FPDF_PAGEOBJECT page_object);

// 检查页面对象是否为间接对象
FPDF_EXPORT int FPDF_CALLCONV 
PdfiumEx_IsIndirectPageObject(FPDF_PAGEOBJECT page_object);

// 获取页面对象本身的信息（/Type /Page对象）
FPDF_EXPORT PDFIUM_EX_OBJECT_INFO* FPDF_CALLCONV 
PdfiumEx_GetPageObjectDict(FPDF_PAGE page);

// 获取页面的内容流对象编号列表
FPDF_EXPORT int FPDF_CALLCONV 
PdfiumEx_GetPageContentStreamObjects(FPDF_PAGE page, uint32_t* obj_nums, int max_count);

// 获取页面引用的所有对象编号（Resources、Contents等）
FPDF_EXPORT int FPDF_CALLCONV 
PdfiumEx_GetPageReferencedObjects(FPDF_PAGE page, uint32_t* obj_nums, int max_count);

// 构建PDF对象引用树（从页面对象开始）
FPDF_EXPORT PDFIUM_EX_OBJECT_TREE_NODE* FPDF_CALLCONV 
PdfiumEx_BuildObjectTree(FPDF_DOCUMENT document, FPDF_PAGE page, int max_depth);

// 释放PDF对象树
FPDF_EXPORT void FPDF_CALLCONV 
PdfiumEx_ReleaseObjectTree(PDFIUM_EX_OBJECT_TREE_NODE* root);

#ifdef __cplusplus
}
#endif

#endif  // PDFIUM_EX_OBJECT_INFO_H_
