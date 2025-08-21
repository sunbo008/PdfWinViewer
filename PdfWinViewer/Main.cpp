#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <ShlObj.h>
#include <knownfolders.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <commctrl.h>
#include <fpdf_doc.h>
#include <fpdfview.h>
#include <fpdf_formfill.h>
#include <fpdf_edit.h>

// 某些 PDFium 版本头文件中未声明该函数，这里做前向声明以便使用
// 运行时解析以兼容未导出符号的旧版本 pdfium
static int GetPageIndexFromDest(FPDF_DOCUMENT document, FPDF_DEST dest) {
    if (!document || !dest) return -1;
    HMODULE hPdfium = GetModuleHandleW(L"pdfium.dll");
    if (!hPdfium) {
        hPdfium = LoadLibraryW(L"pdfium.dll");
        if (!hPdfium) return -1;
    }
    typedef int (FPDF_CALLCONV *PFN_GetPageIndex)(FPDF_DOCUMENT, FPDF_DEST);
    static PFN_GetPageIndex pfn = nullptr;
    if (!pfn) pfn = reinterpret_cast<PFN_GetPageIndex>(GetProcAddress(hPdfium, "FPDFDest_GetPageIndex"));
    if (!pfn) pfn = reinterpret_cast<PFN_GetPageIndex>(GetProcAddress(hPdfium, "FPDFDest_GetDestPageIndex"));
    if (!pfn) return -1;
    int idx = pfn(document, dest);
    return (idx >= 0) ? idx : -1;
}

//#pragma comment(lib, "pdfium.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowscodecs.lib")

static FPDF_DOCUMENT g_doc = nullptr;
static FPDF_FORMHANDLE g_form = nullptr;
static ULONG_PTR g_gdiplusToken = 0;
static FPDF_FORMFILLINFO g_ffi{}; // 保持整个 form 环境生命周期内有效，避免退出时解引用野指针
static int g_page_index = 0;
static int g_dpiX = 96, g_dpiY = 96;
static double g_zoom = 1.0;
static int g_scrollX = 0, g_scrollY = 0;
static int g_pagePxW = 0, g_pagePxH = 0;
static HMENU g_hMenu = nullptr, g_hFileMenu = nullptr, g_hNavMenu = nullptr;
static HWND g_hStatus = nullptr;
static HWND g_hPageLabel = nullptr;
static HWND g_hPageEdit = nullptr;
static HWND g_hPageTotal = nullptr;
static HWND g_hUpDown = nullptr;
static bool g_savingImageNow = false;
static bool g_inFileDialog = false;
static std::wstring g_currentDocPath; // 当前文档完整路径，仅用于标题栏显示
static bool g_comInited = false;
static bool g_dragging = false;       // 鼠标左键拖拽平移
static POINT g_lastDragPt{};
static HWND g_hToc = nullptr;         // 书签树
static int g_sidebarPx = 0;           // 左侧书签面板宽度（像素）
static int g_contentOriginX = 0;      // 内容绘制与命中测试的 X 偏移


static std::vector<std::wstring> g_recent;
static const UINT ID_FILE_OPEN = 1001;
static const UINT ID_RECENT_BASE = 1100; // 1100..1119
static const UINT ID_RECENT_CLEAR = 1199;
static const size_t kMaxRecent = 10;

static const UINT ID_NAV_PREV = 2001;
static const UINT ID_NAV_NEXT = 2002;
static const UINT ID_NAV_FIRST = 2003;
static const UINT ID_NAV_LAST = 2004;
static const UINT ID_NAV_GOTO = 2005;
static const UINT ID_EDIT_PAGE = 3001;
static const UINT ID_UPDOWN = 3002;
static const UINT ID_CTX_EXPORT_PNG = 4001;
static const UINT ID_CTX_SAVE_IMAGE = 4002;
static const UINT ID_CTX_PROPERTIES = 4003;

// Forward declarations for functions used before their definitions
static void RecalcPagePixelSize(HWND hWnd);
static void UpdateScrollBars(HWND hWnd);
static void UpdateStatusBarInfo(HWND hWnd);
static void LayoutStatusBarChildren(HWND hWnd);
static LRESULT CALLBACK PageEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static void GetContentClientSize(HWND hWnd, int& outWidth, int& outHeight);
static void CloseDoc();
static void InitFormEnv(HWND hWnd);
static void SetZoom(HWND hWnd, double newZoom, POINT* anchorClient);
static void RenderPageToDC(HWND hWnd, HDC hdc);
static void GetDPI(HWND hWnd);
static void ClampScroll(HWND hWnd);
static void FitWindowToPage(HWND hWnd);
static void JumpToPageFromEdit(HWND hWnd);
static void SetPageAndRefresh(HWND hWnd, int newIndex);
static bool OpenDocumentFromPath(HWND hWnd, const std::wstring& path);
// 前向声明：在 OpenDocumentFromPath 中会用到
static std::string WideToUTF8(const std::wstring& w);
static void AddRecent(const std::wstring& path);
static void UpdateRecentMenu(HWND hWnd);
static void EnsureGdiplus();
static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
static bool SaveBufferAsPng(const wchar_t* path, const void* buffer, int width, int height, int stride);
static bool SaveBufferAsJpeg(const wchar_t* path, const void* buffer, int width, int height, int stride, int quality);
static std::wstring SavePngDialog(HWND hWnd, int pageIndex);
static bool ExportCurrentPageAsPNG(HWND hWnd);
static bool IsPointOverImage(HWND hWnd, POINT clientPt);
static bool ExportImageAtPoint(HWND hWnd, POINT clientPt);
static bool SaveBinaryToFile(const wchar_t* path, const void* data, size_t size);
static std::wstring SaveDialogWithExt(HWND hWnd, const wchar_t* defName, const wchar_t* filter, const wchar_t* defExt);
static void EnsureCOM();
static void UninitCOM();
static bool SaveImageFromObject(HWND hWnd, FPDF_PAGE page, FPDF_PAGEOBJECT imgObj);
// 书签面板与跳转
static void BuildBookmarks(HWND hWnd);
static void ClearBookmarks();
static void LayoutSidebarAndContent(HWND hWnd);
struct TocItemData;

// TreeView item param
struct TocItemData { int pageIndex; FPDF_DEST dest; };

static void ClearBookmarks() {
    if (!g_hToc) return;
    // 交给 TVN_DELETEITEM 逐个释放
    TreeView_DeleteAllItems(g_hToc);
}

static void AddBookmarkRecursive(HWND hTree, HTREEITEM hParent, FPDF_DOCUMENT doc, FPDF_BOOKMARK bm) {
    while (bm) {
        // 标题
        wchar_t textW[256]{};
        unsigned long len = FPDFBookmark_GetTitle(bm, nullptr, 0);
        std::vector<wchar_t> wbuf; wbuf.resize((len + 1) / 2 + 1);
        if (len > 0) FPDFBookmark_GetTitle(bm, reinterpret_cast<FPDF_WCHAR*>(wbuf.data()), len);
        if (!wbuf.empty()) wbuf[wbuf.size()-1] = 0;
        const wchar_t* title = wbuf.empty()? L"(书签)" : wbuf.data();

        // 目标页
        int pageIndex = -1;
        FPDF_DEST dest = FPDFBookmark_GetDest(doc, bm);
        if (!dest) {
            FPDF_ACTION act = FPDFBookmark_GetAction(bm);
            if (act) dest = FPDFAction_GetDest(doc, act);
        }
        if (dest) {
            pageIndex = GetPageIndexFromDest(doc, dest);
        }

        TocItemData* data = new TocItemData{ pageIndex, dest };
        TVINSERTSTRUCTW ins{};
        ins.hParent = hParent; ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = const_cast<wchar_t*>(title);
        ins.item.lParam = reinterpret_cast<LPARAM>(data);
        HTREEITEM hNode = TreeView_InsertItem(hTree, &ins);

        // 子节点
        FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(doc, bm);
        if (child) AddBookmarkRecursive(hTree, hNode, doc, child);

        bm = FPDFBookmark_GetNextSibling(doc, bm);
    }
}

static void BuildBookmarks(HWND hWnd) {
    if (!g_hToc) return;
    ClearBookmarks();
    if (!g_doc) return;
    FPDF_BOOKMARK root = FPDFBookmark_GetFirstChild(g_doc, nullptr);
    AddBookmarkRecursive(g_hToc, TVI_ROOT, g_doc, root);
}

static void LayoutStatusBarChildren(HWND hWnd) {
    if (!g_hStatus) return;
    SendMessageW(g_hStatus, WM_SIZE, 0, 0);
    RECT rcSB{}; GetClientRect(g_hStatus, &rcSB);
    // DPI 缩放工具
    auto Dpi = [&](int v){ return MulDiv(v, g_dpiX, 96); };
    // 估算"Page:"与编辑框合适宽度（根据字体与总页数）
    int width = rcSB.right - rcSB.left;
    int margin = Dpi(6);
    HDC hdc = GetDC(g_hStatus);
    HFONT hf = (HFONT)SendMessageW(g_hStatus, WM_GETFONT, 0, 0);
    HFONT hOld = (HFONT)SelectObject(hdc, hf);
    SIZE szText{};
    std::wstring labelTxt = L"Page:";
    GetTextExtentPoint32W(hdc, labelTxt.c_str(), (int)labelTxt.size(), &szText);
    int labelW = szText.cx + Dpi(8);
    int total = (g_doc ? FPDF_GetPageCount(g_doc) : 9999); // 没文档时按 4 位估算
    int digits = 1; for (int t = std::max(1,total); t; t/=10) ++digits; // 至少 1 位
    SIZE sz888{}; GetTextExtentPoint32W(hdc, L"888888", 6, &sz888);
    int avgCharW = std::max<int>(8, (int)(sz888.cx / 6));
    int editW = avgCharW * (digits + 2) + Dpi(20);
    if (editW < Dpi(80)) editW = Dpi(80);
    if (editW > Dpi(180)) editW = Dpi(180);
    SelectObject(hdc, hOld); ReleaseDC(g_hStatus, hdc);
    // 设置状态栏分区：仅两段。0 输入区（Page:+Edit），1 右侧剩余区域用于"/ 总数"
    int leftPartRight = margin + labelW + margin + editW + margin;
    int parts[2]{ leftPartRight, -1 };
    SendMessageW(g_hStatus, SB_SETPARTS, (WPARAM)2, (LPARAM)parts);
    // 获取分区矩形并安放控件
    RECT r0{}, r1{}; SendMessageW(g_hStatus, SB_GETRECT, 0, (LPARAM)&r0); SendMessageW(g_hStatus, SB_GETRECT, 1, (LPARAM)&r1);
    int editH = (r0.bottom - r0.top) - Dpi(6);
    if (editH < Dpi(18)) editH = Dpi(18);
    if (g_hPageLabel) SetWindowPos(g_hPageLabel, nullptr, r0.left + margin, r0.top + 2, labelW, (r0.bottom - r0.top) - 4, SWP_NOZORDER);
    if (g_hPageEdit) SetWindowPos(g_hPageEdit, nullptr, r0.left + margin + labelW + margin, r0.top + 2, editW, editH, SWP_NOZORDER);
    int totalW = r1.right - r1.left - margin * 2;
    if (totalW < 40) totalW = 40;
    if (g_hPageTotal) SetWindowPos(g_hPageTotal, nullptr, r1.left + margin, r1.top + 2, totalW, (r1.bottom - r1.top) - 4, SWP_NOZORDER);
    // 根据字体自动提升状态栏最小高度，避免剪裁
    HDC hdc2 = GetDC(g_hStatus);
    HFONT hf2 = (HFONT)SendMessageW(g_hStatus, WM_GETFONT, 0, 0);
    HFONT old2 = (HFONT)SelectObject(hdc2, hf2);
    TEXTMETRIC tm{}; GetTextMetricsW(hdc2, &tm);
    SelectObject(hdc2, old2); ReleaseDC(g_hStatus, hdc2);
    int minH = tm.tmHeight + tm.tmExternalLeading + Dpi(12);
    SendMessageW(g_hStatus, SB_SETMINHEIGHT, 0, MAKELONG(minH, 0));
}

static void GetContentClientSize(HWND hWnd, int& outWidth, int& outHeight) {
	RECT rc{}; GetClientRect(hWnd, &rc);
	int cw = std::max(1, (int)(rc.right - rc.left));
	int ch = std::max(1, (int)(rc.bottom - rc.top));
	if (g_hStatus) {
		RECT rs{}; GetWindowRect(g_hStatus, &rs);
		int sbH = rs.bottom - rs.top;
		ch = std::max(1, ch - sbH);
	}
	// 预留左侧书签面板宽度
	g_contentOriginX = 0;
	if (g_hToc && IsWindowVisible(g_hToc)) {
		int sidebar = (g_sidebarPx > 0) ? g_sidebarPx : MulDiv(220, g_dpiX, 96);
		g_contentOriginX = sidebar;
		cw = std::max(1, cw - sidebar);
	}
	outWidth = cw; outHeight = ch;
}

static void FitWindowToPage(HWND hWnd) {
	if (!g_doc) return;
	// 目标客户区（内容区）应恰好容纳页面加状态栏
	int statusH = 0;
	if (g_hStatus) {
		RECT rs{}; GetWindowRect(g_hStatus, &rs);
		statusH = rs.bottom - rs.top;
	}
	int desiredCW = std::max(100, g_pagePxW);
	// 预留侧边栏宽度
	if (g_hToc && IsWindowVisible(g_hToc)) {
		int sidebar = (g_sidebarPx > 0) ? g_sidebarPx : MulDiv(220, g_dpiX, 96);
		desiredCW += sidebar;
	}
	int desiredCH = std::max(100, g_pagePxH + statusH);
	RECT wr{ 0, 0, desiredCW, desiredCH };
	DWORD style = (DWORD)GetWindowLongPtrW(hWnd, GWL_STYLE);
	DWORD exStyle = (DWORD)GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
	BOOL hasMenu = (GetMenu(hWnd) != nullptr);
	AdjustWindowRectEx(&wr, style, hasMenu, exStyle);
	int winW = wr.right - wr.left;
	int winH = wr.bottom - wr.top;
	RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
	int maxW = std::max(200, (int)(wa.right - wa.left));
	int maxH = std::max(200, (int)(wa.bottom - wa.top));
	winW = std::min(winW, maxW);
	winH = std::min(winH, maxH);
	RECT cur{}; GetWindowRect(hWnd, &cur);
	int x = cur.left, y = cur.top;
	if (x + winW > wa.right) x = wa.right - winW;
	if (y + winH > wa.bottom) y = wa.bottom - winH;
	if (x < wa.left) x = wa.left;
	if (y < wa.top) y = wa.top;
	SetWindowPos(hWnd, nullptr, x, y, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);
	// 触发布局更新
	SendMessageW(hWnd, WM_SIZE, 0, 0);
}

static void UpdateStatusBarInfo(HWND hWnd) {
	if (!g_hStatus) return;
	int total = g_doc ? FPDF_GetPageCount(g_doc) : 0;
	int cur = g_doc ? (g_page_index + 1) : 0;
	wchar_t buf[64];
	swprintf(buf, 64, L"%d", cur);
	if (g_hPageEdit) SetWindowTextW(g_hPageEdit, buf);
	swprintf(buf, 64, L"/ %d", total);
	if (g_hPageTotal) SetWindowTextW(g_hPageTotal, buf);
    // 移除右侧重复的大号文本
}

static void JumpToPageFromEdit(HWND hWnd) {
	if (!g_doc) return;
	int total = FPDF_GetPageCount(g_doc);
	if (total <= 0) return;
	wchar_t buf[32] = L""; GetWindowTextW(g_hPageEdit, buf, 31);
	int v = _wtoi(buf);
	if (v < 1 || v > total) {
		// 还原为当前页
		swprintf(buf, 32, L"%d", g_page_index + 1);
		SetWindowTextW(g_hPageEdit, buf);
		return;
	}
	SetPageAndRefresh(hWnd, v - 1);
}

// 子类过程：拦截 Enter、Esc
static LRESULT CALLBACK PageEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    HWND mainWnd = (HWND)dwRefData;
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) { JumpToPageFromEdit(mainWnd); return 0; }
        if (wParam == VK_ESCAPE) { UpdateStatusBarInfo(mainWnd); return 0; }
        // 在编辑框拥有焦点时，也支持翻页快捷键
        if (g_doc) {
            switch (wParam) {
            case VK_PRIOR: // PgUp
                SetPageAndRefresh(mainWnd, g_page_index - 1); return 0;
            case VK_NEXT:  // PgDn
                SetPageAndRefresh(mainWnd, g_page_index + 1); return 0;
            case VK_HOME:
                SetPageAndRefresh(mainWnd, 0); return 0;
            case VK_END: {
                int pc = FPDF_GetPageCount(g_doc); if (pc > 0) SetPageAndRefresh(mainWnd, pc - 1); return 0; }
            }
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, PageEditSubclassProc, 0);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void SetZoom(HWND hWnd, double newZoom, POINT* anchorClient) {
	newZoom = std::min(8.0, std::max(0.1, newZoom));
	if (!g_doc) { g_zoom = newZoom; return; }
	// 记录旧尺寸/滚动偏移
	double oldZoom = g_zoom;
	int oldPageW = g_pagePxW;
	int oldPageH = g_pagePxH;
	int oldScrollX = g_scrollX;
	int oldScrollY = g_scrollY;
	// 应用新缩放并重算
	g_zoom = newZoom;
	GetDPI(hWnd);
	RecalcPagePixelSize(hWnd);
	if (anchorClient) {
		double contentX = oldScrollX + anchorClient->x;
		double contentY = oldScrollY + anchorClient->y;
		double scale = (oldPageW > 0) ? ((double)g_pagePxW / (double)oldPageW) : (g_zoom / oldZoom);
		g_scrollX = (int)std::lround(contentX * scale - anchorClient->x);
		g_scrollY = (int)std::lround(contentY * scale - anchorClient->y);
	}
	ClampScroll(hWnd);
	UpdateScrollBars(hWnd);
	InvalidateRect(hWnd, nullptr, TRUE);
}

static void RenderPageToDC(HWND hWnd, HDC hdc) {
	if (!g_doc) return;
	int page_count = FPDF_GetPageCount(g_doc);
	if (g_page_index < 0) g_page_index = 0;
	if (g_page_index >= page_count) g_page_index = page_count - 1;
	int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
	FPDF_PAGE page = FPDF_LoadPage(g_doc, g_page_index);
	if (!page) return;
	FPDF_BITMAP bmp = FPDFBitmap_Create(cw, ch, 1);
	if (bmp) {
		FPDFBitmap_FillRect(bmp, 0, 0, cw, ch, 0xFFFFFFFF);
		int flags = FPDF_ANNOT | FPDF_LCD_TEXT;
		FPDF_RenderPageBitmap(bmp, page, -g_scrollX, -g_scrollY, g_pagePxW, g_pagePxH, 0, flags);
		void* buffer = FPDFBitmap_GetBuffer(bmp);
		BITMAPINFO bmi{}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cw; bmi.bmiHeader.biHeight = -ch; bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
		StretchDIBits(hdc, g_contentOriginX, 0, cw, ch, 0, 0, cw, ch, buffer, &bmi, DIB_RGB_COLORS, SRCCOPY);
		FPDFBitmap_Destroy(bmp);
	}
	FPDF_ClosePage(page);
}

static void EnsureGdiplus() {
    if (g_gdiplusToken == 0) {
        Gdiplus::GdiplusStartupInput si{};
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &si, nullptr);
    }
}

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
    using namespace Gdiplus;
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    std::vector<BYTE> buf(size);
    ImageCodecInfo* pImageCodecInfo = reinterpret_cast<ImageCodecInfo*>(buf.data());
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) { *pClsid = pImageCodecInfo[j].Clsid; return j; }
    }
    return -1;
}

static bool SaveBufferAsPng(const wchar_t* path, const void* buffer, int width, int height, int stride)
{
    using namespace Gdiplus;
    EnsureGdiplus();
    // PDFium 缓冲是 BGRA 排列，GDI+ 的 PixelFormat32bppARGB 在内存上也是 BGRA（小端），可直接包装
    Bitmap bmp(width, height, stride, PixelFormat32bppARGB, (BYTE*)buffer);
    CLSID clsid{}; if (GetEncoderClsid(L"image/png", &clsid) < 0) return false;
    return bmp.Save(path, &clsid) == Ok;
}

static std::wstring SavePngDialog(HWND hWnd, int pageIndex) {
    wchar_t file[MAX_PATH]{};
    swprintf(file, MAX_PATH, L"page_%d.png", pageIndex + 1);
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) return file;
    return L"";
}

static bool ExportCurrentPageAsPNG(HWND hWnd) {
    if (!g_doc) return false;
    int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
    FPDF_PAGE page = FPDF_LoadPage(g_doc, g_page_index);
    if (!page) return false;
    FPDF_BITMAP bmp = FPDFBitmap_Create(g_pagePxW, g_pagePxH, 1);
    if (!bmp) { FPDF_ClosePage(page); return false; }
    FPDFBitmap_FillRect(bmp, 0, 0, g_pagePxW, g_pagePxH, 0xFFFFFFFF);
    int flags = FPDF_ANNOT | FPDF_LCD_TEXT;
    FPDF_RenderPageBitmap(bmp, page, 0, 0, g_pagePxW, g_pagePxH, 0, flags);
    void* buf = FPDFBitmap_GetBuffer(bmp);
    int stride = FPDFBitmap_GetStride(bmp);
    std::wstring path = SavePngDialog(hWnd, g_page_index);
    bool ok = false;
    if (!path.empty()) {
		if (g_inFileDialog) { FPDFBitmap_Destroy(bmp); FPDF_ClosePage(page); return false; }
		g_inFileDialog = true;
		int wpage = FPDFBitmap_GetWidth(bmp);
		int hpage = FPDFBitmap_GetHeight(bmp);
		ok = SaveBufferAsPng(path.c_str(), buf, wpage, hpage, stride);
		g_inFileDialog = false;
	}
    FPDFBitmap_Destroy(bmp);
    FPDF_ClosePage(page);
    return ok;
}

// 矩阵工具（同 FS_MATRIX 语义）：
static FS_MATRIX MatrixMultiply(const FS_MATRIX& m1, const FS_MATRIX& m2) {
	FS_MATRIX r{};
	r.a = (float)(m1.a * m2.a + m1.c * m2.b);
	r.b = (float)(m1.b * m2.a + m1.d * m2.b);
	r.c = (float)(m1.a * m2.c + m1.c * m2.d);
	r.d = (float)(m1.b * m2.c + m1.d * m2.d);
	r.e = (float)(m1.a * m2.e + m1.c * m2.f + m1.e);
	r.f = (float)(m1.b * m2.e + m1.d * m2.f + m1.f);
	return r;
}
static POINTF TransformPoint(const FS_MATRIX& m, float x, float y) {
	POINTF p; p.x = (float)(m.a * x + m.c * y + m.e); p.y = (float)(m.b * x + m.d * y + m.f); return p;
}
struct ImageCandidate {
	FPDF_PAGEOBJECT obj{}; float minx{}, miny{}, maxx{}, maxy{}; int bpp{}; int colorspace{}; float area{};
};
static void CollectImagesRecursive(FPDF_PAGE page, FPDF_PAGEOBJECT obj, const FS_MATRIX& parent, std::vector<ImageCandidate>& out) {
	FS_MATRIX local{}; local.a=1; local.d=1; // identity
	FPDFPageObj_GetMatrix(obj, &local);
	FS_MATRIX M = MatrixMultiply(parent, local);
	int type = FPDFPageObj_GetType(obj);
	if (type == FPDF_PAGEOBJ_IMAGE) {
		float l=0,b=0,r=0,t=0; if (!FPDFPageObj_GetBounds(obj, &l, &b, &r, &t)) return;
		POINTF p1 = TransformPoint(M, l, b);
		POINTF p2 = TransformPoint(M, r, b);
		POINTF p3 = TransformPoint(M, r, t);
		POINTF p4 = TransformPoint(M, l, t);
		ImageCandidate c{}; c.obj = obj;
		c.minx = std::min(std::min(p1.x, p2.x), std::min(p3.x, p4.x));
		c.maxx = std::max(std::max(p1.x, p2.x), std::max(p3.x, p4.x));
		c.miny = std::min(std::min(p1.y, p2.y), std::min(p3.y, p4.y));
		c.maxy = std::max(std::max(p1.y, p2.y), std::max(p3.y, p4.y));
		FPDF_IMAGEOBJ_METADATA md{}; if (FPDFImageObj_GetImageMetadata(obj, page, &md)) { c.bpp = (int)md.bits_per_pixel; c.colorspace = md.colorspace; }
		c.area = std::max(0.0f, (c.maxx - c.minx)) * std::max(0.0f, (c.maxy - c.miny));
		out.push_back(c);
		return;
	}
	if (type == FPDF_PAGEOBJ_FORM) {
		int n = FPDFFormObj_CountObjects(obj);
		for (int i=0;i<n;++i) {
			FPDF_PAGEOBJECT child = FPDFFormObj_GetObject(obj, (unsigned long)i);
			if (child) CollectImagesRecursive(page, child, M, out);
		}
	}
}
static FPDF_PAGEOBJECT FindImageAtPoint(FPDF_PAGE page, double pageX, double pageY, double pageHeight) {
	// 直接使用对象的旋转边界在页坐标命中，避免递归误差
	int count = FPDFPage_CountObjects(page);
	float px = (float)pageX; float py = (float)(pageHeight - pageY); // PDF 坐标 y 向上
	const float eps = 2.0f; // 2px 容差
	for (int i = count - 1; i >= 0; --i) { // 从上往下，优先显示在上的对象
		FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
		if (!obj) continue;
		if (FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_IMAGE) continue;
		FS_QUADPOINTSF qp{}; if (!FPDFPageObj_GetRotatedBounds(obj, &qp)) continue;
		float minx = std::min(std::min(qp.x1, qp.x2), std::min(qp.x3, qp.x4)) - eps;
		float maxx = std::max(std::max(qp.x1, qp.x2), std::max(qp.x3, qp.x4)) + eps;
		float miny = std::min(std::min(qp.y1, qp.y2), std::min(qp.y3, qp.y4)) - eps;
		float maxy = std::max(std::max(qp.y1, qp.y2), std::max(qp.y3, qp.y4)) + eps;
		if (px >= minx && px <= maxx && py >= miny && py <= maxy) return obj;
	}
	return nullptr;
}

static bool IsPointOverImage(HWND hWnd, POINT clientPt) {
	if (!g_doc) return false;
	int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
	double pageX = (clientPt.x + g_scrollX) * (72.0 / g_dpiX) / g_zoom;
	double pageY = (clientPt.y + g_scrollY) * (72.0 / g_dpiY) / g_zoom;
	FPDF_PAGE page = FPDF_LoadPage(g_doc, g_page_index);
	if (!page) return false;
	double w_pt = 0, h_pt = 0; FPDF_GetPageSizeByIndex(g_doc, g_page_index, &w_pt, &h_pt);
	FPDF_PAGEOBJECT obj = FindImageAtPoint(page, pageX, pageY, h_pt);
	FPDF_ClosePage(page);
	return obj != nullptr;
}

static bool ExportImageAtPoint(HWND hWnd, POINT clientPt) {
	if (!g_doc) return false;
	EnsureCOM();
	int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
	double pageX = (clientPt.x + g_scrollX) * (72.0 / g_dpiX) / g_zoom;
	double pageY = (clientPt.y + g_scrollY) * (72.0 / g_dpiY) / g_zoom;
	FPDF_PAGE page = FPDF_LoadPage(g_doc, g_page_index);
	if (!page) return false;
	double w_pt = 0, h_pt = 0; FPDF_GetPageSizeByIndex(g_doc, g_page_index, &w_pt, &h_pt);
	FPDF_PAGEOBJECT hitObj = FindImageAtPoint(page, pageX, pageY, h_pt);
	bool ok = false;
	if (hitObj) ok = SaveImageFromObject(hWnd, page, hitObj);
	FPDF_ClosePage(page);
	return ok;
}

static void EnsureCOM() { if (!g_comInited) { if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) g_comInited = true; } }

static void UninitCOM() { if (g_comInited) { CoUninitialize(); g_comInited = false; } }

static void UpdateWindowTitle(HWND hWnd) {
	std::wstring title = L"PDFium Win32 Viewer";
	if (!g_currentDocPath.empty()) {
		title += L" - ";
		title += PathFindFileNameW(g_currentDocPath.c_str());
	}
	SetWindowTextW(hWnd, title.c_str());
}

// 将 PDFium 位图任意格式逐行转换为 32bpp BGRA 缓冲
static void ConvertAnyToBGRA(const void* srcBuf, int width, int height, int stride, int format,
                             std::vector<unsigned char>& outBGRA) {
    outBGRA.assign((size_t)width * height * 4, 0);
    const unsigned char* srcBase = static_cast<const unsigned char*>(srcBuf);
    for (int y = 0; y < height; ++y) {
        const unsigned char* src = srcBase + (size_t)y * stride;
        unsigned char* dst = outBGRA.data() + (size_t)y * (width * 4);
        if (format == FPDFBitmap_BGRA || format == FPDFBitmap_BGRA_Premul) {
            memcpy(dst, src, (size_t)width * 4);
        } else if (format == FPDFBitmap_BGRx) {
            for (int x = 0; x < width; ++x) {
                dst[4 * x + 0] = src[4 * x + 0];
                dst[4 * x + 1] = src[4 * x + 1];
                dst[4 * x + 2] = src[4 * x + 2];
                dst[4 * x + 3] = 255;
            }
        } else if (format == FPDFBitmap_BGR) {
            for (int x = 0; x < width; ++x) {
                dst[4 * x + 0] = src[3 * x + 0];
                dst[4 * x + 1] = src[3 * x + 1];
                dst[4 * x + 2] = src[3 * x + 2];
                dst[4 * x + 3] = 255;
            }
        } else if (format == FPDFBitmap_Gray) {
            for (int x = 0; x < width; ++x) {
                unsigned char g = src[x];
                dst[4 * x + 0] = g; dst[4 * x + 1] = g; dst[4 * x + 2] = g; dst[4 * x + 3] = 255;
            }
        } else {
            // 未知格式：尽力复制
            memcpy(dst, src, (size_t)width * 4);
        }
    }
}

static bool SaveImageFromObject(HWND hWnd, FPDF_PAGE page, FPDF_PAGEOBJECT imgObj) {
	if (!page || !imgObj) return false;
	EnsureCOM();
	// 先获取可以保存的位图缓冲；若两种方式都失败，则直接返回，不弹保存框
	void* buffer = nullptr; int width = 0, height = 0, stride = 0; bool preferJpeg = false; FPDF_BITMAP hold = nullptr;
	int nfilters = FPDFImageObj_GetImageFilterCount(imgObj);
	for (int k = 0; k < nfilters; ++k) { char name[32]{}; if (FPDFImageObj_GetImageFilter(imgObj, k, name, sizeof(name))>0) { if (_stricmp(name, "DCTDecode")==0) { preferJpeg = true; break; } } }
	// 优先原始像素
	if (!buffer) {
		FPDF_BITMAP baseBmp = FPDFImageObj_GetBitmap(imgObj);
		if (baseBmp) {
			void* src = FPDFBitmap_GetBuffer(baseBmp);
			int fmt = FPDFBitmap_GetFormat(baseBmp);
			width = FPDFBitmap_GetWidth(baseBmp);
			height = FPDFBitmap_GetHeight(baseBmp);
			stride = FPDFBitmap_GetStride(baseBmp);
			static std::vector<unsigned char> convBase; convBase.clear();
			if (fmt == FPDFBitmap_BGRA || fmt == FPDFBitmap_BGRA_Premul) {
				buffer = src;
			} else {
				ConvertAnyToBGRA(src, width, height, stride, fmt, convBase);
				buffer = convBase.data();
				stride = width * 4;
			}
			hold = baseBmp;
		}
	}
	// 退回渲染（包含遮罩/矩阵）
	if (!buffer) {
		FPDF_BITMAP bmp = FPDFImageObj_GetRenderedBitmap(g_doc, page, imgObj);
		if (bmp) {
			void* src = FPDFBitmap_GetBuffer(bmp);
			int fmt = FPDFBitmap_GetFormat(bmp);
			width = FPDFBitmap_GetWidth(bmp);
			height = FPDFBitmap_GetHeight(bmp);
			stride = FPDFBitmap_GetStride(bmp);
			static std::vector<unsigned char> convR; convR.clear();
			if (fmt == FPDFBitmap_BGRA || fmt == FPDFBitmap_BGRA_Premul) {
				buffer = src;
			} else {
				ConvertAnyToBGRA(src, width, height, stride, fmt, convR);
				buffer = convR.data();
				stride = width * 4;
			}
			hold = bmp;
		}
	}
	if (!buffer || width <= 0 || height <= 0) { if (hold) FPDFBitmap_Destroy(hold); return false; }
	// 仅在确实可以保存时弹一次保存框
	std::wstring defName = preferJpeg ? L"image.jpg" : L"image.png";
	std::wstring path = preferJpeg ? SaveDialogWithExt(hWnd, defName.c_str(), L"JPEG Image (*.jpg)\0*.jpg\0\0", L"jpg")
						 : SaveDialogWithExt(hWnd, defName.c_str(), L"PNG Image (*.png)\0*.png\0\0", L"png");
	if (path.empty()) { if (hold) FPDFBitmap_Destroy(hold); return false; }
	if (g_inFileDialog) { if (hold) FPDFBitmap_Destroy(hold); return false; }
	g_inFileDialog = true;
	bool ok = preferJpeg ? SaveBufferAsJpeg(path.c_str(), buffer, width, height, stride, 90)
						 : SaveBufferAsPng(path.c_str(), buffer, width, height, stride);
	g_inFileDialog = false;
	// 若保存失败并且首选为 JPEG，尝试回退为 PNG（有些系统 JPEG 编码器可能不可用）
	if (!ok && preferJpeg) {
		std::wstring pngPath = path;
		size_t pos = pngPath.find_last_of(L'.');
		if (pos != std::wstring::npos) pngPath = pngPath.substr(0, pos) + L".png";
		g_inFileDialog = true;
		ok = SaveBufferAsPng(pngPath.c_str(), buffer, width, height, stride);
		g_inFileDialog = false;
	}
	// 反馈保存结果（临时诊断用）
	// wchar_t msg[512]; swprintf(msg, 512, L"Save image %s\nSize: %dx%d\nPath: %s", ok?L"OK":L"FAILED", width, height, (ok? path.c_str(): L"(see fallback/unknown)"));
	// MessageBoxW(hWnd, msg, L"SaveImageFromObject", ok? MB_ICONINFORMATION : MB_ICONERROR);
	if (hold) FPDFBitmap_Destroy(hold);
	return ok;
}

static bool SaveBufferAsJpeg(const wchar_t* path, const void* buffer, int width, int height, int stride, int quality)
{
    using namespace Gdiplus;
    EnsureGdiplus();
    Bitmap bmp(width, height, stride, PixelFormat32bppARGB, (BYTE*)buffer);
    CLSID clsid{}; if (GetEncoderClsid(L"image/jpeg", &clsid) < 0) return false;
    // 设置质量参数
    EncoderParameters ep{}; ep.Count = 1; EncoderParameter p{}; p.Guid = EncoderQuality; p.NumberOfValues = 1; p.Type = EncoderParameterValueTypeLong; ULONG q = (ULONG)(quality <= 0 ? 75 : quality); p.Value = &q; ep.Parameter[0] = p;
    return bmp.Save(path, &clsid, &ep) == Ok;
}

static bool SaveBinaryToFile(const wchar_t* path, const void* data, size_t size) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.good()) return false;
	out.write(static_cast<const char*>(data), size);
	out.close();
	return true;
}

static std::wstring SaveDialogWithExt(HWND hWnd, const wchar_t* defName, const wchar_t* filter, const wchar_t* defExt) {
	wchar_t file[MAX_PATH]{}; wcsncpy_s(file, defName, _TRUNCATE);
	OPENFILENAMEW ofn{ sizeof(ofn) }; ofn.hwndOwner = hWnd; ofn.lpstrFilter = filter; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrDefExt = defExt; ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	if (GetSaveFileNameW(&ofn)) return file; return L"";
}

static std::wstring SaveImageDialog(HWND hWnd, const wchar_t* baseName) {
	wchar_t file[MAX_PATH]{}; wcsncpy_s(file, baseName, _TRUNCATE);
	OPENFILENAMEW ofn{ sizeof(ofn) };
	ofn.hwndOwner = hWnd;
	ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0BMP Image (*.bmp)\0*.bmp\0\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"png";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	if (GetSaveFileNameW(&ofn)) return file; return L"";
}

static void CollectImagesFlat(FPDF_PAGE page, FPDF_PAGEOBJECT obj, std::vector<FPDF_PAGEOBJECT>& out) {
	int type = FPDFPageObj_GetType(obj);
	if (type == FPDF_PAGEOBJ_IMAGE) { out.push_back(obj); return; }
	if (type == FPDF_PAGEOBJ_FORM) {
		int n = FPDFFormObj_CountObjects(obj);
		for (int i=0;i<n;++i) {
			FPDF_PAGEOBJECT child = FPDFFormObj_GetObject(obj, (unsigned long)i);
			if (child) CollectImagesFlat(page, child, out);
		}
	}
}
static bool PageHasAnyImage(FPDF_PAGE page) {
	int count = FPDFPage_CountObjects(page);
	for (int i=0;i<count;++i) {
		FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
		if (!obj) continue;
		int t = FPDFPageObj_GetType(obj);
		if (t == FPDF_PAGEOBJ_IMAGE) return true;
		if (t == FPDF_PAGEOBJ_FORM) {
			std::vector<FPDF_PAGEOBJECT> tmp; CollectImagesFlat(page, obj, tmp);
			if (!tmp.empty()) return true;
		}
	}
	return false;
}
static FPDF_PAGEOBJECT FindLargestImage(FPDF_PAGE page) {
	std::vector<FPDF_PAGEOBJECT> all;
	int count = FPDFPage_CountObjects(page);
	for (int i=0;i<count;++i) { FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i); if (obj) CollectImagesFlat(page, obj, all); }
	FPDF_PAGEOBJECT best=nullptr; unsigned int bestPixels=0;
	for (auto obj : all) {
		unsigned int w=0,h=0; if (FPDFImageObj_GetImagePixelSize(obj,&w,&h)) { unsigned int p=w*h; if (p>bestPixels) { bestPixels=p; best=obj; } }
	}
	return best;
}

static void SetPageAndRefresh(HWND hWnd, int newIndex) {
	if (!g_doc) return;
	int page_count = FPDF_GetPageCount(g_doc);
	if (page_count <= 0) return;
	if (newIndex < 0) newIndex = 0;
	if (newIndex >= page_count) newIndex = page_count - 1;
	g_page_index = newIndex;
	g_scrollX = g_scrollY = 0;
	RecalcPagePixelSize(hWnd);
	UpdateScrollBars(hWnd);
	InvalidateRect(hWnd, nullptr, TRUE);
	UpdateStatusBarInfo(hWnd);
}


struct GotoCtx { HWND parent; HWND hwnd; HWND hEdit; int maxPage; int result; bool done; };

static LRESULT CALLBACK GotoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	GotoCtx* ctx = reinterpret_cast<GotoCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	switch (msg) {
	case WM_NCCREATE: {
		CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
		return TRUE;
	}
	case WM_CREATE: {
		ctx = reinterpret_cast<GotoCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		CreateWindowW(L"STATIC", L"Page (1 - N):", WS_CHILD | WS_VISIBLE, 10, 10, 220, 18, hwnd, nullptr, nullptr, nullptr);
		ctx->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_TABSTOP, 10, 35, 220, 24, hwnd, (HMENU)100, nullptr, nullptr);
		CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 70, 70, 70, 26, hwnd, (HMENU)IDOK, nullptr, nullptr);
		CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 160, 70, 70, 26, hwnd, (HMENU)IDCANCEL, nullptr, nullptr);
		SendMessageW(ctx->hEdit, EM_SETLIMITTEXT, 9, 0);
		SetFocus(ctx->hEdit);
		return 0;
	}
	case WM_COMMAND: {
		UINT id = LOWORD(wParam);
		if (id == IDOK) {
			wchar_t buf[32] = L""; GetWindowTextW(ctx->hEdit, buf, 31);
			int v = _wtoi(buf);
			if (v >= 1 && v <= ctx->maxPage) { ctx->result = v - 1; ctx->done = true; DestroyWindow(hwnd); }
			else { MessageBoxW(hwnd, L"Invalid page number.", L"Goto Page", MB_ICONWARNING); }
			return 0;
		}
		if (id == IDCANCEL) { ctx->result = -1; ctx->done = true; DestroyWindow(hwnd); return 0; }
		break;
	}
	case WM_CLOSE:
		ctx->result = -1; ctx->done = true; DestroyWindow(hwnd); return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int PromptGotoPage(HWND owner, int maxPage) {
	GotoCtx ctx{}; ctx.parent = owner; ctx.maxPage = maxPage; ctx.result = -1; ctx.done = false;
	WNDCLASSW wc{}; wc.lpfnWndProc = GotoWndProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"PdfWinViewerGotoWnd"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	static ATOM g_atom = 0; if (!g_atom) g_atom = RegisterClassW(&wc);
	RECT prc{}; GetWindowRect(owner, &prc);
	int w = 250, h = 120;
	int x = prc.left + ((prc.right - prc.left) - w) / 2;
	int y = prc.top + ((prc.bottom - prc.top) - h) / 2;
	HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Go to Page", WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE, x, y, w, h, owner, nullptr, wc.hInstance, &ctx);
	if (!dlg) return -1;
	EnableWindow(owner, FALSE);
	
	MSG msg{};
	while (!ctx.done && GetMessageW(&msg, nullptr, 0, 0)) {
		if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	}
	EnableWindow(owner, TRUE);
	SetForegroundWindow(owner);
	return ctx.result;
}

static void EnableDPIAwareness() {
	auto pSetCtx = (BOOL(WINAPI*)(HANDLE))GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
	if (pSetCtx) {
		pSetCtx((HANDLE)-4 /*DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2*/);
	} else {
		SetProcessDPIAware();
	}
}

static std::wstring OpenPDFDialog(HWND hWnd) {
	OPENFILENAMEW ofn = { sizeof(ofn) };
	wchar_t file[MAX_PATH] = L"";
	ofn.hwndOwner = hWnd;
	ofn.lpstrFilter = L"PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	ofn.lpstrDefExt = L"pdf";
	if (GetOpenFileNameW(&ofn)) return file;
	return L"";
}

static bool OpenDocumentFromPath(HWND hWnd, const std::wstring& path) {
    if (path.empty()) return false;
    CloseDoc();
    std::string u8 = WideToUTF8(path);
    g_doc = FPDF_LoadDocument(u8.c_str(), nullptr);
    if (!g_doc) return false;
    g_currentDocPath = path; // 记录当前文档路径用于标题栏
    int form_type = FPDF_GetFormType(g_doc);
    if (form_type == FORMTYPE_XFA_FULL || form_type == FORMTYPE_XFA_FOREGROUND) {
        FPDF_LoadXFA(g_doc);
    }
    InitFormEnv(hWnd);
    RecalcPagePixelSize(hWnd);
    UpdateScrollBars(hWnd);
    BuildBookmarks(hWnd);
    UpdateStatusBarInfo(hWnd);
    FitWindowToPage(hWnd);
    InvalidateRect(hWnd, nullptr, TRUE);
    AddRecent(path);
    UpdateRecentMenu(hWnd);
    UpdateWindowTitle(hWnd);
    if (g_hPageEdit) SetFocus(g_hPageEdit);
    return true;
}

static std::string WideToUTF8(const std::wstring& w) {
	if (w.empty()) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string u8(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), u8.data(), len, nullptr, nullptr);
	return u8;
}

static std::wstring UTF8ToWide(const std::string& s) {
	if (s.empty()) return L"";
	int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
	return w;
}

static std::filesystem::path GetRecentFilePath() {
	PWSTR p = nullptr; std::filesystem::path pth;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &p))) {
		pth = std::filesystem::path(p) / L"PdfWinViewer";
		CoTaskMemFree(p);
	}
	else {
		wchar_t buf[MAX_PATH]; GetTempPathW(MAX_PATH, buf); pth = std::filesystem::path(buf) / L"PdfWinViewer";
	}
	std::error_code ec; std::filesystem::create_directories(pth, ec);
	return pth / L"recent.txt";
}

static void LoadRecent() {
	g_recent.clear();
	auto file = GetRecentFilePath();
	std::ifstream in(file, std::ios::binary);
	if (!in.good()) return;
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back()=='\r') line.pop_back();
		std::wstring w = UTF8ToWide(line);
		if (!w.empty()) g_recent.push_back(w);
		if (g_recent.size() >= kMaxRecent) break;
	}
}

static void SaveRecent() {
	auto file = GetRecentFilePath();
	std::ofstream out(file, std::ios::binary|std::ios::trunc);
	for (auto& w : g_recent) {
		std::string u8 = WideToUTF8(w);
		out.write(u8.c_str(), (std::streamsize)u8.size());
		out.put('\n');
	}
}

static void AddRecent(const std::wstring& path) {
	auto it = std::remove_if(g_recent.begin(), g_recent.end(), [&](const std::wstring& s){ return _wcsicmp(s.c_str(), path.c_str())==0; });
	g_recent.erase(it, g_recent.end());
	g_recent.insert(g_recent.begin(), path);
	if (g_recent.size() > kMaxRecent) g_recent.resize(kMaxRecent);
	SaveRecent();
}

static void UpdateRecentMenu(HWND hWnd) {
	if (!g_hFileMenu) return;
	// D_RECENT_BASE..ID_RECENT_CLEAR
	for (UINT id = ID_RECENT_BASE; id <= ID_RECENT_CLEAR; ++id) {
		for (int i = GetMenuItemCount(g_hFileMenu)-1; i >= 0; --i) {
			MENUITEMINFOW mii{ sizeof(mii) }; mii.fMask = MIIM_ID;
			if (GetMenuItemInfoW(g_hFileMenu, i, TRUE, &mii) && mii.wID == id) {
				RemoveMenu(g_hFileMenu, i, MF_BYPOSITION);
			}
		}
	}
	
	int insertPos = 0;
	int cnt = GetMenuItemCount(g_hFileMenu);
	for (int i = 0; i < cnt; ++i) {
		MENUITEMINFOW mii{ sizeof(mii) }; mii.fMask = MIIM_ID; GetMenuItemInfoW(g_hFileMenu, i, TRUE, &mii);
		if (mii.wID == ID_FILE_OPEN) { insertPos = i + 1; break; }
	}
	InsertMenuW(g_hFileMenu, insertPos++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
	UINT id = ID_RECENT_BASE;
	for (auto &w : g_recent) {
		std::wstring text = std::wstring(L"&") + std::to_wstring(id - ID_RECENT_BASE + 1) + L"  " + std::wstring(PathFindFileNameW(w.c_str()));
		InsertMenuW(g_hFileMenu, insertPos++, MF_BYPOSITION | MF_STRING, id++, text.c_str());
		if (id >= ID_RECENT_BASE + kMaxRecent) break;
	}
	InsertMenuW(g_hFileMenu, insertPos++, MF_BYPOSITION | MF_STRING, ID_RECENT_CLEAR, L"Clear Recent");
	DrawMenuBar(hWnd);
}

static void GetDPI(HWND hWnd) {
	HDC hdc = GetDC(hWnd);
	if (hdc) {
		g_dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
		g_dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
		ReleaseDC(hWnd, hdc);
	}
}

static void RecalcPagePixelSize(HWND hWnd) {
	if (!g_doc) { g_pagePxW = g_pagePxH = 0; return; }
	double w_pt = 0, h_pt = 0;
	if (!FPDF_GetPageSizeByIndex(g_doc, g_page_index, &w_pt, &h_pt)) { g_pagePxW = g_pagePxH = 0; return; }
	g_pagePxW = std::max(1, (int)std::lround(w_pt / 72.0 * g_dpiX * g_zoom));
	g_pagePxH = std::max(1, (int)std::lround(h_pt / 72.0 * g_dpiY * g_zoom));
}

static void CloseDoc() {
    // 先销毁 form 环境，再关闭文档
    if (g_form) {
        __try { FPDFDOC_ExitFormFillEnvironment(g_form); }
        __except(EXCEPTION_EXECUTE_HANDLER) { /* 保护性：避免第三方库异常导致崩溃 */ }
        g_form = nullptr;
        ZeroMemory(&g_ffi, sizeof(g_ffi));
    }
    if (g_doc) {
        FPDF_CloseDocument(g_doc);
        g_doc = nullptr;
    }
    ClearBookmarks();
    g_page_index = 0; g_scrollX = g_scrollY = 0; g_zoom = 1.0; g_pagePxW = g_pagePxH = 0;
    g_currentDocPath.clear();
}

static void InitFormEnv(HWND hWnd) {
	ZeroMemory(&g_ffi, sizeof(g_ffi));
	g_ffi.version = 2;
	g_form = FPDFDOC_InitFormFillEnvironment(g_doc, &g_ffi);
	if (g_form) {
		FPDF_SetFormFieldHighlightColor(g_form, 0, 0xFF00FF00);
		FPDF_SetFormFieldHighlightAlpha(g_form, 100);
		FORM_DoDocumentJSAction(g_form);
		FORM_DoDocumentOpenAction(g_form);
	}
}

static void ClampScroll(HWND hWnd) {
    int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
    int maxX = std::max(0, g_pagePxW - cw);
    int maxY = std::max(0, g_pagePxH - ch);
    g_scrollX = std::min(std::max(0, g_scrollX), maxX);
    g_scrollY = std::min(std::max(0, g_scrollY), maxY);
}

static void LayoutSidebarAndContent(HWND hWnd) {
    RECT rc{}; GetClientRect(hWnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    int statusH = 0;
    if (g_hStatus) {
        RECT rs{}; GetWindowRect(g_hStatus, &rs);
        statusH = rs.bottom - rs.top;
    }
    ch = std::max(1, ch - statusH);
    int sidebar = (g_sidebarPx > 0) ? g_sidebarPx : MulDiv(220, g_dpiX, 96);
    if (g_hToc) {
        SetWindowPos(g_hToc, nullptr, 0, 0, sidebar, ch, SWP_NOZORDER | SWP_SHOWWINDOW);
    }
}

static void UpdateScrollBars(HWND hWnd) {
    int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
    cw = std::max(1, cw);
    ch = std::max(1, ch);
    SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = std::max(0, g_pagePxW - 1); si.nPage = (UINT)cw; si.nPos = std::min(g_scrollX, std::max(0, g_pagePxW - cw)); SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
    si.nMin = 0; si.nMax = std::max(0, g_pagePxH - 1); si.nPage = (UINT)ch; si.nPos = std::min(g_scrollY, std::max(0, g_pagePxH - ch)); SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
}

static void OnScroll(HWND hWnd, int bar, UINT code, UINT pos) {
    int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
    SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
    GetScrollInfo(hWnd, bar, &si);
    int cur = (bar == SB_HORZ) ? g_scrollX : g_scrollY;
    int page = (bar == SB_HORZ) ? cw : ch;
    UINT c = code;
    if (bar == SB_VERT) {
        if (c == SB_LINEUP) c = SB_LINELEFT;
        else if (c == SB_LINEDOWN) c = SB_LINERIGHT;
        else if (c == SB_PAGEUP) c = SB_PAGELEFT;
        else if (c == SB_PAGEDOWN) c = SB_PAGERIGHT;
        else if (c == SB_TOP) c = SB_LEFT;
        else if (c == SB_BOTTOM) c = SB_RIGHT;
    }
    switch (c) {
    case SB_LINELEFT:  cur -= 30; break;
    case SB_LINERIGHT: cur += 30; break;
    case SB_PAGELEFT:  cur -= page; break;
    case SB_PAGERIGHT: cur += page; break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK: cur = si.nTrackPos; break;
    case SB_LEFT:   cur = 0; break;
    case SB_RIGHT:  cur = si.nMax; break;
    default: return;
    }
    if (bar == SB_HORZ) g_scrollX = cur; else g_scrollY = cur;
    ClampScroll(hWnd);
    si.fMask = SIF_POS; si.nPos = (bar == SB_HORZ) ? g_scrollX : g_scrollY;
    SetScrollInfo(hWnd, bar, &si, TRUE);
    InvalidateRect(hWnd, nullptr, TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_CREATE: {
		EnableDPIAwareness();
		FPDF_LIBRARY_CONFIG config{}; config.version = 3; FPDF_InitLibraryWithConfig(&config);
		GetDPI(hWnd);
		// 菜单构建 + 最近文件 + 导航
		g_hMenu = CreateMenu(); g_hFileMenu = CreatePopupMenu(); g_hNavMenu = CreatePopupMenu();
		AppendMenuW(g_hFileMenu, MF_STRING, ID_FILE_OPEN, L"Open...");
		LoadRecent(); UpdateRecentMenu(hWnd);
		AppendMenuW(g_hMenu, MF_POPUP, (UINT_PTR)g_hFileMenu, L"File");
		AppendMenuW(g_hNavMenu, MF_STRING, ID_NAV_PREV, L"Previous Page\tPgUp");
		AppendMenuW(g_hNavMenu, MF_STRING, ID_NAV_NEXT, L"Next Page\tPgDn");
		AppendMenuW(g_hNavMenu, MF_STRING, ID_NAV_FIRST, L"First Page\tHome");
		AppendMenuW(g_hNavMenu, MF_STRING, ID_NAV_LAST, L"Last Page\tEnd");
		AppendMenuW(g_hNavMenu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(g_hNavMenu, MF_STRING, ID_NAV_GOTO, L"Go to Page...\tCtrl+G");
		AppendMenuW(g_hMenu, MF_POPUP, (UINT_PTR)g_hNavMenu, L"Navigate");
		SetMenu(hWnd, g_hMenu);
		// 启用拖放
		DragAcceptFiles(hWnd, TRUE);
		// 左侧书签树
		g_hToc = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
			WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS,
			0, 0, 200, 100, hWnd, (HMENU)(INT_PTR)20001, GetModuleHandleW(nullptr), nullptr);
		// 状态栏与页码控件
		INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
		InitCommonControlsEx(&icc);
		g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)100, GetModuleHandleW(nullptr), nullptr);
		// 在状态栏文本之外，放置独立的页码控件（主窗口子控件）
		g_hPageLabel = CreateWindowW(L"STATIC", L"Page:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 44, 18, g_hStatus, nullptr, nullptr, nullptr);
		g_hPageEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 116, 26, g_hStatus, (HMENU)(INT_PTR)ID_EDIT_PAGE, nullptr, nullptr);
		g_hPageTotal = CreateWindowW(L"STATIC", L"/ 0", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 160, 22, g_hStatus, nullptr, nullptr, nullptr);
		// 限制输入长度并拦截 Enter
		SendMessageW(g_hPageEdit, EM_SETLIMITTEXT, 9, 0);
		SetWindowSubclass(g_hPageEdit, PageEditSubclassProc, 0, (DWORD_PTR)hWnd);
		// 设定最小高度，保证编辑框不被裁切
		SendMessageW(g_hStatus, SB_SETMINHEIGHT, 0, MAKELONG(22, 0));
		LayoutStatusBarChildren(hWnd);
		LayoutSidebarAndContent(hWnd);
		UpdateStatusBarInfo(hWnd);
		return 0; }
	case WM_DROPFILES: {
		HDROP hDrop = (HDROP)wParam;
		wchar_t file[MAX_PATH]{};
		if (DragQueryFileW(hDrop, 0, file, MAX_PATH)) {
			std::wstring path = file;
			OpenDocumentFromPath(hWnd, path);
		}
		DragFinish(hDrop);
		return 0;
	}
	case WM_CONTEXTMENU: {
		// 在客户区内弹出右键菜单
		POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		POINT clientPt = pt; ScreenToClient(hWnd, &clientPt);
		bool enableSave = false;
		if (g_doc) {
			FPDF_PAGE pg = FPDF_LoadPage(g_doc, g_page_index);
			if (pg) {
				double pageX = (clientPt.x + g_scrollX) * (72.0 / g_dpiX) / g_zoom;
				double pageY = (clientPt.y + g_scrollY) * (72.0 / g_dpiY) / g_zoom;
				double w_pt = 0, h_pt = 0; FPDF_GetPageSizeByIndex(g_doc, g_page_index, &w_pt, &h_pt);
				FPDF_PAGEOBJECT obj = FindImageAtPoint(pg, pageX, pageY, h_pt);
				enableSave = (obj != nullptr);
				FPDF_ClosePage(pg);
			}
		}
		HMENU hPopup = CreatePopupMenu();
		AppendMenuW(hPopup, MF_STRING | (g_doc ? 0 : MF_GRAYED), ID_CTX_EXPORT_PNG, L"导出当前页为 PNG...");
		// 仅当点击位置命中图片时可用；不再提供"回退最大图片"
		AppendMenuW(hPopup, MF_STRING | ((g_doc && enableSave) ? 0 : MF_GRAYED), ID_CTX_SAVE_IMAGE, L"保存图片...");
		AppendMenuW(hPopup, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(hPopup, MF_STRING | (g_doc ? 0 : MF_GRAYED), ID_CTX_PROPERTIES, L"属性...");
		int cmd = TrackPopupMenu(hPopup, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
		DestroyMenu(hPopup);
		if (cmd == ID_CTX_EXPORT_PNG) { ExportCurrentPageAsPNG(hWnd); }
		else if (cmd == ID_CTX_SAVE_IMAGE) {
			if (g_doc) {
				if (g_savingImageNow) { return 0; }
				g_savingImageNow = true;
				FPDF_PAGE pg = FPDF_LoadPage(g_doc, g_page_index);
				if (pg) {
					double pageX = (clientPt.x + g_scrollX) * (72.0 / g_dpiX) / g_zoom;
					double pageY = (clientPt.y + g_scrollY) * (72.0 / g_dpiY) / g_zoom;
					double w_pt = 0, h_pt = 0; FPDF_GetPageSizeByIndex(g_doc, g_page_index, &w_pt, &h_pt);
					FPDF_PAGEOBJECT obj = FindImageAtPoint(pg, pageX, pageY, h_pt);
					if (obj) { SaveImageFromObject(hWnd, pg, obj); }
					FPDF_ClosePage(pg);
				}
				g_savingImageNow = false;
			}
		}
		else if (cmd == ID_CTX_PROPERTIES) {
			if (g_doc) {
				int pages = FPDF_GetPageCount(g_doc);
				std::wstring name = g_currentDocPath.empty()? L"(未命名)": PathFindFileNameW(g_currentDocPath.c_str());
				wchar_t buf[1024];
				swprintf(buf, 1024, L"文件: %s\n路径: %s\n页数: %d", name.c_str(), g_currentDocPath.c_str(), pages);
				MessageBoxW(hWnd, buf, L"文档属性", MB_OK | MB_ICONINFORMATION);
			}
		}
		return 0;
	}
	case WM_DPICHANGED: {
		UINT newDpiX = LOWORD(wParam); UINT newDpiY = HIWORD(wParam);
		g_dpiX = (int)newDpiX; g_dpiY = (int)newDpiY;
		RECT* prc = (RECT*)lParam; SetWindowPos(hWnd, nullptr, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top, SWP_NOZORDER | SWP_NOACTIVATE);
		RecalcPagePixelSize(hWnd); UpdateScrollBars(hWnd); InvalidateRect(hWnd, nullptr, TRUE);
		return 0;
	}
	case WM_COMMAND: {
		UINT id = LOWORD(wParam);
		if (id == ID_FILE_OPEN) {
			std::wstring path = OpenPDFDialog(hWnd);
			if (!path.empty()) { OpenDocumentFromPath(hWnd, path); }
			return 0;
		}
		// TreeView 通知处理：点击书签跳页
		if (HIWORD(wParam) == 0 && (HWND)lParam == g_hToc) {
			// no-op
		}
		if (id == ID_NAV_PREV && g_doc) { SetPageAndRefresh(hWnd, g_page_index - 1); return 0; }
		if (id == ID_NAV_NEXT && g_doc) { SetPageAndRefresh(hWnd, g_page_index + 1); return 0; }
		if (id == ID_NAV_FIRST && g_doc) { SetPageAndRefresh(hWnd, 0); return 0; }
		if (id == ID_NAV_LAST && g_doc) { int pc = FPDF_GetPageCount(g_doc); if (pc > 0) SetPageAndRefresh(hWnd, pc - 1); return 0; }
		if (id == ID_NAV_GOTO && g_doc) {
			int pc = FPDF_GetPageCount(g_doc);
			int idx = PromptGotoPage(hWnd, pc);
			if (idx >= 0) SetPageAndRefresh(hWnd, idx);
			return 0;
		}
		if (id == ID_RECENT_CLEAR) { g_recent.clear(); SaveRecent(); UpdateRecentMenu(hWnd); return 0; }
		if (id >= ID_RECENT_BASE && id < ID_RECENT_BASE + kMaxRecent) {
			UINT idx = id - ID_RECENT_BASE; if (idx < g_recent.size()) {
				std::wstring path = g_recent[idx];
				if (PathFileExistsW(path.c_str())) {
					OpenDocumentFromPath(hWnd, path);
				}
			}
			return 0;
		}
		// 编辑框失焦→尝试跳页
		if (id == ID_EDIT_PAGE && HIWORD(wParam) == EN_KILLFOCUS) { JumpToPageFromEdit(hWnd); return 0; }
		break; }
	case WM_KEYDOWN: {
		if (!g_doc) break;
		switch (wParam) {
		case VK_PRIOR: // PgUp
			SetPageAndRefresh(hWnd, g_page_index - 1); return 0;
		case VK_NEXT: // PgDn
			SetPageAndRefresh(hWnd, g_page_index + 1); return 0;
		case VK_HOME:
			SetPageAndRefresh(hWnd, 0); return 0;
		case VK_END: {
			int pc = FPDF_GetPageCount(g_doc); if (pc > 0) SetPageAndRefresh(hWnd, pc - 1); return 0; }
		case 'G': {
			if (GetKeyState(VK_CONTROL) & 0x8000) { int pc = FPDF_GetPageCount(g_doc); int idx = PromptGotoPage(hWnd, pc); if (idx >= 0) SetPageAndRefresh(hWnd, idx); return 0; }
			break;
		}
		}
		break;
	}
	case WM_SIZE: {
		ClampScroll(hWnd);
		UpdateScrollBars(hWnd);
		if (g_hStatus) { SendMessageW(g_hStatus, WM_SIZE, 0, 0); LayoutStatusBarChildren(hWnd); UpdateStatusBarInfo(hWnd); }
		LayoutSidebarAndContent(hWnd);
		InvalidateRect(hWnd, nullptr, TRUE);
		return 0;
	}
	case WM_HSCROLL: {
		OnScroll(hWnd, SB_HORZ, LOWORD(wParam), HIWORD(wParam));
		return 0;
	}
	case WM_VSCROLL: {
		OnScroll(hWnd, SB_VERT, LOWORD(wParam), HIWORD(wParam));
		return 0;
	}
	case WM_MOUSEWHEEL: {
		short delta = GET_WHEEL_DELTA_WPARAM(wParam);
		if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
			POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ScreenToClient(hWnd, &pt);
			double factor = (delta > 0) ? 1.1 : 0.9;
			SetZoom(hWnd, g_zoom * factor, &pt);
		} else {
			OnScroll(hWnd, SB_VERT, (delta > 0) ? SB_LINEUP : SB_LINEDOWN, 0);
		}
		return 0;
	}
	case WM_LBUTTONDOWN: {
		SetCapture(hWnd);
		g_dragging = true;
		g_lastDragPt.x = GET_X_LPARAM(lParam);
		g_lastDragPt.y = GET_Y_LPARAM(lParam);
		return 0;
	}
	case WM_MOUSEMOVE: {
		if (g_dragging) {
			POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
			int dx = pt.x - g_lastDragPt.x;
			int dy = pt.y - g_lastDragPt.y;
			g_lastDragPt = pt;
			// 鼠标向右移动，内容应向右跟随 => 滚动条减少
			g_scrollX = std::max(0, g_scrollX - dx);
			g_scrollY = std::max(0, g_scrollY - dy);
			ClampScroll(hWnd);
			UpdateScrollBars(hWnd);
			InvalidateRect(hWnd, nullptr, TRUE);
			return 0;
		}
		break;
	}
	case WM_NOTIFY: {
		LPNMHDR hdr = (LPNMHDR)lParam;
		if (hdr && hdr->hwndFrom == g_hToc) {
			if (hdr->code == TVN_DELETEITEMW) {
				LPNMTREEVIEWW tv = (LPNMTREEVIEWW)lParam;
				TocItemData* d = (TocItemData*)tv->itemOld.lParam;
				if (d) delete d;
				return 0;
			}
			if (hdr->code == TVN_SELCHANGEDW) {
				LPNMTREEVIEWW tv = (LPNMTREEVIEWW)lParam;
				TocItemData* data = (TocItemData*)(tv->itemNew.lParam);
				if (data && g_doc) {
					int idx = data->pageIndex;
					if (idx < 0 && data->dest) idx = GetPageIndexFromDest(g_doc, data->dest);
					if (idx >= 0) SetPageAndRefresh(hWnd, idx);
					else MessageBeep(MB_ICONWARNING);
				}
				return 0;
			}
			if (hdr->code == NM_DBLCLK) {
				HTREEITEM sel = TreeView_GetSelection(g_hToc);
				if (sel && g_doc) {
					TVITEMW it{}; it.mask = TVIF_PARAM; it.hItem = sel; if (TreeView_GetItem(g_hToc, &it)) {
						TocItemData* data = (TocItemData*)it.lParam;
						if (data) {
							int idx = data->pageIndex;
							if (idx < 0 && data->dest) idx = GetPageIndexFromDest(g_doc, data->dest);
							if (idx >= 0) SetPageAndRefresh(hWnd, idx);
							else MessageBeep(MB_ICONWARNING);
						}
					}
				}
				return 0;
			}
		}
		break;
	}
	case WM_LBUTTONUP: {
		if (g_dragging) {
			g_dragging = false;
			releaseCapture:
			ReleaseCapture();
		}
		return 0;
	}
	case WM_CAPTURECHANGED: {
		g_dragging = false;
		return 0;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
		RenderPageToDC(hWnd, hdc);
		EndPaint(hWnd, &ps);
		return 0;
	}
	case WM_DESTROY: {
		if (g_gdiplusToken) { Gdiplus::GdiplusShutdown(g_gdiplusToken); g_gdiplusToken = 0; }
		CloseDoc();
		FPDF_DestroyLibrary();
		UninitCOM();
		PostQuitMessage(0);
		return 0;
	}
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
	const wchar_t* cls = L"PdfWinViewerWnd";
	WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = cls; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassW(&wc);
	HWND hWnd = CreateWindowW(cls, L"PDFium Win32 Viewer", WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768, nullptr, nullptr, hInst, nullptr);
	
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	return 0;
}
