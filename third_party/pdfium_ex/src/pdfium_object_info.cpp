// PDFium扩展库主要实现
#include "../include/pdfium_object_info.h"
#include "pdfium_internal_access.cpp"

#include <cstring>
#include <memory>
#include <sstream>

using namespace pdfium_ex;

FPDF_EXPORT PDFIUM_EX_OBJECT_INFO* FPDF_CALLCONV 
PdfiumEx_GetPageObjectInfo(FPDF_PAGEOBJECT page_object) {
    CPDF_PageObject* pPageObj = GetInternalPageObject(page_object);
    if (!pPageObj) return nullptr;
    
    // 分配对象信息结构体
    auto* obj_info = static_cast<PDFIUM_EX_OBJECT_INFO*>(
        malloc(sizeof(PDFIUM_EX_OBJECT_INFO)));
    if (!obj_info) return nullptr;
    
    memset(obj_info, 0, sizeof(PDFIUM_EX_OBJECT_INFO));
    
    // 获取对象类型
    obj_info->obj_type = static_cast<int>(pPageObj->GetType());
    
    // 注意：这里需要页面信息来查找关联的PDF对象
    // 但是从FPDF_PAGEOBJECT无法直接获取页面信息
    // 这是PDFium API的限制，我们先实现基础功能
    
    // 初始实现：标记为内联对象，生成基础字典
    obj_info->obj_num = 0;  // 0表示内联对象
    obj_info->gen_num = 0;
    obj_info->is_indirect = 0;
    obj_info->has_stream = 0;
    
    // 生成基础的字典内容（基于页面对象的属性）
    std::ostringstream dict_stream;
    
    // 添加类型信息
    switch (pPageObj->GetType()) {
        case CPDF_PageObject::Type::kText:
            dict_stream << "/Type /Text ";
            break;
        case CPDF_PageObject::Type::kPath:
            dict_stream << "/Type /Path ";
            break;
        case CPDF_PageObject::Type::kImage:
            dict_stream << "/Type /XObject /Subtype /Image ";
            break;
        case CPDF_PageObject::Type::kShading:
            dict_stream << "/Type /Shading ";
            break;
        case CPDF_PageObject::Type::kForm:
            dict_stream << "/Type /XObject /Subtype /Form ";
            break;
    }
    
    // 添加边界框
    CFX_FloatRect bbox = pPageObj->GetRect();
    dict_stream << "/BBox [ " << std::fixed << std::setprecision(1)
                << bbox.left << " " << bbox.bottom 
                << " " << bbox.right << " " << bbox.top << " ] ";
    
    // 添加变换矩阵
    CFX_Matrix matrix = pPageObj->matrix();
    dict_stream << "/Matrix [ " << std::fixed << std::setprecision(2)
                << matrix.a << " " << matrix.b << " " << matrix.c 
                << " " << matrix.d << " " << matrix.e << " " << matrix.f << " ] ";
    
    // 根据对象类型添加特定属性
    switch (pPageObj->GetType()) {
        case CPDF_PageObject::Type::kText: {
            const CPDF_TextObject* text_obj = pPageObj->AsText();
            if (text_obj) {
                // 获取文本状态
                const CPDF_TextState& text_state = text_obj->text_state();
                dict_stream << "/FontSize " << text_state.GetFontSize() << " ";
                
                // 获取颜色状态
                const CPDF_ColorState& color_state = pPageObj->color_state();
                if (color_state.HasFillColor()) {
                    dict_stream << "/FillColor true ";
                }
                if (color_state.HasStrokeColor()) {
                    dict_stream << "/StrokeColor true ";
                }
            }
            break;
        }
        case CPDF_PageObject::Type::kPath: {
            const CPDF_PathObject* path_obj = pPageObj->AsPath();
            if (path_obj) {
                // 获取路径的填充和描边信息
                dict_stream << "/FillType " << path_obj->filltype() << " ";
                dict_stream << "/Stroke " << (path_obj->stroke() ? "true" : "false") << " ";
            }
            break;
        }
        case CPDF_PageObject::Type::kImage: {
            const CPDF_ImageObject* img_obj = pPageObj->AsImage();
            if (img_obj) {
                dict_stream << "/Subtype /Image ";
                // 可以添加更多图像特定属性
            }
            break;
        }
        default:
            break;
    }
    
    std::string dict_str = dict_stream.str();
    obj_info->dict_length = dict_str.length();
    obj_info->raw_dict_content = static_cast<char*>(
        malloc(obj_info->dict_length + 1));
    if (obj_info->raw_dict_content) {
        strcpy(obj_info->raw_dict_content, dict_str.c_str());
    }
    
    return obj_info;
}

// 尝试获取页面对象的真实对象编号（高级实现）
uint32_t GetRealPageObjectNumber(CPDF_PageObject* page_obj, CPDF_Page* page) {
    if (!page_obj || !page) return 0;
    
    // 尝试通过页面资源查找对象引用
    const CPDF_Object* pdf_obj = GetPageObjectPDFObject(page_obj, page);
    if (pdf_obj && pdf_obj->IsReference()) {
        return pdf_obj->AsReference()->GetRefObjNum();
    }
    
    return 0; // 内联对象
}

} // namespace pdfium_ex
