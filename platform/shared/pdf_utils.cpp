#include "pdf_utils.h"
#include <algorithm>

PdfHitImageResult PdfHitImageAt(FPDF_PAGE page, double pageX, double pageY, double pageHeight, float tolerancePx) {
    PdfHitImageResult result{};
    if (!page) return result;
    
    // Convert from top-left origin to PDF coordinate system (bottom-left origin)
    float px = (float)pageX;
    float py = (float)(pageHeight - pageY);
    
    const int count = FPDFPage_CountObjects(page);
    int imageCount = 0;
    
    for (int i = count - 1; i >= 0; --i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (!obj) continue;
        
        int objType = FPDFPageObj_GetType(obj);
        if (objType != FPDF_PAGEOBJ_IMAGE) continue;
        
        imageCount++;
        
        FS_QUADPOINTSF qp{}; 
        if (!FPDFPageObj_GetRotatedBounds(obj, &qp)) continue;
        
        float minx = std::min(std::min(qp.x1, qp.x2), std::min(qp.x3, qp.x4)) - tolerancePx;
        float maxx = std::max(std::max(qp.x1, qp.x2), std::max(qp.x3, qp.x4)) + tolerancePx;
        float miny = std::min(std::min(qp.y1, qp.y2), std::min(qp.y3, qp.y4)) - tolerancePx;
        float maxy = std::max(std::max(qp.y1, qp.y2), std::max(qp.y3, qp.y4)) + tolerancePx;
        
        if (px >= minx && px <= maxx && py >= miny && py <= maxy) {
            result.imageObj = obj; result.minx = minx; result.miny = miny; result.maxx = maxx; result.maxy = maxy;
            return result;
        }
    }
    return result;
}

FPDF_BITMAP PdfAcquireBitmapForImage(FPDF_DOCUMENT doc,
                                     FPDF_PAGE page,
                                     FPDF_PAGEOBJECT imageObj,
                                     bool& outNeedsDestroy) {
    outNeedsDestroy = false;
    if (!imageObj) return nullptr;
    
    // 优先尝试获取原始高分辨率位图
    FPDF_BITMAP base = FPDFImageObj_GetBitmap(imageObj);
    if (base) {
        // 原始位图不需要释放
        outNeedsDestroy = false;
        return base;
    }
    
    // 如果原始位图不可用，则使用渲染位图作为回退
    FPDF_BITMAP rbmp = FPDFImageObj_GetRenderedBitmap(doc, page, imageObj);
    if (rbmp) {
        outNeedsDestroy = true;
        return rbmp;
    }
    
    return nullptr;
}


