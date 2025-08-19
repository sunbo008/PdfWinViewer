#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <ShlObj.h>
#include <knownfolders.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <commctrl.h>
#include "D:\\workspace\\pdfium_20250814\\pdfium\\public\\fpdfview.h"
#include "D:\\workspace\\pdfium_20250814\\pdfium\\public\\fpdf_formfill.h"

//#pragma comment(lib, "pdfium.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

static FPDF_DOCUMENT g_doc = nullptr;
static FPDF_FORMHANDLE g_form = nullptr;
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

// 鏈€杩戞枃浠?
static std::vector<std::wstring> g_recent;
static const UINT ID_FILE_OPEN = 1001;
static const UINT ID_RECENT_BASE = 1100; // 1100..1119
static const UINT ID_RECENT_CLEAR = 1199;
static const size_t kMaxRecent = 10;
// 瀵艰埅鍛戒护
static const UINT ID_NAV_PREV = 2001;
static const UINT ID_NAV_NEXT = 2002;
static const UINT ID_NAV_FIRST = 2003;
static const UINT ID_NAV_LAST = 2004;
static const UINT ID_NAV_GOTO = 2005;
static const UINT ID_EDIT_PAGE = 3001;
static const UINT ID_UPDOWN = 3002;

// Forward declarations for functions used before their definitions
static void RecalcPagePixelSize(HWND hWnd);
static void UpdateScrollBars(HWND hWnd);
static void UpdateStatusBarInfo(HWND hWnd);
static void LayoutStatusBarChildren(HWND hWnd);
static void GetContentClientSize(HWND hWnd, int& outWidth, int& outHeight);
static void CloseDoc();
static void InitFormEnv(HWND hWnd);
static void SetZoom(HWND hWnd, double newZoom, POINT* anchorClient);
static void RenderPageToDC(HWND hWnd, HDC hdc);
static void GetDPI(HWND hWnd);
static void ClampScroll(HWND hWnd);

static void LayoutStatusBarChildren(HWND hWnd) {
    if (!g_hStatus) return;
    SendMessageW(g_hStatus, WM_SIZE, 0, 0);
    RECT rcSB{}; GetClientRect(g_hStatus, &rcSB);
    // 设置状态栏分区：0 左侧给“Page:”与输入框，1 预留，2 右侧文本显示
    int parts[3];
    parts[0] = 200;          // 左侧区宽度
    parts[1] = 380;          // 中间区右边界
    parts[2] = -1;           // 右侧自适应
    SendMessageW(g_hStatus, SB_SETPARTS, (WPARAM)3, (LPARAM)parts);
    int x = 8; int y = 2;
    if (g_hPageLabel) SetWindowPos(g_hPageLabel, nullptr, x, y, 44, 18, SWP_NOZORDER);
    x += 48;
    if (g_hPageEdit) SetWindowPos(g_hPageEdit, nullptr, x, y - 1, 64, 22, SWP_NOZORDER);
    x += 68;
    if (g_hPageTotal) SetWindowPos(g_hPageTotal, nullptr, x, y, 100, 18, SWP_NOZORDER);
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
	outWidth = cw; outHeight = ch;
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
    // 在状态栏第3段显示整体页码文本，控件占用第1、2段
    wchar_t sb[96]; swprintf(sb, 96, L"Page: %d / %d", cur, total);
    SendMessageW(g_hStatus, SB_SETTEXTW, 2, (LPARAM)sb);
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
		StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, cw, ch, buffer, &bmi, DIB_RGB_COLORS, SRCCOPY);
		FPDFBitmap_Destroy(bmp);
	}
	FPDF_ClosePage(page);
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

// 绠€鏄撯€滆浆鍒伴〉鐮佲€濆璇濓紙鏃犺祫婧愶紝绾唬鐮佸垱寤猴級
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
	// 妯℃€佸惊鐜?
	MSG msg{};
	while (!ctx.done && GetMessageW(&msg, nullptr, 0, 0)) {
		if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	}
	EnableWindow(owner, TRUE);
	SetForegroundWindow(owner);
	return ctx.result;
}

static void EnableDPIAwareness() {
	// 浼樺厛 Per-Monitor V2锛屽叾娆＄郴缁熺骇 DPI 鎰熺煡
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
	// 鍘婚噸锛堜笉鍖哄垎澶у皬鍐欙級
	auto it = std::remove_if(g_recent.begin(), g_recent.end(), [&](const std::wstring& s){ return _wcsicmp(s.c_str(), path.c_str())==0; });
	g_recent.erase(it, g_recent.end());
	g_recent.insert(g_recent.begin(), path);
	if (g_recent.size() > kMaxRecent) g_recent.resize(kMaxRecent);
	SaveRecent();
}

static void UpdateRecentMenu(HWND hWnd) {
	if (!g_hFileMenu) return;
	// 鍏堝垹闄ゆ棫鐨勬渶杩戦」锛圛D_RECENT_BASE..ID_RECENT_CLEAR锛?
	for (UINT id = ID_RECENT_BASE; id <= ID_RECENT_CLEAR; ++id) {
		for (int i = GetMenuItemCount(g_hFileMenu)-1; i >= 0; --i) {
			MENUITEMINFOW mii{ sizeof(mii) }; mii.fMask = MIIM_ID;
			if (GetMenuItemInfoW(g_hFileMenu, i, TRUE, &mii) && mii.wID == id) {
				RemoveMenu(g_hFileMenu, i, MF_BYPOSITION);
			}
		}
	}
	// 鍦ㄢ€淥pen...鈥濆悗鎻掑叆鍒嗛殧涓庢渶杩戦」
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
	if (g_form) { FPDFDOC_ExitFormFillEnvironment(g_form); g_form = nullptr; }
	if (g_doc) { FPDF_CloseDocument(g_doc); g_doc = nullptr; }
	g_page_index = 0; g_scrollX = g_scrollY = 0; g_zoom = 1.0; g_pagePxW = g_pagePxH = 0;
}

static void InitFormEnv(HWND hWnd) {
	FPDF_FORMFILLINFO ffi{}; ffi.version = 2;
	g_form = FPDFDOC_InitFormFillEnvironment(g_doc, &ffi);
	FPDF_SetFormFieldHighlightColor(g_form, 0, 0xFF00FF00);
	FPDF_SetFormFieldHighlightAlpha(g_form, 100);
	FORM_DoDocumentJSAction(g_form);
	FORM_DoDocumentOpenAction(g_form);
}

static void ClampScroll(HWND hWnd) {
    int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
    int maxX = std::max(0, g_pagePxW - cw);
    int maxY = std::max(0, g_pagePxH - ch);
    g_scrollX = std::min(std::max(0, g_scrollX), maxX);
    g_scrollY = std::min(std::max(0, g_scrollY), maxY);
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
		// 状态栏与页码控件
		INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
		InitCommonControlsEx(&icc);
		g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)100, GetModuleHandleW(nullptr), nullptr);
		// 在状态栏文本之外，放置独立的页码控件（主窗口子控件）
		g_hPageLabel = CreateWindowW(L"STATIC", L"Page:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 44, 18, g_hStatus, nullptr, nullptr, nullptr);
		g_hPageEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 64, 22, g_hStatus, (HMENU)(INT_PTR)ID_EDIT_PAGE, nullptr, nullptr);
		g_hPageTotal = CreateWindowW(L"STATIC", L"/ 0", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 100, 18, g_hStatus, nullptr, nullptr, nullptr);
		LayoutStatusBarChildren(hWnd);
		UpdateStatusBarInfo(hWnd);
		return 0; }
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
			if (!path.empty()) {
				CloseDoc(); std::string u8 = WideToUTF8(path); g_doc = FPDF_LoadDocument(u8.c_str(), nullptr);
				if (g_doc) {
					int form_type = FPDF_GetFormType(g_doc);
					if (form_type == FORMTYPE_XFA_FULL || form_type == FORMTYPE_XFA_FOREGROUND) FPDF_LoadXFA(g_doc);
					InitFormEnv(hWnd); RecalcPagePixelSize(hWnd); UpdateScrollBars(hWnd); InvalidateRect(hWnd, nullptr, TRUE);
					AddRecent(path); UpdateRecentMenu(hWnd);
				}
			}
			return 0;
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
					CloseDoc(); std::string u8 = WideToUTF8(path); g_doc = FPDF_LoadDocument(u8.c_str(), nullptr);
					if (g_doc) { int ft = FPDF_GetFormType(g_doc); if (ft==FORMTYPE_XFA_FULL||ft==FORMTYPE_XFA_FOREGROUND) FPDF_LoadXFA(g_doc); InitFormEnv(hWnd); RecalcPagePixelSize(hWnd); UpdateScrollBars(hWnd); InvalidateRect(hWnd, nullptr, TRUE); AddRecent(path); UpdateRecentMenu(hWnd);} }
			}
			return 0;
		}
		// 页码编辑框：失焦触发跳转（避免 EN_UPDATE 的频繁回调导致重入）
		if (id == ID_EDIT_PAGE && HIWORD(wParam) == EN_KILLFOCUS) {
			wchar_t buf[32] = L""; GetWindowTextW(g_hPageEdit, buf, 31);
			int v = _wtoi(buf);
			if (v >= 1) SetPageAndRefresh(hWnd, v - 1);
			return 0;
		}
		break; }
	case WM_KEYDOWN: {
		if (!g_doc) break;
		if (GetFocus() == g_hPageEdit && wParam == VK_RETURN) {
			wchar_t buf[32] = L""; GetWindowTextW(g_hPageEdit, buf, 31);
			int v = _wtoi(buf);
			if (v >= 1) SetPageAndRefresh(hWnd, v - 1);
			return 0;
		}
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
		if (g_hStatus) {
			SendMessageW(g_hStatus, WM_SIZE, 0, 0);
			RECT rcCli{}; GetClientRect(hWnd, &rcCli);
			RECT rcSB{}; GetWindowRect(g_hStatus, &rcSB);
			int sbH = rcSB.bottom - rcSB.top;
			int x = 6; int y = rcCli.bottom - sbH + 3;
			SetWindowPos(g_hPageLabel, nullptr, x, y, 44, 20, SWP_NOZORDER);
			x += 48;
			SetWindowPos(g_hPageEdit, nullptr, x, y-2, 56, 22, SWP_NOZORDER);
			x += 60;
			SetWindowPos(g_hPageTotal, nullptr, x, y, 80, 20, SWP_NOZORDER);
			UpdateStatusBarInfo(hWnd);
		}
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
	case WM_PAINT: {
		PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
		RenderPageToDC(hWnd, hdc);
		EndPaint(hWnd, &ps);
		return 0;
	}
	case WM_DESTROY: {
		CloseDoc();
		FPDF_DestroyLibrary();
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
	// 鑿滃崟宸插湪 WM_CREATE 涓垱寤哄苟濉厖锛堝惈鏈€杩戞祻瑙堬級锛岃繖閲屼笉鍐嶉噸澶嶅垱寤轰互鍏嶈鐩?
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	return 0;
}
