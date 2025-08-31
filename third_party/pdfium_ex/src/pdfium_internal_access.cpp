// PDFium内部结构访问包装
// 通过包含PDFium内部头文件来访问内部数据结构

#include "../include/pdfium_object_info.h"

// 包含PDFium内部头文件
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fpdfapi/page/cpdf_pathobject.h"
#include "core/fpdfapi/page/cpdf_imageobject.h"
#include "core/fpdfapi/page/cpdf_shadingobject.h"
#include "core/fpdfapi/page/cpdf_formobject.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_string.h"
#include "core/fpdfapi/parser/cpdf_boolean.h"
#include "fpdfsdk/cpdfsdk_helpers.h"

#include <sstream>
#include <iomanip>

namespace pdfium_ex {

// 内部辅助函数：从FPDF_PAGEOBJECT获取CPDF_PageObject
CPDF_PageObject* GetInternalPageObject(FPDF_PAGEOBJECT page_object) {
    return reinterpret_cast<CPDF_PageObject*>(page_object);
}

// 内部辅助函数：从FPDF_DOCUMENT获取CPDF_Document
CPDF_Document* GetInternalDocument(FPDF_DOCUMENT document) {
    return CPDFDocumentFromFPDFDocument(document);
}

// 内部辅助函数：从FPDF_PAGE获取CPDF_Page
CPDF_Page* GetInternalPage(FPDF_PAGE page) {
    return CPDFPageFromFPDFPage(page);
}

// 内部辅助函数：将CPDF_Object转换为PDF格式字符串
std::string ObjectToPdfString(const CPDF_Object* obj, int depth = 0) {
    if (!obj || depth > 10) return "null";
    
    std::ostringstream oss;
    
    switch (obj->GetType()) {
        case CPDF_Object::kBoolean:
            oss << (obj->GetInteger() ? "true" : "false");
            break;
        case CPDF_Object::kNumber: {
            float num = obj->GetNumber();
            if (num == static_cast<int>(num)) {
                oss << static_cast<int>(num);
            } else {
                oss << std::fixed << std::setprecision(2) << num;
            }
            break;
        }
        case CPDF_Object::kString:
            oss << "(" << obj->GetString() << ")";
            break;
        case CPDF_Object::kName:
            oss << "/" << obj->GetString();
            break;
        case CPDF_Object::kArray: {
            oss << "[ ";
            const CPDF_Array* arr = obj->AsArray();
            for (size_t i = 0; i < arr->size(); ++i) {
                if (i > 0) oss << " ";
                oss << ObjectToPdfString(arr->GetObjectAt(i), depth + 1);
            }
            oss << " ]";
            break;
        }
        case CPDF_Object::kDictionary: {
            oss << "<< ";
            const CPDF_Dictionary* dict = obj->AsDictionary();
            // 使用CPDF_DictionaryLocker来遍历字典
            CPDF_DictionaryLocker locker(dict);
            for (const auto& pair : locker) {
                oss << "/" << pair.first << " " 
                    << ObjectToPdfString(pair.second.Get(), depth + 1) << " ";
            }
            oss << ">>";
            break;
        }
        case CPDF_Object::kReference: {
            const CPDF_Reference* ref = obj->AsReference();
            oss << ref->GetRefObjNum() << " 0 R";  // 生成编号通常为0
            break;
        }
        case CPDF_Object::kStream: {
            const CPDF_Stream* stream = obj->AsStream();
            oss << ObjectToPdfString(stream->GetDict(), depth + 1) << " stream\n";
            oss << "<< stream data >>\nendstream";
            break;
        }
        case CPDF_Object::kNullobj:
            oss << "null";
            break;
        default:
            oss << "unknown";
            break;
    }
    
    return oss.str();
}

// 尝试通过页面的资源字典查找对象引用
const CPDF_Object* FindObjectInPageResources(CPDF_Page* page, CPDF_PageObject* page_obj) {
    if (!page || !page_obj) return nullptr;
    
    // 获取页面字典
    const CPDF_Dictionary* page_dict = page->GetDict();
    if (!page_dict) return nullptr;
    
    // 获取资源字典
    const CPDF_Dictionary* resources = page_dict->GetDictFor("Resources");
    if (!resources) return nullptr;
    
    // 根据对象类型在不同的资源类别中查找
    switch (page_obj->GetType()) {
        case CPDF_PageObject::Type::kText: {
            // 在Font资源中查找
            const CPDF_Dictionary* fonts = resources->GetDictFor("Font");
            if (fonts) {
                // 遍历字体资源，查找匹配的对象
                CPDF_DictionaryLocker font_locker(fonts);
                for (const auto& font_pair : font_locker) {
                    const CPDF_Object* font_obj = font_pair.second.Get();
                    if (font_obj && font_obj->IsReference()) {
                        // 找到字体引用，可能与文本对象相关
                        return font_obj;
                    }
                }
            }
            break;
        }
        case CPDF_PageObject::Type::kImage: {
            // 在XObject资源中查找
            const CPDF_Dictionary* xobjects = resources->GetDictFor("XObject");
            if (xobjects) {
                CPDF_DictionaryLocker xobj_locker(xobjects);
                for (const auto& xobj_pair : xobj_locker) {
                    const CPDF_Object* xobj = xobj_pair.second.Get();
                    if (xobj && xobj->IsReference()) {
                        return xobj;
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    
    return nullptr;
}

// 尝试从页面的内容流中查找对象引用
const CPDF_Object* FindObjectInContentStream(CPDF_Page* page, CPDF_PageObject* page_obj) {
    if (!page || !page_obj) return nullptr;
    
    // 获取页面字典
    const CPDF_Dictionary* page_dict = page->GetDict();
    if (!page_dict) return nullptr;
    
    // 获取Contents对象
    const CPDF_Object* contents_obj = page_dict->GetObjectFor("Contents");
    if (!contents_obj) return nullptr;
    
    // Contents可能是数组或单个流对象
    if (contents_obj->IsReference()) {
        return contents_obj;
    } else if (contents_obj->IsArray()) {
        const CPDF_Array* contents_array = contents_obj->AsArray();
        if (contents_array->size() > 0) {
            // 返回第一个内容流对象
            return contents_array->GetObjectAt(0);
        }
    }
    
    return nullptr;
}

// 主要函数：尝试获取页面对象关联的PDF文档对象
const CPDF_Object* GetPageObjectPDFObject(CPDF_PageObject* page_obj, CPDF_Page* page) {
    if (!page_obj || !page) return nullptr;
    
    // 方法1：尝试从页面资源中查找
    const CPDF_Object* obj = FindObjectInPageResources(page, page_obj);
    if (obj) return obj;
    
    // 方法2：尝试从内容流中查找
    obj = FindObjectInContentStream(page, page_obj);
    if (obj) return obj;
    
    // 方法3：如果都找不到，说明是内联对象
    return nullptr;
}

} // namespace pdfium_ex
