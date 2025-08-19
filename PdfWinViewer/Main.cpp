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
static HMENU g_hMenu = nullptr, g_hFileMenu = nullptr;

// 最近文件
static std::vector<std::wstring> g_recent;
static const UINT ID_FILE_OPEN = 1001;
static const UINT ID_RECENT_BASE = 1100; // 1100..1119
static const UINT ID_RECENT_CLEAR = 1199;
static const size_t kMaxRecent = 10;

static void EnableDPIAwareness() {
	// 优先 Per-Monitor V2，其次系统级 DPI 感知
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
	// 去重（不区分大小写）
	auto it = std::remove_if(g_recent.begin(), g_recent.end(), [&](const std::wstring& s){ return _wcsicmp(s.c_str(), path.c_str())==0; });
	g_recent.erase(it, g_recent.end());
	g_recent.insert(g_recent.begin(), path);
	if (g_recent.size() > kMaxRecent) g_recent.resize(kMaxRecent);
	SaveRecent();
}

static void UpdateRecentMenu(HWND hWnd) {
	if (!g_hFileMenu) return;
	// 先删除旧的最近项（ID_RECENT_BASE..ID_RECENT_CLEAR）
	for (UINT id = ID_RECENT_BASE; id <= ID_RECENT_CLEAR; ++id) {
		for (int i = GetMenuItemCount(g_hFileMenu)-1; i >= 0; --i) {
			MENUITEMINFOW mii{ sizeof(mii) }; mii.fMask = MIIM_ID;
			if (GetMenuItemInfoW(g_hFileMenu, i, TRUE, &mii) && mii.wID == id) {
				RemoveMenu(g_hFileMenu, i, MF_BYPOSITION);
			}
		}
	}
	// 在“Open...”后插入分隔与最近项
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

static void ClampScroll(HWND hWnd) {
	RECT rc; GetClientRect(hWnd, &rc);
	int cw = std::max(1, (int)(rc.right - rc.left));
	int ch = std::max(1, (int)(rc.bottom - rc.top));
	int maxX = std::max(0, g_pagePxW - cw);
	int maxY = std::max(0, g_pagePxH - ch);
	g_scrollX = std::min(std::max(0, g_scrollX), maxX);
	g_scrollY = std::min(std::max(0, g_scrollY), maxY);
}

static void UpdateScrollBars(HWND hWnd) {
	RECT rc; GetClientRect(hWnd, &rc);
	int cw = std::max(1, (int)(rc.right - rc.left));
	int ch = std::max(1, (int)(rc.bottom - rc.top));
	SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	si.nMin = 0; si.nMax = std::max(0, g_pagePxW - 1); si.nPage = cw; si.nPos = g_scrollX; SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
	si.nMin = 0; si.nMax = std::max(0, g_pagePxH - 1); si.nPage = ch; si.nPos = g_scrollY; SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
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

static void SetZoom(HWND hWnd, double newZoom, POINT* anchorClient) {
	newZoom = std::min(8.0, std::max(0.1, newZoom));
	if (!g_doc) { g_zoom = newZoom; return; }
	RECT rc; GetClientRect(hWnd, &rc);
	int cw = std::max(1, (int)(rc.right - rc.left));
	int ch = std::max(1, (int)(rc.bottom - rc.top));
	double oldZoom = g_zoom;
	int oldPageW = g_pagePxW; int oldPageH = g_pagePxH;
	int oldScrollX = g_scrollX; int oldScrollY = g_scrollY;
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
	RECT rc; GetClientRect(hWnd, &rc);
	int cw = std::max(1, (int)(rc.right - rc.left));
	int ch = std::max(1, (int)(rc.bottom - rc.top));
	FPDF_PAGE page = FPDF_LoadPage(g_doc, g_page_index);
	if (!page) return;
	FPDF_BITMAP bmp = FPDFBitmap_Create(cw, ch, 1);
	if (bmp) {
		FPDFBitmap_FillRect(bmp, 0, 0, cw, ch, 0xFFFFFFFF);
		int flags = FPDF_ANNOT | FPDF_LCD_TEXT;
		FPDF_RenderPageBitmap(bmp, page, -g_scrollX, -g_scrollY, g_pagePxW, g_pagePxH, 0, flags);
		void* buffer = FPDFBitmap_GetBuffer(bmp);
		BITMAPINFO bmi{}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cw;
		bmi.bmiHeader.biHeight = -ch;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, cw, ch, buffer, &bmi, DIB_RGB_COLORS, SRCCOPY);
		FPDFBitmap_Destroy(bmp);
	}
	FPDF_ClosePage(page);
}

static void OnScroll(HWND hWnd, int bar, UINT code, UINT pos) {
	RECT rc; GetClientRect(hWnd, &rc);
	int cw = std::max(1, (int)(rc.right - rc.left));
	int ch = std::max(1, (int)(rc.bottom - rc.top));
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
		// 菜单构建 + 最近文件
		g_hMenu = CreateMenu(); g_hFileMenu = CreatePopupMenu();
		AppendMenuW(g_hFileMenu, MF_STRING, ID_FILE_OPEN, L"Open...");
		LoadRecent(); UpdateRecentMenu(hWnd);
		AppendMenuW(g_hMenu, MF_POPUP, (UINT_PTR)g_hFileMenu, L"File"); SetMenu(hWnd, g_hMenu);
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
		break; }
	case WM_SIZE: {
		ClampScroll(hWnd);
		UpdateScrollBars(hWnd);
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
	// 菜单已在 WM_CREATE 中创建并填充（含最近浏览），这里不再重复创建以免覆盖
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	return 0;
}
