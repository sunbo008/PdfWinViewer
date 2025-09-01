// PDFium扩展库主要实现
#include "../include/pdfium_object_info.h"

// 包含内部访问代码
#include "pdfium_internal_access.cpp"

#include <cstring>
#include <memory>
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

using namespace pdfium_ex;

// 包含高级映射功能
#include "advanced_object_mapper.cpp"

FPDF_EXPORT PDFIUM_EX_OBJECT_INFO *FPDF_CALLCONV
PdfiumEx_GetPageObjectInfo(FPDF_PAGEOBJECT page_object) {
  CPDF_PageObject *pPageObj = GetInternalPageObject(page_object);
  if (!pPageObj)
    return nullptr;

  // 分配对象信息结构体
  auto *obj_info = static_cast<PDFIUM_EX_OBJECT_INFO *>(
      malloc(sizeof(PDFIUM_EX_OBJECT_INFO)));
  if (!obj_info)
    return nullptr;

  memset(obj_info, 0, sizeof(PDFIUM_EX_OBJECT_INFO));

  // 获取对象类型
  obj_info->obj_type = static_cast<int>(pPageObj->GetType());

  // 注意：从FPDF_PAGEOBJECT无法直接获取关联的CPDF_Page
  // 这是PDFium API设计的限制
  // 我们先实现基础功能，后续可以通过其他方式改进

  obj_info->obj_num = 0; // 暂时标记为内联对象
  obj_info->gen_num = 0;
  obj_info->is_indirect = 0;
  obj_info->has_stream = 0;

  // 生成基础的字典内容
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
  dict_stream << "/BBox [ " << std::fixed << std::setprecision(1) << bbox.left
              << " " << bbox.bottom << " " << bbox.right << " " << bbox.top
              << " ] ";

  // 添加变换矩阵
  CFX_Matrix matrix = pPageObj->original_matrix();
  dict_stream << "/Matrix [ " << std::fixed << std::setprecision(2) << matrix.a
              << " " << matrix.b << " " << matrix.c << " " << matrix.d << " "
              << matrix.e << " " << matrix.f << " ] ";

  std::string dict_str = dict_stream.str();
  obj_info->dict_length = dict_str.length();
  obj_info->raw_dict_content =
      static_cast<char *>(malloc(obj_info->dict_length + 1));
  if (obj_info->raw_dict_content) {
    strcpy(obj_info->raw_dict_content, dict_str.c_str());
  }

  return obj_info;
}

FPDF_EXPORT PDFIUM_EX_OBJECT_INFO *FPDF_CALLCONV
PdfiumEx_GetPageObjectInfoEx(FPDF_PAGE page, FPDF_PAGEOBJECT page_object) {
  CPDF_Page *pPage = GetInternalPage(page);
  CPDF_PageObject *pPageObj = GetInternalPageObject(page_object);
  if (!pPage || !pPageObj)
    return nullptr;

  // 分配对象信息结构体
  auto *obj_info = static_cast<PDFIUM_EX_OBJECT_INFO *>(
      malloc(sizeof(PDFIUM_EX_OBJECT_INFO)));
  if (!obj_info)
    return nullptr;

  memset(obj_info, 0, sizeof(PDFIUM_EX_OBJECT_INFO));

  // 获取对象类型
  obj_info->obj_type = static_cast<int>(pPageObj->GetType());

  // 尝试获取真实的PDF对象映射
  const CPDF_Object *pdf_obj = GetAdvancedPageObjectMapping(pPageObj, pPage);
  if (pdf_obj && pdf_obj->IsReference()) {
    // 找到了真实的间接对象
    obj_info->obj_num = pdf_obj->AsReference()->GetRefObjNum();
    obj_info->gen_num = 0; // 通常为0
    obj_info->is_indirect = 1;

    // 获取真实的对象内容
    CPDF_Document *doc = pPage->GetDocument();
    if (doc) {
      RetainPtr<CPDF_Object> real_obj =
          doc->GetOrParseIndirectObject(obj_info->obj_num);
      if (real_obj) {
        std::string dict_content = ObjectToPdfString(real_obj.Get());
        obj_info->dict_length = dict_content.length();
        obj_info->raw_dict_content =
            static_cast<char *>(malloc(obj_info->dict_length + 1));
        if (obj_info->raw_dict_content) {
          strcpy(obj_info->raw_dict_content, dict_content.c_str());
        }

        obj_info->has_stream = real_obj->IsStream() ? 1 : 0;
      }
    }
  } else {
    // 内联对象，使用基础实现
    obj_info->obj_num = 0;
    obj_info->gen_num = 0;
    obj_info->is_indirect = 0;
    obj_info->has_stream = 0;

    // 生成基础字典内容（与基础版本相同的逻辑）
    std::ostringstream dict_stream;

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

    CFX_FloatRect bbox = pPageObj->GetRect();
    dict_stream << "/BBox [ " << std::fixed << std::setprecision(1) << bbox.left
                << " " << bbox.bottom << " " << bbox.right << " " << bbox.top
                << " ] ";

    CFX_Matrix matrix = pPageObj->original_matrix();
    dict_stream << "/Matrix [ " << std::fixed << std::setprecision(2)
                << matrix.a << " " << matrix.b << " " << matrix.c << " "
                << matrix.d << " " << matrix.e << " " << matrix.f << " ] ";

    std::string dict_str = dict_stream.str();
    obj_info->dict_length = dict_str.length();
    obj_info->raw_dict_content =
        static_cast<char *>(malloc(obj_info->dict_length + 1));
    if (obj_info->raw_dict_content) {
      strcpy(obj_info->raw_dict_content, dict_str.c_str());
    }
  }

  return obj_info;
}

FPDF_EXPORT void FPDF_CALLCONV
PdfiumEx_ReleaseObjectInfo(PDFIUM_EX_OBJECT_INFO *obj_info) {
  if (!obj_info)
    return;

  if (obj_info->raw_dict_content) {
    free(obj_info->raw_dict_content);
  }
  free(obj_info);
}

FPDF_EXPORT char *FPDF_CALLCONV PdfiumEx_GetRawObjectContent(
    FPDF_DOCUMENT document, uint32_t obj_num, uint32_t gen_num) {
  CPDF_Document *pDoc = GetInternalDocument(document);
  if (!pDoc)
    return nullptr;

  // 通过对象编号获取PDF对象
  RetainPtr<CPDF_Object> obj = pDoc->GetOrParseIndirectObject(obj_num);
  if (!obj || obj->GetGenNum() != gen_num) {
    return nullptr;
  }

  // 转换为PDF格式字符串
  std::string content = ObjectToPdfString(obj.Get());

  // 分配返回字符串
  char *result = static_cast<char *>(malloc(content.length() + 1));
  if (result) {
    strcpy(result, content.c_str());
  }

  return result;
}

FPDF_EXPORT uint32_t FPDF_CALLCONV
PdfiumEx_GetPageObjectNumber(FPDF_PAGEOBJECT page_object) {
  CPDF_PageObject *pPageObj = GetInternalPageObject(page_object);
  if (!pPageObj)
    return 0;

  // 注意：这里需要页面信息才能查找真实对象编号
  // 当前API设计的限制，返回0表示内联对象
  return 0;
}

FPDF_EXPORT int FPDF_CALLCONV
PdfiumEx_IsIndirectPageObject(FPDF_PAGEOBJECT page_object) {
  return PdfiumEx_GetPageObjectNumber(page_object) > 0 ? 1 : 0;
}

FPDF_EXPORT int FPDF_CALLCONV PdfiumEx_GetPageContentStreamObjects(
    FPDF_PAGE page, uint32_t *obj_nums, int max_count) {
  CPDF_Page *pPage = GetInternalPage(page);
  if (!pPage || !obj_nums || max_count <= 0)
    return 0;

  // 获取页面字典
  const CPDF_Dictionary *page_dict = pPage->GetDict();
  if (!page_dict)
    return 0;

  // 获取Contents对象
  const CPDF_Object *contents_obj = page_dict->GetObjectFor("Contents");
  if (!contents_obj)
    return 0;

  int count = 0;

  if (contents_obj->IsReference() && count < max_count) {
    obj_nums[count++] = contents_obj->AsReference()->GetRefObjNum();
  } else if (contents_obj->IsArray()) {
    const CPDF_Array *contents_array = contents_obj->AsArray();
    for (size_t i = 0; i < contents_array->size() && count < max_count; ++i) {
      const CPDF_Object *stream_obj = contents_array->GetObjectAt(i);
      if (stream_obj && stream_obj->IsReference()) {
        obj_nums[count++] = stream_obj->AsReference()->GetRefObjNum();
      }
    }
  }

  return count;
}

FPDF_EXPORT PDFIUM_EX_OBJECT_INFO *FPDF_CALLCONV
PdfiumEx_GetPageObjectDict(FPDF_PAGE page) {
  CPDF_Page *pPage = GetInternalPage(page);
  if (!pPage)
    return nullptr;

  // 分配对象信息结构体
  auto *obj_info = static_cast<PDFIUM_EX_OBJECT_INFO *>(
      malloc(sizeof(PDFIUM_EX_OBJECT_INFO)));
  if (!obj_info)
    return nullptr;

  memset(obj_info, 0, sizeof(PDFIUM_EX_OBJECT_INFO));

  // 获取页面字典
  const CPDF_Dictionary *page_dict = pPage->GetDict();
  if (!page_dict) {
    free(obj_info);
    return nullptr;
  }

  // 页面对象总是间接对象，获取其对象编号
  obj_info->obj_num = page_dict->GetObjNum();
  obj_info->gen_num = page_dict->GetGenNum();
  obj_info->obj_type = 0; // 页面对象不是页面内容对象
  obj_info->is_indirect = 1;
  obj_info->has_stream = 0;

  // 将页面字典转换为PDF格式
  std::string dict_content = ObjectToPdfString(page_dict);
  obj_info->dict_length = dict_content.length();
  obj_info->raw_dict_content =
      static_cast<char *>(malloc(obj_info->dict_length + 1));
  if (obj_info->raw_dict_content) {
    strcpy(obj_info->raw_dict_content, dict_content.c_str());
  }

  return obj_info;
}

FPDF_EXPORT int FPDF_CALLCONV PdfiumEx_GetPageReferencedObjects(
    FPDF_PAGE page, uint32_t *obj_nums, int max_count) {
  CPDF_Page *pPage = GetInternalPage(page);
  if (!pPage || !obj_nums || max_count <= 0)
    return 0;

  const CPDF_Dictionary *page_dict = pPage->GetDict();
  if (!page_dict)
    return 0;

  int count = 0;

  // 收集页面引用的所有对象
  CPDF_DictionaryLocker locker(page_dict);
  for (const auto &pair : locker) {
    if (count >= max_count)
      break;

    const CPDF_Object *obj = pair.second.Get();
    if (obj && obj->IsReference()) {
      obj_nums[count++] = obj->AsReference()->GetRefObjNum();
    } else if (obj && obj->IsArray()) {
      // 处理数组中的引用
      const CPDF_Array *arr = obj->AsArray();
      for (size_t i = 0; i < arr->size() && count < max_count; ++i) {
        const CPDF_Object *arr_obj = arr->GetObjectAt(i);
        if (arr_obj && arr_obj->IsReference()) {
          obj_nums[count++] = arr_obj->AsReference()->GetRefObjNum();
        }
      }
    } else if (obj && obj->IsDictionary()) {
      // 递归处理字典中的引用
      const CPDF_Dictionary *sub_dict = obj->AsDictionary();
      CPDF_DictionaryLocker sub_locker(sub_dict);
      for (const auto &sub_pair : sub_locker) {
        if (count >= max_count)
          break;
        const CPDF_Object *sub_obj = sub_pair.second.Get();
        if (sub_obj && sub_obj->IsReference()) {
          obj_nums[count++] = sub_obj->AsReference()->GetRefObjNum();
        }
      }
    }
  }

  return count;
}

// ========== 对象树构建功能 ==========

// 辅助函数：创建树节点
static PDFIUM_EX_OBJECT_TREE_NODE *CreateTreeNode(uint32_t obj_num,
                                                  uint32_t gen_num,
                                                  const char *content,
                                                  int depth) {
  auto *node = static_cast<PDFIUM_EX_OBJECT_TREE_NODE *>(
      malloc(sizeof(PDFIUM_EX_OBJECT_TREE_NODE)));
  if (!node)
    return nullptr;

  memset(node, 0, sizeof(PDFIUM_EX_OBJECT_TREE_NODE));
  node->obj_num = obj_num;
  node->gen_num = gen_num;
  node->depth = depth;
  node->max_children = 50; // 初始容量，支持大型引用数组

  if (content) {
    node->content_length = strlen(content);
    node->raw_content = static_cast<char *>(malloc(node->content_length + 1));
    if (node->raw_content) {
      strcpy(node->raw_content, content);
    }
  }

  node->children = static_cast<PDFIUM_EX_OBJECT_TREE_NODE **>(
      malloc(sizeof(PDFIUM_EX_OBJECT_TREE_NODE *) * node->max_children));
  if (node->children) {
    memset(node->children, 0,
           sizeof(PDFIUM_EX_OBJECT_TREE_NODE *) * node->max_children);
  }

  return node;
}

// 辅助函数：添加子节点
static void AddChildNode(PDFIUM_EX_OBJECT_TREE_NODE *parent,
                         PDFIUM_EX_OBJECT_TREE_NODE *child) {
  if (!parent || !child || !parent->children)
    return;

  if (parent->child_count >= parent->max_children) {
    // 扩展容量
    int new_capacity = parent->max_children * 2;
    auto *new_children = static_cast<PDFIUM_EX_OBJECT_TREE_NODE **>(realloc(
        parent->children, sizeof(PDFIUM_EX_OBJECT_TREE_NODE *) * new_capacity));
    if (!new_children)
      return;

    parent->children = new_children;
    parent->max_children = new_capacity;
  }

  parent->children[parent->child_count++] = child;
}

// 队列式构建对象树（替代递归方式）
static void BuildObjectTreeWithQueue(FPDF_DOCUMENT document,
                                     PDFIUM_EX_OBJECT_TREE_NODE *root,
                                     int max_depth) {
  if (!document || !root)
    return;

  CPDF_Document *pDoc = GetInternalDocument(document);
  if (!pDoc)
    return;

  // 维护查找队列和对象树映射
  std::queue<std::pair<uint32_t, PDFIUM_EX_OBJECT_TREE_NODE *>>
      analysis_queue; // <对象号, 父节点>
  std::unordered_map<uint32_t, PDFIUM_EX_OBJECT_TREE_NODE *>
      object_tree_map; // 对象号 -> 树节点映射

  // 初始化：将Page对象加入队列和映射
  analysis_queue.push({root->obj_num, nullptr}); // root的父节点为nullptr
  object_tree_map[root->obj_num] = root;

  int processed_count = 0;
  while (!analysis_queue.empty() &&
         processed_count < 1000000) { // 限制总处理数量防止无限循环
    processed_count++;

    auto current = analysis_queue.front();
    analysis_queue.pop();

    uint32_t current_obj_num = current.first;
    PDFIUM_EX_OBJECT_TREE_NODE *current_node = object_tree_map[current_obj_num];

    if (!current_node || current_node->depth >= max_depth)
      continue;

    // 获取当前对象
    RetainPtr<const CPDF_Object> obj =
        pDoc->GetOrParseIndirectObject(current_obj_num);
    if (!obj || !obj->IsDictionary())
      continue;

    const CPDF_Dictionary *dict = obj->AsDictionary();

    // 收集所有引用的对象编号
    std::vector<uint32_t> ref_obj_nums;

    CPDF_DictionaryLocker locker(dict);
    for (const auto &pair : locker) {
      const CPDF_Object *value = pair.second.Get();
      if (!value)
        continue;

      if (value->IsReference()) {
        uint32_t ref_num = value->AsReference()->GetRefObjNum();
        if (ref_num > 0 && ref_num != current_obj_num) { // 避免自引用
          ref_obj_nums.push_back(ref_num);
        }
      } else if (value->IsArray()) {
        const CPDF_Array *arr = value->AsArray();
        for (size_t i = 0; i < arr->size() && i < 100;
             ++i) { // 支持大型注释数组
          const CPDF_Object *arr_obj = arr->GetObjectAt(i);
          if (arr_obj && arr_obj->IsReference()) {
            uint32_t ref_num = arr_obj->AsReference()->GetRefObjNum();
            if (ref_num > 0 && ref_num != current_obj_num) {
              ref_obj_nums.push_back(ref_num);
            }
          }
        }
      } else if (value->IsDictionary() &&
                 current_node->depth < 1000000) { // 只在前2层处理字典引用
        const CPDF_Dictionary *sub_dict = value->AsDictionary();
        CPDF_DictionaryLocker sub_locker(sub_dict);
        for (const auto &sub_pair : sub_locker) {
          if (ref_obj_nums.size() >= 1000000)
            break; // 限制总引用数量
          const CPDF_Object *sub_obj = sub_pair.second.Get();
          if (sub_obj && sub_obj->IsReference()) {
            uint32_t ref_num = sub_obj->AsReference()->GetRefObjNum();
            if (ref_num > 0 && ref_num != current_obj_num) {
              ref_obj_nums.push_back(ref_num);
            }
          }
        }
      }
    }

    // 为每个引用的对象创建子节点
    for (uint32_t ref_obj_num : ref_obj_nums) {
      // 检查是否已经在对象树中（避免重复）
      if (object_tree_map.find(ref_obj_num) != object_tree_map.end()) {
        continue; // 跳过已在树中的对象
      }

      // 限制子节点数量
      if (current_node->child_count >= 1000000)
        break;

      // 获取引用对象的内容
      char *content_str =
          PdfiumEx_GetRawObjectContent(document, ref_obj_num, 0);
      if (!content_str || strlen(content_str) == 0) {
        if (content_str)
          free(content_str);
        continue;
      }

      // 创建子节点
      PDFIUM_EX_OBJECT_TREE_NODE *child =
          CreateTreeNode(ref_obj_num, 0, content_str, current_node->depth + 1);
      if (child) {
        AddChildNode(current_node, child);
        object_tree_map[ref_obj_num] = child; // 加入对象树映射

        // 将新对象加入分析队列（如果深度允许）
        if (child->depth < max_depth) {
          analysis_queue.push({ref_obj_num, current_node});
        }
      }

      free(content_str);
    }
  }
}

FPDF_EXPORT PDFIUM_EX_OBJECT_TREE_NODE *FPDF_CALLCONV PdfiumEx_BuildObjectTree(
    FPDF_DOCUMENT document, FPDF_PAGE page, int max_depth) {
  if (!document || !page)
    return nullptr;

  CPDF_Page *pPage = GetInternalPage(page);
  if (!pPage)
    return nullptr;

  // 限制最大深度为1000000，允许更完整的分析
  if (max_depth <= 0 || max_depth > 1000000)
    max_depth = 1000000;

  const CPDF_Dictionary *page_dict = pPage->GetDict();
  if (!page_dict)
    return nullptr;

  // 获取页面对象编号和内容
  uint32_t page_obj_num = page_dict->GetObjNum();
  uint32_t page_gen_num = page_dict->GetGenNum();

  // 安全检查对象编号
  if (page_obj_num == 0)
    return nullptr;

  std::string page_content = ObjectToPdfString(page_dict);
  if (page_content.empty())
    return nullptr;

  // 创建根节点（页面对象）
  PDFIUM_EX_OBJECT_TREE_NODE *root =
      CreateTreeNode(page_obj_num, page_gen_num, page_content.c_str(), 0);
  if (!root)
    return nullptr;

  // 使用队列式构建对象树
  BuildObjectTreeWithQueue(document, root, max_depth);

  return root;
}

FPDF_EXPORT void FPDF_CALLCONV
PdfiumEx_ReleaseObjectTree(PDFIUM_EX_OBJECT_TREE_NODE *root) {
  if (!root)
    return;

  // 递归释放子节点
  for (int i = 0; i < root->child_count; i++) {
    PdfiumEx_ReleaseObjectTree(root->children[i]);
  }

  // 释放当前节点的资源
  if (root->raw_content) {
    free(root->raw_content);
  }
  if (root->children) {
    free(root->children);
  }
  free(root);
}