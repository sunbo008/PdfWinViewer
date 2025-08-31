// Cross-platform PDFium helpers shared by macOS/Windows frontends
#pragma once

#include <fpdfview.h>
#include <fpdf_edit.h>

// Result of image hit test
struct PdfHitImageResult {
    FPDF_PAGEOBJECT imageObj {nullptr};
    float minx {0}, miny {0}, maxx {0}, maxy {0};
};

// Hit-test image object at page pixel coordinates (origin at left-bottom).
// Returns result struct with imageObj set if hit, nullptr otherwise.
// 'tolerancePx' expands bounds slightly to be more user-friendly.
PdfHitImageResult PdfHitImageAt(FPDF_PAGE page, double pageX, double pageY, double pageHeight, float tolerancePx = 2.0f);

// Try to obtain a bitmap for the given image object.
// Prefer the original embedded bitmap; if unavailable, fallback to a rendered bitmap.
// Returns nullptr on failure. If 'outNeedsDestroy' is true, the caller should
// destroy the returned bitmap via FPDFBitmap_Destroy().
FPDF_BITMAP PdfAcquireBitmapForImage(FPDF_DOCUMENT doc,
                                     FPDF_PAGE page,
                                     FPDF_PAGEOBJECT imageObj,
                                     bool& outNeedsDestroy);


