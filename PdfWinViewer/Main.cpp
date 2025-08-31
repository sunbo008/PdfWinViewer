#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <ShlObj.h>
#include <knownfolders.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <wincodec.h>
#include <psapi.h>
#include <Richedit.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <commctrl.h>
#include <fpdf_doc.h>
#include <fpdfview.h>
#include <fpdf_formfill.h>
#include <fpdf_edit.h>
#include <fpdf_text.h>
#include "../platform/shared/pdf_utils.h"

// 直接使用公共头中的 API：FPDFDest_GetDestPageIndex

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
static HMENU g_hSettingsMenu = nullptr;
static HWND g_hStatus = nullptr;
static HWND g_hPageLabel = nullptr;
static HWND g_hPageEdit = nullptr;
static HWND g_hPageTotal = nullptr;
static HWND g_hUpDown = nullptr;
// 已取消主窗口内的日志工具栏，避免主界面出现"日志"字样
static bool g_savingImageNow = false;
static bool g_inFileDialog = false;
static std::wstring g_currentDocPath; // 当前文档完整路径，仅用于标题栏显示
static bool g_comInited = false;
static bool g_dragging = false;       // 鼠标左键拖拽平移
static POINT g_lastDragPt{};
static HWND g_hToc = nullptr;         // 书签树
static int g_sidebarPx = 0;           // 左侧书签面板宽度（像素）
static int g_contentOriginX = 0;      // 内容绘制与命中测试的 X 偏移
static int g_contentOriginY = 0;      // 顶部偏移（不使用工具栏时为 0）


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
static const UINT ID_CTX_COPY_TEXT = 4004;
static const UINT ID_SETTINGS_OPEN = 5001;
static const UINT ID_VIEW_LOG = 9001;

// Forward declarations for functions used before their definitions
static void RecalcPagePixelSize(HWND hWnd);
static void UpdateScrollBars(HWND hWnd);
static void UpdateStatusBarInfo(HWND hWnd);
static void LayoutStatusBarChildren(HWND hWnd);
static LRESULT CALLBACK PageEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static LRESULT CALLBACK TocSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static void GetContentClientSize(HWND hWnd, int& outWidth, int& outHeight);
static void CloseDoc();
static void InitFormEnv(HWND hWnd);
static void SetZoom(HWND hWnd, double newZoom, POINT* anchorClient);
static void RenderPageToDC(HWND hWnd, HDC hdc);
static void ShowLogWindow(HWND owner);
static std::wstring MakeProjectRelativePath(const std::wstring& abs);
static std::wstring MakeProjectAbsolutePath(const std::wstring& rel);
static bool ParseSourceRef(const std::wstring& text, std::wstring& outRel, int& outLine);
static bool TryOpenInCursor(const std::wstring& absPath, int line);
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

// 简易 COM 智能指针（最小实现）
template <typename T>
class ComPtr {
public:
	ComPtr() : p_(nullptr) {}
	~ComPtr() { Reset(); }
	T** operator&() { Reset(); return &p_; }
	T* operator->() const { return p_; }
	T* get() const { return p_; }
	explicit operator bool() const { return p_ != nullptr; }
	T* Detach() { T* t = p_; p_ = nullptr; return t; }
	void Reset(T* np = nullptr) { if (p_) { p_->Release(); } p_ = np; }
private:
	T* p_;
};

// CoTaskMemFree RAII 释放器
struct CoTaskMemDeleter {
	void operator()(wchar_t* p) const { if (p) CoTaskMemFree(p); }
};

// 文本选择与复制
static bool g_selecting = false;          // 是否正在拖拽选择
static bool g_hasSelection = false;       // 是否已有选区
static POINT g_selStart{};                // 选区起点（客户区坐标）
static POINT g_selEnd{};                  // 选区终点（客户区坐标）
static std::wstring g_selectedText;       // 最近一次选中的文本
static bool g_mouseDown = false;          // 是否按下左键
static bool g_movedSinceDown = false;     // 从按下以来是否移动过（用于区分点击/拖动）
static POINT g_mouseDownPt{};             // 记录按下位置

static void ClearSelection(HWND hWnd) {
	g_selecting = false;
	g_hasSelection = false;
	g_selectedText.clear();
	InvalidateRect(hWnd, nullptr, TRUE);
}

static RECT GetNormalizedClientRect(POINT a, POINT b) {
	RECT r{};
	r.left = std::min(a.x, b.x);
	r.right = std::max(a.x, b.x);
	r.top = std::min(a.y, b.y);
	r.bottom = std::max(a.y, b.y);
	return r;
}

// 将客户区坐标转换为 PDF 页面坐标（原点在左下）
static void ClientToPdfPageXY(POINT client, double& outPageX, double& outPageY, double pageHeightPt) {
	int contentX = client.x - g_contentOriginX;
	int contentY = client.y - g_contentOriginY;
	double pageX = (contentX + g_scrollX) * (72.0 / g_dpiX) / g_zoom;
	double pageYTopDown = (contentY + g_scrollY) * (72.0 / g_dpiY) / g_zoom;
	outPageX = pageX;
	outPageY = std::max(0.0, pageHeightPt - pageYTopDown);
}

static bool ExtractSelectedTextOnCurrentPage(HWND hWnd, std::wstring& outText) {
	outText.clear();
	if (!g_doc) return false;
	RECT rcClient = GetNormalizedClientRect(g_selStart, g_selEnd);
	// 限制在内容区域内
	int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
	int contentLeft = g_contentOriginX;
	int contentRight = g_contentOriginX + cw;
	RECT rcLimit{ contentLeft, 0, contentRight, ch };
	RECT rc = rcClient;
	if (!IntersectRect(&rc, &rcClient, &rcLimit)) return false;

	double w_pt = 0, h_pt = 0; if (!FPDF_GetPageSizeByIndex(g_doc, g_page_index, &w_pt, &h_pt)) return false;
	// 将两角转换为 PDF 页面坐标
	POINT p1{ rc.left, rc.top };
	POINT p2{ rc.right, rc.bottom };
	double x1=0, y1=0, x2=0, y2=0;
	ClientToPdfPageXY(p1, x1, y1, h_pt);
	ClientToPdfPageXY(p2, x2, y2, h_pt);
	double left = std::min(x1, x2);
	double right = std::max(x1, x2);
	double bottom = std::min(y1, y2);
	double top = std::max(y1, y2);

	FPDF_PAGE page = FPDF_LoadPage(g_doc, g_page_index);
	if (!page) return false;
	FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
	if (!textpage) { FPDF_ClosePage(page); return false; }

	int chars = FPDFText_GetBoundedText(textpage, left, top, right, bottom, nullptr, 0);
	bool ok = false;
	if (chars > 0) {
		std::vector<unsigned short> buf((size_t)chars + 1u);
		int got = FPDFText_GetBoundedText(textpage, left, top, right, bottom, reinterpret_cast<FPDF_WCHAR*>(buf.data()), chars);
		if (got > 0) {
			buf[(size_t)got] = 0;
			outText.assign(reinterpret_cast<wchar_t*>(buf.data()));
			ok = !outText.empty();
		}
	}
	FPDFText_ClosePage(textpage);
	FPDF_ClosePage(page);
	return ok;
}

static void CopyTextToClipboard(HWND hWnd, const std::wstring& text) {
	if (text.empty()) return;
	if (!OpenClipboard(hWnd)) return;
	EmptyClipboard();
	size_t bytes = (text.size() + 1) * sizeof(wchar_t);
	HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (hmem) {
		void* ptr = GlobalLock(hmem);
		if (ptr) {
			memcpy(ptr, text.c_str(), bytes);
			GlobalUnlock(hmem);
			SetClipboardData(CF_UNICODETEXT, hmem);
			// 不要 GlobalFree，交给剪贴板所有权
		} else {
			GlobalFree(hmem);
		}
	}
	CloseClipboard();
}

static void TryNavigateLinkAtPoint(HWND hWnd, POINT clientPt) {
	if (!g_doc) return;
	// 将点击位置转换到 PDF 页面坐标
	double w_pt = 0, h_pt = 0; if (!FPDF_GetPageSizeByIndex(g_doc, g_page_index, &w_pt, &h_pt)) return;
	double px = 0, py = 0; ClientToPdfPageXY(clientPt, px, py, h_pt);
	// FPDFLink_GetLinkAtPoint 需要 PDF 页面坐标（原点左下）
	FPDF_PAGE page = FPDF_LoadPage(g_doc, g_page_index);
	if (!page) return;
	FPDF_LINK link = FPDFLink_GetLinkAtPoint(page, px, py);
	if (link) {
		// 优先取 Dest
		FPDF_DEST dest = FPDFLink_GetDest(g_doc, link);
		if (!dest) {
			FPDF_ACTION act = FPDFLink_GetAction(link);
			if (act) dest = FPDFAction_GetDest(g_doc, act);
		}
		if (dest) {
			int pageIndex = FPDFDest_GetDestPageIndex(g_doc, dest);
			if (pageIndex >= 0) SetPageAndRefresh(hWnd, pageIndex);
		}
	}
	FPDF_ClosePage(page);
}

// ========== 设置持久化 ==========
struct AiTokenPair { std::wstring agent; std::wstring token; };
struct AppSettings { std::vector<std::wstring> kbDirs; std::vector<AiTokenPair> tokens; };
static AppSettings g_settings;

static std::filesystem::path GetSettingsFilePath() {
	std::filesystem::path dir;
	PWSTR p = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &p))) {
		dir = std::filesystem::path(p) / L"PdfWinViewer";
		CoTaskMemFree(p);
	} else {
		wchar_t buf[MAX_PATH]; GetTempPathW(MAX_PATH, buf); dir = std::filesystem::path(buf) / L"PdfWinViewer";
	}
	std::error_code ec; std::filesystem::create_directories(dir, ec);
	return dir / L"settings.json";
}

static std::wstring JsonEscape(const std::wstring& in) {
	std::wstring out; out.reserve(in.size()+8);
	for (wchar_t c : in) {
		if (c == L'\\' || c == L'"') { out.push_back(L'\\'); out.push_back(c); }
		else if (c == L'\n') { out += L"\\n"; }
		else if (c == L'\r') { out += L"\\r"; }
		else if (c == L'\t') { out += L"\\t"; }
		else { out.push_back(c); }
	}
	return out;
}

static void SaveSettings() {
	auto path = GetSettingsFilePath();
	std::wofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.good()) return;
	out << L"{\n  \"kb_dirs\": [";
	for (size_t i=0;i<g_settings.kbDirs.size();++i) {
		if (i) out << L",";
		out << L"\n    \"" << JsonEscape(g_settings.kbDirs[i]) << L"\"";
	}
	out << L"\n  ],\n  \"ai_tokens\": [";
	for (size_t i=0;i<g_settings.tokens.size();++i) {
		if (i) out << L",";
		out << L"\n    {\"agent\": \"" << JsonEscape(g_settings.tokens[i].agent) << L"\", \"token\": \"" << JsonEscape(g_settings.tokens[i].token) << L"\"}";
	}
	out << L"\n  ]\n}\n";
	out.close();
}

static void SkipSpaces(const std::wstring& s, size_t& i) { while (i<s.size() && iswspace(s[i])) ++i; }
static bool MatchLit(const std::wstring& s, size_t& i, const wchar_t* lit) { size_t j=0; while (lit[j]) { if (i>=s.size() || s[i]!=lit[j]) return false; ++i; ++j; } return true; }
static bool ReadQuoted(const std::wstring& s, size_t& i, std::wstring& out) {
	SkipSpaces(s,i); if (i>=s.size() || s[i]!=L'"') return false; ++i; out.clear();
	while (i<s.size()) { wchar_t c=s[i++]; if (c==L'"') return true; if (c==L'\\' && i<s.size()) { wchar_t e=s[i++]; if (e==L'"'||e==L'\\') out.push_back(e); else if (e==L'n') out.push_back(L'\n'); else if (e==L'r') out.push_back(L'\r'); else if (e==L't') out.push_back(L'\t'); else out.push_back(e);} else out.push_back(c);} return false; }

static void LoadSettings() {
	g_settings.kbDirs.clear(); g_settings.tokens.clear();
	auto path = GetSettingsFilePath();
	std::wifstream in(path, std::ios::binary);
	if (!in.good()) return;
	std::wstring s((std::istreambuf_iterator<wchar_t>(in)), std::istreambuf_iterator<wchar_t>());
	size_t i=0; SkipSpaces(s,i);
	// parse { "kb_dirs": [..], "ai_tokens": [..] }
	if (i>=s.size() || s[i]!=L'{') return; ++i;
	while (i<s.size()) {
		SkipSpaces(s,i); if (i<s.size() && s[i]==L'}') { ++i; break; }
		std::wstring key; if (!ReadQuoted(s,i,key)) break; SkipSpaces(s,i); if (i>=s.size()||s[i]!=L':') break; ++i; SkipSpaces(s,i);
		if (key==L"kb_dirs") {
			if (i<s.size() && s[i]==L'[') { ++i; SkipSpaces(s,i);
				while (i<s.size() && s[i]!=L']') { std::wstring val; if (!ReadQuoted(s,i,val)) break; g_settings.kbDirs.push_back(val); SkipSpaces(s,i); if (i<s.size() && s[i]==L',') { ++i; SkipSpaces(s,i);} else break; }
				if (i<s.size() && s[i]==L']') ++i;
			}
		} else if (key==L"ai_tokens") {
			if (i<s.size() && s[i]==L'[') { ++i; SkipSpaces(s,i);
				while (i<s.size() && s[i]!=L']') { SkipSpaces(s,i); if (i<s.size() && s[i]==L'{') { ++i; SkipSpaces(s,i); AiTokenPair p{}; bool done=false; while (i<s.size() && !done) { std::wstring k; if (!ReadQuoted(s,i,k)) break; SkipSpaces(s,i); if (i<s.size() && s[i]==L':') ++i; SkipSpaces(s,i); std::wstring v; ReadQuoted(s,i,v); if (k==L"agent") p.agent=v; else if (k==L"token") p.token=v; SkipSpaces(s,i); if (i<s.size() && s[i]==L',') { ++i; SkipSpaces(s,i);} else { /* maybe end */ } if (i<s.size() && s[i]==L'}') { ++i; done=true; }} g_settings.tokens.push_back(p); SkipSpaces(s,i); if (i<s.size() && s[i]==L',') { ++i; SkipSpaces(s,i);} else break; } else break; }
				if (i<s.size() && s[i]==L']') ++i;
			}
		}
		SkipSpaces(s,i); if (i<s.size() && s[i]==L',') { ++i; continue; }
	}
}

// ========== 设置窗口 ==========
struct SettingsCtx {
	HWND owner{}; HWND hwnd{};
	HWND hListKb{}; HWND hBtnAddKb{}; HWND hBtnRemKb{}; HWND hBtnUpKb{}; HWND hBtnDownKb{};
	HWND hListTok{}; HWND hEdAgent{}; HWND hEdToken{}; HWND hBtnTokAdd{}; HWND hBtnTokRem{};
	int dpiX{96}; int dpiY{96};
	HFONT hFont{};
};

static void SettingsRefreshKbList(SettingsCtx* ctx) {
	send:
	SendMessageW(ctx->hListKb, LB_RESETCONTENT, 0, 0);
	for (auto& d : g_settings.kbDirs) { SendMessageW(ctx->hListKb, LB_ADDSTRING, 0, (LPARAM)d.c_str()); }
}

static std::wstring MaskToken(const std::wstring& t) { if (t.size()<=4) return L"****"; return t.substr(0,3) + L"****"; }

static void SettingsRefreshTokList(SettingsCtx* ctx) {
	SendMessageW(ctx->hListTok, LB_RESETCONTENT, 0, 0);
	for (auto& p : g_settings.tokens) {
		std::wstring line = p.agent + L"\t" + MaskToken(p.token);
		SendMessageW(ctx->hListTok, LB_ADDSTRING, 0, (LPARAM)line.c_str());
	}
}

static HFONT CreateUIFontForDPI(int dpiY, int pointSize) {
	int height = -MulDiv(pointSize, dpiY, 72);
	return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"宋体");
}

static void SettingsApplyFont(SettingsCtx* ctx) {
	if (!ctx || !IsWindow(ctx->hwnd)) return;
	if (ctx->hFont) { DeleteObject(ctx->hFont); ctx->hFont = nullptr; }
	ctx->hFont = CreateUIFontForDPI(ctx->dpiY, 14);
	auto setf = [&](HWND h){ if (h) SendMessageW(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE); };
	setf(GetDlgItem(ctx->hwnd, 7001));
	setf(ctx->hListKb); setf(ctx->hBtnAddKb); setf(ctx->hBtnRemKb); setf(ctx->hBtnUpKb); setf(ctx->hBtnDownKb);
	setf(GetDlgItem(ctx->hwnd, 7002));
	setf(GetDlgItem(ctx->hwnd, 7003)); setf(ctx->hEdAgent);
	setf(GetDlgItem(ctx->hwnd, 7004)); setf(ctx->hEdToken);
	setf(ctx->hListTok); setf(ctx->hBtnTokAdd); setf(ctx->hBtnTokRem);
	setf(GetDlgItem(ctx->hwnd, 6199));
}

static void SettingsAddKbDir(HWND owner, SettingsCtx* ctx) {
	EnsureCOM();
	ComPtr<IFileDialog> pfd;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
	if (SUCCEEDED(hr) && pfd) {
		DWORD opt = 0; pfd->GetOptions(&opt); pfd->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		if (SUCCEEDED(pfd->Show(owner))) {
			ComPtr<IShellItem> it;
			if (SUCCEEDED(pfd->GetResult(&it)) && it) {
				PWSTR pszRaw = nullptr;
				if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &pszRaw)) && pszRaw) {
					std::unique_ptr<wchar_t, CoTaskMemDeleter> psz(pszRaw);
					std::wstring path = psz.get();
					if (!path.empty()) {
						// 去重
						auto itf = std::find_if(g_settings.kbDirs.begin(), g_settings.kbDirs.end(), [&](const std::wstring& s){ return _wcsicmp(s.c_str(), path.c_str())==0; });
						if (itf == g_settings.kbDirs.end()) { g_settings.kbDirs.push_back(path); SaveSettings(); SettingsRefreshKbList(ctx); }
					}
				}
			}
		}
	}
}

static void SettingsRemoveSelectedKb(SettingsCtx* ctx) {
	int sel = (int)SendMessageW(ctx->hListKb, LB_GETCURSEL, 0, 0);
	if (sel != LB_ERR && sel < (int)g_settings.kbDirs.size()) { g_settings.kbDirs.erase(g_settings.kbDirs.begin()+sel); SaveSettings(); SettingsRefreshKbList(ctx); if (sel >= (int)g_settings.kbDirs.size()) sel = (int)g_settings.kbDirs.size()-1; if (sel>=0) SendMessageW(ctx->hListKb, LB_SETCURSEL, sel, 0); }
}

static void SettingsMoveKb(SettingsCtx* ctx, bool up) {
	int sel = (int)SendMessageW(ctx->hListKb, LB_GETCURSEL, 0, 0);
	if (sel == LB_ERR) return;
	int to = up ? sel-1 : sel+1; if (to < 0 || to >= (int)g_settings.kbDirs.size()) return;
	std::swap(g_settings.kbDirs[sel], g_settings.kbDirs[to]); SaveSettings(); SettingsRefreshKbList(ctx); SendMessageW(ctx->hListKb, LB_SETCURSEL, to, 0);
}

static void SettingsAddOrUpdateToken(SettingsCtx* ctx) {
	wchar_t agent[128]{}; wchar_t token[2048]{};
	GetWindowTextW(ctx->hEdAgent, agent, 127); GetWindowTextW(ctx->hEdToken, token, 2047);
	std::wstring a=agent, t=token; if (a.empty()) return;
	auto it = std::find_if(g_settings.tokens.begin(), g_settings.tokens.end(), [&](const AiTokenPair& p){ return _wcsicmp(p.agent.c_str(), a.c_str())==0; });
	if (it == g_settings.tokens.end()) g_settings.tokens.push_back({a,t}); else it->token = t;
	SaveSettings(); SettingsRefreshTokList(ctx);
}

static void SettingsRemoveSelectedToken(SettingsCtx* ctx) {
	int sel = (int)SendMessageW(ctx->hListTok, LB_GETCURSEL, 0, 0);
	if (sel != LB_ERR && sel < (int)g_settings.tokens.size()) { g_settings.tokens.erase(g_settings.tokens.begin()+sel); SaveSettings(); SettingsRefreshTokList(ctx); }
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	SettingsCtx* ctx = reinterpret_cast<SettingsCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	switch (msg) {
	case WM_NCCREATE: {
		CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
		return TRUE;
	}
	case WM_CREATE: {
		ctx = reinterpret_cast<SettingsCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (!ctx) return -1;
		ctx->hwnd = hwnd;
		auto Dpi = [&](int v){ return MulDiv(v, ctx->dpiX, 96); };
		int margin = Dpi(10);
		int labelH = Dpi(18);
		int listW = Dpi(400);
		int listH1 = Dpi(150);
		int btnW = Dpi(100);
		int btnH = Dpi(26);
		int btnStep = Dpi(30);
		int rightX = margin + listW + Dpi(10);
		CreateWindowW(L"STATIC", L"知识库目录：", WS_CHILD | WS_VISIBLE, margin, margin, Dpi(200), labelH, hwnd, (HMENU)(INT_PTR)7001, nullptr, nullptr);
		ctx->hListKb = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, margin, margin+labelH+Dpi(4), listW, listH1, hwnd, (HMENU)(INT_PTR)6001, nullptr, nullptr);
		ctx->hBtnAddKb = CreateWindowW(L"BUTTON", L"添加...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX, margin+labelH+Dpi(4), btnW, btnH, hwnd, (HMENU)(INT_PTR)6002, nullptr, nullptr);
		ctx->hBtnRemKb = CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX, margin+labelH+Dpi(4)+btnStep, btnW, btnH, hwnd, (HMENU)(INT_PTR)6003, nullptr, nullptr);
		ctx->hBtnUpKb  = CreateWindowW(L"BUTTON", L"上移", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX, margin+labelH+Dpi(4)+btnStep*2, btnW, btnH, hwnd, (HMENU)(INT_PTR)6004, nullptr, nullptr);
		ctx->hBtnDownKb= CreateWindowW(L"BUTTON", L"下移", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX, margin+labelH+Dpi(4)+btnStep*3, btnW, btnH, hwnd, (HMENU)(INT_PTR)6005, nullptr, nullptr);

		int y2 = margin+labelH+Dpi(4)+listH1+margin;
		CreateWindowW(L"STATIC", L"AI Token：", WS_CHILD | WS_VISIBLE, margin, y2, Dpi(200), labelH, hwnd, (HMENU)(INT_PTR)7002, nullptr, nullptr);
		ctx->hListTok = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, margin, y2+labelH+Dpi(4), listW, Dpi(140), hwnd, (HMENU)(INT_PTR)6101, nullptr, nullptr);
		CreateWindowW(L"STATIC", L"Agent:", WS_CHILD | WS_VISIBLE, rightX, y2+labelH+Dpi(4), Dpi(60), Dpi(20), hwnd, (HMENU)(INT_PTR)7003, nullptr, nullptr);
		ctx->hEdAgent = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, rightX+Dpi(60), y2+labelH+Dpi(2), Dpi(160), Dpi(24), hwnd, (HMENU)(INT_PTR)6102, nullptr, nullptr);
		CreateWindowW(L"STATIC", L"Token:", WS_CHILD | WS_VISIBLE, rightX, y2+labelH+Dpi(4)+Dpi(28), Dpi(60), Dpi(20), hwnd, (HMENU)(INT_PTR)7004, nullptr, nullptr);
		ctx->hEdToken = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, rightX+Dpi(60), y2+labelH+Dpi(2)+Dpi(28), Dpi(160), Dpi(24), hwnd, (HMENU)(INT_PTR)6103, nullptr, nullptr);
		ctx->hBtnTokAdd = CreateWindowW(L"BUTTON", L"添加/更新", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX+Dpi(60), y2+labelH+Dpi(2)+Dpi(28)+Dpi(30), Dpi(160), btnH, hwnd, (HMENU)(INT_PTR)6104, nullptr, nullptr);
		ctx->hBtnTokRem = CreateWindowW(L"BUTTON", L"删除选中", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX+Dpi(60), y2+labelH+Dpi(2)+Dpi(28)+Dpi(30)+btnStep, Dpi(160), btnH, hwnd, (HMENU)(INT_PTR)6105, nullptr, nullptr);
		CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX+Dpi(60), y2+labelH+Dpi(2)+Dpi(28)+Dpi(30)+btnStep*2, Dpi(160), Dpi(28), hwnd, (HMENU)(INT_PTR)6199, nullptr, nullptr);

		SettingsApplyFont(ctx);
		SettingsRefreshKbList(ctx); SettingsRefreshTokList(ctx);
		return 0;
	}
	case WM_COMMAND: {
		UINT id = LOWORD(wParam);
		if (id == 6002) { SettingsAddKbDir(ctx->owner, ctx); return 0; }
		if (id == 6003) { SettingsRemoveSelectedKb(ctx); return 0; }
		if (id == 6004) { SettingsMoveKb(ctx, true); return 0; }
		if (id == 6005) { SettingsMoveKb(ctx, false); return 0; }
		if (id == 6104) { SettingsAddOrUpdateToken(ctx); return 0; }
		if (id == 6105) { SettingsRemoveSelectedToken(ctx); return 0; }
		if (id == 6199) { DestroyWindow(hwnd); return 0; }
		break;
	}
	case WM_CLOSE:
		DestroyWindow(hwnd); return 0;
	case WM_DESTROY:
		if (ctx && ctx->hFont) { DeleteObject(ctx->hFont); ctx->hFont = nullptr; }
		if (ctx) { EnableWindow(ctx->owner, TRUE); SetForegroundWindow(ctx->owner); }
		return 0;
	case WM_NCDESTROY:
		if (ctx) { SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); delete ctx; }
		return 0;
	case WM_DPICHANGED: {
		if (!ctx) break;
		UINT newDpiX = LOWORD(wParam), newDpiY = HIWORD(wParam);
		ctx->dpiX = (int)newDpiX; ctx->dpiY = (int)newDpiY;
		RECT* prc = (RECT*)lParam;
		SetWindowPos(hwnd, nullptr, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top, SWP_NOZORDER | SWP_NOACTIVATE);
		SettingsApplyFont(ctx);
		return 0;
	}
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowSettingsDialog(HWND owner) {
	static ATOM s_atom = 0; if (!s_atom) { WNDCLASSW wc{}; wc.lpfnWndProc = SettingsWndProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"PdfWinViewerSettingsWnd"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1); s_atom = RegisterClassW(&wc); }
	auto ctx = std::make_unique<SettingsCtx>(); ctx->owner = owner; ctx->dpiX = g_dpiX; ctx->dpiY = g_dpiY;
	int w = MulDiv(660, g_dpiX, 96), h = MulDiv(480, g_dpiY, 96);
	RECT prc{}; GetWindowRect(owner, &prc); int x = prc.left + 40; int y = prc.top + 40;
	DWORD style = (WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME));
	HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PdfWinViewerSettingsWnd", L"setting...", style, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), ctx.get());
	if (!dlg) { return; }
	SetWindowTextW(dlg, L"setting...");
	ShowWindow(dlg, SW_SHOWNORMAL);
	UpdateWindow(dlg);
	EnableWindow(owner, FALSE);
	MSG msg{}; while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) { if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
	// WM_NCDESTROY 会负责 delete；此处释放 unique_ptr 所有权避免双删
	ctx.release();
}

// ================= 日志模块与窗口 =================
#if !defined(PDFWV_ENABLE_LOGGING)
#define PDFWV_ENABLE_LOGGING 0
#endif

enum class LogLevel { Critical, Error, Warning, Debug, Trace };

static bool g_logHeaderShown = false; static unsigned long g_logLineIndex = 0;

static const int W_ELAPSED = 12; // includes "[+xx.xxs]"
static const int W_LVL     = 16; // label+desc
static const int W_PAGE    = 6;
static const int W_ZOOM    = 6;
static const int W_TIME    = 9;
static const int W_MEM     = 9;
static const int W_DMEM    = 9;
static const int W_REMARKS = 48; // remarks/file:line/func

static const wchar_t* LevelToLabel(LogLevel lv) {
    switch (lv) {
    case LogLevel::Critical: return L"Critical";
    case LogLevel::Error:    return L"Error";
    case LogLevel::Warning:  return L"Warning";
    case LogLevel::Debug:    return L"Debug";
    case LogLevel::Trace:    return L"Trace";
    }
    return L"Debug";
}

static const wchar_t* LevelToDesc(LogLevel lv) {
    switch (lv) {
    case LogLevel::Critical: return L"致命错误";
    case LogLevel::Error:    return L"错误";
    case LogLevel::Warning:  return L"警告";
    case LogLevel::Debug:    return L"渲染性能";
    case LogLevel::Trace:    return L"跟踪";
    }
    return L"";
}

static std::wstring NarrowToWideAcp(const char* s) {
    if (!s) return L"";
    int len = (int)strlen(s);
    if (len == 0) return L"";
    int wlen = MultiByteToWideChar(CP_ACP, 0, s, len, nullptr, 0);
    std::wstring w; w.resize((size_t)wlen);
    MultiByteToWideChar(CP_ACP, 0, s, len, w.data(), wlen);
    return w;
}

struct Log {
    static bool& Enabled() noexcept { static bool e = true; return e; }
    static bool IsEnabled() noexcept { return Enabled(); }
    static void SetEnabled(bool on) noexcept { Enabled() = on; }
    static void WriteF(LogLevel lv, const wchar_t* fmt, ...);
    static void WritePerf(int page, double zoomPct, double timeMs, double memMB, double deltaMB);
    static void WritePerfEx(int page, double zoomPct, double timeMs, double memMB, double deltaMB, const wchar_t* remarks, const char* file, int line, const char* func);
    static void Clear();
    static void Attach(HWND hRichEdit);
    static void AppendHeader();
};

#if PDFWV_ENABLE_LOGGING
  #define LOGF(lv, fmt, ...) do { if (Log::IsEnabled()) Log::WriteF((lv), L##fmt, __VA_ARGS__); } while(0)
  #define LOGM(lv, msg)      do { if (Log::IsEnabled()) Log::WriteF((lv), L"%s", L##msg); } while(0)
#else
  #define LOGF(...) do{}while(0)
  #define LOGM(...) do{}while(0)
#endif

static HWND g_hLogWnd=nullptr, g_hLogEdit=nullptr, g_hLogChk=nullptr, g_hLogAuto=nullptr; // legacy
static HWND g_hLogList=nullptr; // ListView (table mode)
static HWND g_hLogTip=nullptr;
static void AdjustLogColumns();
static LARGE_INTEGER g_qpcFreq{}; static LARGE_INTEGER g_appStartQpc{};
static void InitTimingOnce() { static bool inited=false; if (!inited) { QueryPerformanceFrequency(&g_qpcFreq); QueryPerformanceCounter(&g_appStartQpc); inited=true; } }
static LARGE_INTEGER g_openStartQpc{}; static bool g_firstRenderAfterOpen = false;

static HWND& LogRich() { static HWND h=nullptr; return h; }
void Log::Attach(HWND hRich) { LogRich() = hRich; }

static void AppendColored(HWND hEdit, LogLevel lv, const std::wstring& s, bool autoScroll=true) {
    if (!hEdit) return;
    CHARFORMAT2W cf{}; cf.cbSize=sizeof(cf); cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_BACKCOLOR;
    COLORREF zebra = (g_logLineIndex % 2) ? RGB(236,236,236) : RGB(255,255,255);
    cf.crBackColor = zebra;
    switch (lv) {
    case LogLevel::Critical: cf.crTextColor=RGB(200,0,0); cf.dwEffects=CFE_BOLD; break;
    case LogLevel::Error:    cf.crTextColor=RGB(220,0,0); cf.dwEffects=0; break;
    case LogLevel::Warning:  cf.crTextColor=RGB(200,120,0); cf.dwEffects=0; break;
    case LogLevel::Debug:    cf.crTextColor=RGB(0,90,200);  cf.dwEffects=0; break;
    case LogLevel::Trace:    cf.crTextColor=RGB(110,110,110); cf.dwEffects=0; break;
    }
    SendMessageW(hEdit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    if (autoScroll) {
        SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
        SendMessageW(hEdit, WM_VSCROLL, SB_BOTTOM, 0);
    }
    ++g_logLineIndex;
}

void Log::WriteF(LogLevel lv, const wchar_t* fmt, ...) {
    if (!IsWindow(LogRich())) return;
    InitTimingOnce();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    double elapsedMs = (now.QuadPart - g_appStartQpc.QuadPart) * 1000.0 / (double)g_qpcFreq.QuadPart;
    wchar_t line[1200]; wchar_t payload[1024];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(payload, 1023, fmt, ap);
    va_end(ap); payload[1023]=0;
    swprintf(line, 1199, L"[+%.3fs] %s", elapsedMs/1000.0, payload);
    bool autoScroll = (g_hLogAuto && BST_CHECKED==SendMessageW(g_hLogAuto,BM_GETCHECK,0,0));
    AppendColored(LogRich(), lv, line, autoScroll);
}

void Log::Clear() {
    if (!IsWindow(LogRich())) return;
    SetWindowTextW(LogRich(), L"");
    g_logLineIndex = 0; g_logHeaderShown = false;
}

void Log::AppendHeader() {
    if (g_hLogList && IsWindow(g_hLogList)) { g_logHeaderShown = true; return; }
    if (!IsWindow(LogRich()) || g_logHeaderShown) return;
    CHARFORMAT2W cf{}; cf.cbSize=sizeof(cf); cf.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD;
    cf.dwEffects = CFE_BOLD; cf.crTextColor = RGB(30,30,30); cf.crBackColor = RGB(210,210,210);
    SendMessageW(LogRich(), EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(LogRich(), EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    wchar_t hdr[512];
    swprintf(hdr, 511, L"%-*ls %-*ls %-*ls %-*ls %-*ls %-*ls %-*ls %-*ls",
             W_ELAPSED, L"Elapsed",
             W_LVL,     L"LVL:描述",
             W_PAGE,    L"page",
             W_ZOOM,    L"zoom",
             W_TIME,    L"time(ms)",
             W_MEM,     L"mem(MB)",
             W_DMEM,    L"Δmem(MB)",
             W_REMARKS, L"Remarks");
    SendMessageW(LogRich(), EM_REPLACESEL, FALSE, (LPARAM)hdr);
    SendMessageW(LogRich(), EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    g_logHeaderShown = true; g_logLineIndex = 0;
}

void Log::WritePerfEx(int page, double zoomPct, double timeMs, double memMB, double deltaMB, const wchar_t* remarks, const char* file, int line, const char* func) {
    if (g_hLogList && IsWindow(g_hLogList)) {
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now); if (!g_qpcFreq.QuadPart) InitTimingOnce();
        double elapsedMs = (now.QuadPart - g_appStartQpc.QuadPart) * 1000.0 / (double)g_qpcFreq.QuadPart;
        wchar_t elapsed[32]; swprintf(elapsed, 31, L"%7.3f s", elapsedMs/1000.0);
        wchar_t lvlcol[48]; swprintf(lvlcol, 47, L"%ls:%ls", LevelToLabel(LogLevel::Debug), LevelToDesc(LogLevel::Debug));
        wchar_t zoom[16];  swprintf(zoom, 15, L"%.0f%%", zoomPct);
        wchar_t tms[32];   swprintf(tms, 31, L"%.2f", timeMs);
        wchar_t mem[32];   swprintf(mem, 31, L"%.2f", memMB);
        wchar_t dmem[32];  swprintf(dmem, 31, L"%+.2f", deltaMB);
        std::wstring rel = MakeProjectRelativePath(NarrowToWideAcp(file));
        wchar_t src[256]; swprintf(src, 255, L"%ls:%d %ls", rel.c_str(), line, NarrowToWideAcp(func).c_str());
        wchar_t rem[512]; swprintf(rem, 511, L"%ls%ls%ls", (remarks?remarks:L""), (remarks&&remarks[0]?L" | ":L""), src);
        LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = ListView_GetItemCount(g_hLogList); it.iSubItem = 0; it.pszText = elapsed; int row = ListView_InsertItem(g_hLogList, &it);
        ListView_SetItemText(g_hLogList, row, 1, lvlcol);
        wchar_t bufPage[16]; swprintf(bufPage, 15, L"%d", page); ListView_SetItemText(g_hLogList, row, 2, bufPage);
        ListView_SetItemText(g_hLogList, row, 3, zoom);
        ListView_SetItemText(g_hLogList, row, 4, tms);
        ListView_SetItemText(g_hLogList, row, 5, mem);
        ListView_SetItemText(g_hLogList, row, 6, dmem);
        ListView_SetItemText(g_hLogList, row, 7, rem);
        if (g_hLogTip) {
            TOOLINFOW ti{}; ti.cbSize=sizeof(ti); ti.uFlags = TTF_SUBCLASS; ti.hwnd = g_hLogList; ti.uId = 2; ti.lpszText = rem; GetClientRect(g_hLogList, &ti.rect); SendMessageW(g_hLogTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
        }
        if (g_hLogAuto && BST_CHECKED==SendMessageW(g_hLogAuto,BM_GETCHECK,0,0)) ListView_EnsureVisible(g_hLogList, row, FALSE);
        return;
    }
    // fallback to RichEdit
    if (!IsWindow(LogRich())) return;
    InitTimingOnce();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    double elapsedMs = (now.QuadPart - g_appStartQpc.QuadPart) * 1000.0 / (double)g_qpcFreq.QuadPart;
    wchar_t elapsed[32]; swprintf(elapsed, 31, L"[+%7.3fs]", elapsedMs/1000.0);
    const wchar_t* lvl = LevelToLabel(LogLevel::Debug);
    const wchar_t* desc = LevelToDesc(LogLevel::Debug);
    wchar_t lvlcol2[48]; swprintf(lvlcol2, 47, L"%ls:%ls", lvl, desc);
    std::wstring filew = NarrowToWideAcp(file);
    std::wstring funcw = NarrowToWideAcp(func);
    std::wstring rel = MakeProjectRelativePath(filew);
    wchar_t src2[256]; swprintf(src2, 255, L"%ls:%d %ls", rel.c_str(), line, funcw.c_str());
    wchar_t rem2[512]; swprintf(rem2, 511, L"%ls%ls%ls", (remarks?remarks:L""), (remarks&&remarks[0]?L" | ":L""), src2);
    std::wstring remView = rem2; if ((int)remView.size() > W_REMARKS) { remView.resize(W_REMARKS-3); remView += L"..."; }
    wchar_t linebuf[768];
    swprintf(linebuf, 767, L"%-*ls %-*ls %-*d %-*.*lf%% %-*.*lf %-*.*lf %-*.*lf %-*ls",
             W_ELAPSED, elapsed,
             W_LVL,     lvlcol2,
             W_PAGE,    page,
             W_ZOOM,    0, zoomPct,
             W_TIME,    2, timeMs,
             W_MEM,     2, memMB,
             W_DMEM,    2, deltaMB,
             W_REMARKS, remView.c_str());
    bool autoScroll = (g_hLogAuto && BST_CHECKED==SendMessageW(g_hLogAuto,BM_GETCHECK,0,0));
    AppendColored(LogRich(), LogLevel::Debug, linebuf, autoScroll);
}

void Log::WritePerf(int page, double zoomPct, double timeMs, double memMB, double deltaMB) {
    WritePerfEx(page, zoomPct, timeMs, memMB, deltaMB, L"渲染单页统计", __FILE__, __LINE__, __FUNCTION__);
}

static void ShowLogWindow(HWND owner) {
#if !PDFWV_ENABLE_LOGGING
    MessageBeep(MB_ICONINFORMATION);
    return;
#else
    static HMODULE sRiched = LoadLibraryW(L"Msftedit.dll");
    if (!g_hLogWnd) {
        WNDCLASSW wc{}; wc.lpfnWndProc = [](HWND hwnd, UINT m, WPARAM w, LPARAM l)->LRESULT {
            switch (m) {
            case WM_CREATE: {
                InitTimingOnce();
                int x=8,y=8; g_hLogChk = CreateWindowW(L"BUTTON", L"Enable logging", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x,y,160,24, hwnd,(HMENU)8001,nullptr,nullptr);
                SendMessageW(g_hLogChk, BM_SETCHECK, Log::IsEnabled()?BST_CHECKED:BST_UNCHECKED, 0);
                CreateWindowW(L"BUTTON", L"Clear", WS_CHILD|WS_VISIBLE, x+170,y,80,24, hwnd,(HMENU)8002,nullptr,nullptr);
                g_hLogAuto = CreateWindowW(L"BUTTON", L"Auto-scroll", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x+260,y,120,24, hwnd,(HMENU)8003,nullptr,nullptr);
                SendMessageW(g_hLogAuto, BM_SETCHECK, BST_CHECKED, 0);
                // 新：ListView 表格
                INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&icc);
                g_hLogList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS, 8,40,780,520, hwnd, (HMENU)8101, GetModuleHandleW(nullptr), nullptr);
                ListView_SetExtendedListViewStyle(g_hLogList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);
                // 定义列
                auto addCol=[&](int idx, int width, const wchar_t* text){ LVCOLUMNW c{}; c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM; c.cx=width; c.pszText=(LPWSTR)text; c.iSubItem=idx; ListView_InsertColumn(g_hLogList, idx, &c); };
                addCol(0, 120, L"Elapsed");
                addCol(1, 140, L"LVL:描述");
                addCol(2, 60,  L"page");
                addCol(3, 70,  L"zoom");
                addCol(4, 90,  L"time(ms)");
                addCol(5, 90,  L"mem(MB)");
                addCol(6, 100, L"Δmem(MB)");
                addCol(7, 520, L"Remarks");
                AdjustLogColumns();
                // 旧 RichEdit 隐藏保留（便于将来纯文本导出）
                g_hLogEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"", WS_CHILD|ES_MULTILINE|ES_READONLY, 8, 40, 780, 520, hwnd, (HMENU)8004, nullptr, nullptr);
                ShowWindow(g_hLogEdit, SW_HIDE);
                Log::Attach(g_hLogEdit);
                Log::AppendHeader();
                return 0; }
            case WM_ERASEBKGND: {
                HDC hdc = (HDC)w;
                RECT rc; GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
                // 顶部按钮区到日志之间的整条带状背景
                RECT head{0,0,rc.right,40}; FillRect(hdc, &head, GetSysColorBrush(COLOR_BTNFACE));
                return 1; }
            case WM_SHOWWINDOW: {
                if (w) {
                    HDC hdc = GetDC(hwnd);
                    SIZE sz; GetTextExtentPoint32W(hdc, L"W", 1, &sz); // approx width per char
                    ReleaseDC(hwnd, hdc);
                    int charW = std::max(8, (int)sz.cx);
                    int minW = 20 + (W_ELAPSED + W_LVL + W_PAGE + W_ZOOM + W_TIME + W_MEM + W_DMEM + W_REMARKS + 6) * charW;
                    RECT wr; GetWindowRect(hwnd, &wr);
                    int curW = wr.right - wr.left; int curH = wr.bottom - wr.top;
                    if (curW < minW) SetWindowPos(hwnd, nullptr, wr.left, wr.top, minW, curH, SWP_NOZORDER|SWP_NOACTIVATE);
                    AdjustLogColumns();
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                return 0; }
            case WM_SIZE: {
                RECT rc; GetClientRect(hwnd,&rc);
                SetWindowPos(g_hLogList,nullptr,8,40,rc.right-16,rc.bottom-48,SWP_NOZORDER);
                AdjustLogColumns();
                InvalidateRect(g_hLogList, nullptr, TRUE);
                return 0; }
            case WM_COMMAND: {
                UINT id=LOWORD(w);
                if (id==8001) Log::SetEnabled(BST_CHECKED==SendMessageW(g_hLogChk,BM_GETCHECK,0,0));
                if (id==8002) Log::Clear();
                return 0; }
            case WM_NOTIFY: {
                LPNMHDR hdr = (LPNMHDR)l;
                if (hdr && hdr->hwndFrom == g_hLogList) {
                    if (hdr->code == NM_DBLCLK || hdr->code == LVN_ITEMACTIVATE) {
                        LPNMITEMACTIVATE pia = (LPNMITEMACTIVATE)l;
                        int row = pia->iItem;
                        int col = pia->iSubItem;
                        if (row >= 0 && (col == 7 || col == -1)) {
                            wchar_t buf[1024]; buf[0] = 0;
                            ListView_GetItemText(g_hLogList, row, 7, buf, 1023);
                            std::wstring rel; int lineNo = -1;
                            if (ParseSourceRef(buf, rel, lineNo) && lineNo > 0) {
                                std::wstring abs = MakeProjectAbsolutePath(rel);
                                if (!abs.empty() && std::filesystem::exists(abs)) {
                                    if (!TryOpenInCursor(abs, lineNo)) MessageBeep(MB_ICONWARNING);
                                } else {
                                    MessageBeep(MB_ICONWARNING);
                                }
                            } else {
                                MessageBeep(MB_ICONWARNING);
                            }
                        }
                        return 0;
                    }
                }
                break; }
            case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
            }
            return DefWindowProcW(hwnd,m,w,l);
        };
        wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"PdfWVLogWnd"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
        RegisterClassW(&wc);
        g_hLogWnd = CreateWindowW(L"PdfWVLogWnd", L"日志", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CW_USEDEFAULT, CW_USEDEFAULT, 820, 620, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    ShowWindow(g_hLogWnd, SW_SHOWNOACTIVATE);
    SetForegroundWindow(g_hLogWnd);
#endif
}

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
            pageIndex = FPDFDest_GetDestPageIndex(doc, dest);
        }

        // 使用 unique_ptr 管理，再将所有权交给 TreeView（TVITEM.lParam 为长生命周期存储）
        auto dataPtr = std::make_unique<TocItemData>();
        dataPtr->pageIndex = pageIndex;
        dataPtr->dest = dest;
        TocItemData* data = dataPtr.release();
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
	// 不再预留顶部工具栏高度
	g_contentOriginY = 0;
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

// 子类过程：书签树键盘导航转发到主窗口以控制右侧页面
static LRESULT CALLBACK TocSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    HWND mainWnd = (HWND)dwRefData;
    if (msg == WM_KEYDOWN && mainWnd && g_doc) {
        switch (wParam) {
        case VK_PRIOR: // PgUp
            SendMessageW(mainWnd, WM_COMMAND, MAKEWPARAM(ID_NAV_PREV, 0), (LPARAM)mainWnd);
            return 0;
        case VK_NEXT:  // PgDn
            SendMessageW(mainWnd, WM_COMMAND, MAKEWPARAM(ID_NAV_NEXT, 0), (LPARAM)mainWnd);
            return 0;
        case VK_HOME:
            SendMessageW(mainWnd, WM_COMMAND, MAKEWPARAM(ID_NAV_FIRST, 0), (LPARAM)mainWnd);
            return 0;
        case VK_END:
            SendMessageW(mainWnd, WM_COMMAND, MAKEWPARAM(ID_NAV_LAST, 0), (LPARAM)mainWnd);
            return 0;
        }
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, TocSubclassProc, 0);
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
	// 性能计时开始
	#if PDFWV_ENABLE_LOGGING
	LARGE_INTEGER _pf, _t0, _t1; QueryPerformanceFrequency(&_pf); QueryPerformanceCounter(&_t0);
	#endif
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
		StretchDIBits(hdc, g_contentOriginX, g_contentOriginY, cw, ch, 0, 0, cw, ch, buffer, &bmi, DIB_RGB_COLORS, SRCCOPY);
		FPDFBitmap_Destroy(bmp);
	}
	FPDF_ClosePage(page);
	#if PDFWV_ENABLE_LOGGING
	QueryPerformanceCounter(&_t1);
	double ms = (_t1.QuadPart - _t0.QuadPart) * 1000.0 / (double)_pf.QuadPart;
	PROCESS_MEMORY_COUNTERS_EX pmc{};
	static SIZE_T s_prevPriv = 0;
	if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
		double curMB = pmc.PrivateUsage / (1024.0*1024.0);
		double prevMB = s_prevPriv / (1024.0*1024.0);
		double dMB = curMB - prevMB;
		s_prevPriv = pmc.PrivateUsage;
		if (Log::IsEnabled()) {
			if (g_firstRenderAfterOpen) {
				LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
				double openMs = (now.QuadPart - g_openStartQpc.QuadPart) * 1000.0 / (double)_pf.QuadPart;
				Log::WritePerfEx(g_page_index+1, g_zoom*100.0, openMs, curMB, dMB, L"打开PDF→首次渲染", __FILE__, __LINE__, __FUNCTION__);
				g_firstRenderAfterOpen = false;
			} else {
				Log::WritePerf(g_page_index+1, g_zoom*100.0, ms, curMB, dMB);
			}
		}
	}
	#endif
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
	// 使用共享的 pdf_utils 模块进行命中检测
	PdfHitImageResult result = PdfHitImageAt(page, pageX, pageY, pageHeight);
	return result.imageObj;
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
	
	// 使用共享的 pdf_utils 模块获取位图
	FPDF_BITMAP hold = PdfAcquireBitmapForImage(g_doc, page, imgObj);
	if (!hold) {
		// 调试日志：获取位图失败
		#if PDFWV_ENABLE_LOGGING
		LOGF(LogLevel::Warning, "Failed to acquire bitmap for image object");
		#endif
		return false;
	}
	
	void* buffer = FPDFBitmap_GetBuffer(hold);
	int width = FPDFBitmap_GetWidth(hold);
	int height = FPDFBitmap_GetHeight(hold);
	int stride = FPDFBitmap_GetStride(hold);
	int fmt = FPDFBitmap_GetFormat(hold);
	
	if (!buffer || width <= 0 || height <= 0) {
		FPDFBitmap_Destroy(hold);
		#if PDFWV_ENABLE_LOGGING
		LOGF(LogLevel::Warning, "Invalid bitmap data: buffer=%p, size=%dx%d", buffer, width, height);
		#endif
		return false;
	}
	
	// 检查是否为 JPEG 格式
	bool preferJpeg = false;
	int nfilters = FPDFImageObj_GetImageFilterCount(imgObj);
	for (int k = 0; k < nfilters; ++k) {
		char name[32]{};
		if (FPDFImageObj_GetImageFilter(imgObj, k, name, sizeof(name)) > 0) {
			if (_stricmp(name, "DCTDecode") == 0) {
				preferJpeg = true;
				break;
			}
		}
	}
	
	// 如果不是 BGRA 格式，需要转换
	static std::vector<unsigned char> convBuffer;
	if (fmt != FPDFBitmap_BGRA && fmt != FPDFBitmap_BGRA_Premul) {
		convBuffer.clear();
		ConvertAnyToBGRA(buffer, width, height, stride, fmt, convBuffer);
		buffer = convBuffer.data();
		stride = width * 4;
	}
	
	#if PDFWV_ENABLE_LOGGING
	LOGF(LogLevel::Debug, "Saving image: %dx%d, format=%d, preferJpeg=%s", width, height, fmt, preferJpeg ? "true" : "false");
	#endif
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
	ClearSelection(hWnd);
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
    QueryPerformanceCounter(&g_openStartQpc); g_firstRenderAfterOpen = true;
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

// 应用启动时载入设置
struct SettingsAutoLoader { SettingsAutoLoader() { LoadSettings(); } };
static SettingsAutoLoader g_settingsLoaderOnce;

static void LayoutSidebarAndContent(HWND hWnd) {
    RECT rc{}; GetClientRect(hWnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    // 不再考虑顶部工具栏
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
		// Settings 顶级菜单项，点击直接打开设置窗口
		AppendMenuW(g_hMenu, MF_STRING, ID_SETTINGS_OPEN, L"Settings...");
		// 视图菜单（可选）：日志窗口
		g_hSettingsMenu = CreatePopupMenu();
		AppendMenuW(g_hSettingsMenu, MF_STRING, ID_VIEW_LOG, L"Log Window");
		AppendMenuW(g_hMenu, MF_POPUP, (UINT_PTR)g_hSettingsMenu, L"View");
		SetMenu(hWnd, g_hMenu);
		DrawMenuBar(hWnd);
		// 启用拖放
		DragAcceptFiles(hWnd, TRUE);
		// 左侧书签树
		g_hToc = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
			WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS,
			0, 0, 200, 100, hWnd, (HMENU)(INT_PTR)20001, GetModuleHandleW(nullptr), nullptr);
		// 拦截书签树上的翻页快捷键，转发到主窗口
		SetWindowSubclass(g_hToc, TocSubclassProc, 0, (DWORD_PTR)hWnd);
		// 状态栏与页码控件（不创建主窗口内的工具栏）
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
				
				// 使用共享的 pdf_utils 模块进行命中检测
				PdfHitImageResult hitResult = PdfHitImageAt(pg, pageX, pageY, h_pt);
				enableSave = (hitResult.imageObj != nullptr);
				
				#if PDFWV_ENABLE_LOGGING
				LOGF(LogLevel::Debug, "Right-click at client(%d,%d) -> page(%.2f,%.2f), hit=%s", 
					clientPt.x, clientPt.y, pageX, pageY, enableSave ? "image" : "none");
				#endif
				
				FPDF_ClosePage(pg);
			}
		}
		HMENU hPopup = CreatePopupMenu();
		AppendMenuW(hPopup, MF_STRING | (g_doc ? 0 : MF_GRAYED), ID_CTX_EXPORT_PNG, L"导出当前页为 PNG...");
		// 仅当点击位置命中图片时可用；不再提供"回退最大图片"
		AppendMenuW(hPopup, MF_STRING | ((g_doc && enableSave) ? 0 : MF_GRAYED), ID_CTX_SAVE_IMAGE, L"保存图片...");
		AppendMenuW(hPopup, MF_STRING | ((g_doc && g_hasSelection) ? 0 : MF_GRAYED), ID_CTX_COPY_TEXT, L"复制文本");
		AppendMenuW(hPopup, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(hPopup, MF_STRING | (g_doc ? 0 : MF_GRAYED), ID_CTX_PROPERTIES, L"属性...");
		int cmd = TrackPopupMenu(hPopup, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
		DestroyMenu(hPopup);
		if (cmd == ID_CTX_EXPORT_PNG) { ExportCurrentPageAsPNG(hWnd); }
		else if (cmd == ID_CTX_SAVE_IMAGE) {
			if (g_doc) {
				if (g_savingImageNow) { return 0; }
				g_savingImageNow = true;
				
				#if PDFWV_ENABLE_LOGGING
				LOGF(LogLevel::Debug, "Save image triggered at client(%d,%d)", clientPt.x, clientPt.y);
				#endif
				
				FPDF_PAGE pg = FPDF_LoadPage(g_doc, g_page_index);
				if (pg) {
					double pageX = (clientPt.x + g_scrollX) * (72.0 / g_dpiX) / g_zoom;
					double pageY = (clientPt.y + g_scrollY) * (72.0 / g_dpiY) / g_zoom;
					double w_pt = 0, h_pt = 0; FPDF_GetPageSizeByIndex(g_doc, g_page_index, &w_pt, &h_pt);
					
					// 使用共享的 pdf_utils 模块进行命中检测
					PdfHitImageResult hitResult = PdfHitImageAt(pg, pageX, pageY, h_pt);
					
					#if PDFWV_ENABLE_LOGGING
					LOGF(LogLevel::Debug, "Page coords: (%.2f,%.2f), hit result: obj=%p, size=%dx%d", 
						pageX, pageY, hitResult.imageObj, hitResult.pixelWidth, hitResult.pixelHeight);
					#endif
					
					if (hitResult.imageObj) {
						bool saved = SaveImageFromObject(hWnd, pg, hitResult.imageObj);
						#if PDFWV_ENABLE_LOGGING
						LOGF(LogLevel::Debug, "Save image result: %s", saved ? "success" : "failed");
						#endif
					} else {
						#if PDFWV_ENABLE_LOGGING
						LOGF(LogLevel::Warning, "No image found at click position");
						#endif
					}
					FPDF_ClosePage(pg);
				}
				g_savingImageNow = false;
			}
		}
		else if (cmd == ID_CTX_COPY_TEXT) {
			if (g_doc && g_hasSelection && !g_selectedText.empty()) {
				CopyTextToClipboard(hWnd, g_selectedText);
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
		if (id == ID_SETTINGS_OPEN) { ShowSettingsDialog(hWnd); return 0; }
		if (id == ID_VIEW_LOG) { ShowLogWindow(hWnd); return 0; }
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
		case 'C': {
			if ((GetKeyState(VK_CONTROL) & 0x8000) && g_hasSelection && !g_selectedText.empty()) {
				CopyTextToClipboard(hWnd, g_selectedText);
				return 0;
			}
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
		g_mouseDown = true; g_movedSinceDown = false; g_dragging = false; g_selecting = false;
		g_mouseDownPt.x = GET_X_LPARAM(lParam);
		g_mouseDownPt.y = GET_Y_LPARAM(lParam);
		g_lastDragPt = g_mouseDownPt;
		if (GetKeyState(VK_CONTROL) & 0x8000) {
			// Ctrl + 左键：平移拖动
			g_dragging = true;
		} else {
			// 仅左键：框选文本
			g_selecting = true;
			g_selStart = g_mouseDownPt;
			g_selEnd = g_mouseDownPt;
		}
		SetCapture(hWnd);
		return 0;
	}
	case WM_MOUSEMOVE: {
		POINT cur; cur.x = GET_X_LPARAM(lParam); cur.y = GET_Y_LPARAM(lParam);
		int dx = cur.x - g_lastDragPt.x;
		int dy = cur.y - g_lastDragPt.y;
		if (g_dragging) {
			// 必须按住 Ctrl 才允许页面平移
			if ((GetKeyState(VK_CONTROL) & 0x8000) == 0) {
				return 0;
			}
			g_lastDragPt = cur;
			// 鼠标向右移动，内容应向右跟随 => 滚动条减少
			g_scrollX = std::max(0, g_scrollX - dx);
			g_scrollY = std::max(0, g_scrollY - dy);
			ClampScroll(hWnd);
			UpdateScrollBars(hWnd);
			InvalidateRect(hWnd, nullptr, TRUE);
			if (std::abs(dx) + std::abs(dy) > 0) g_movedSinceDown = true;
			return 0;
		}
		if (g_selecting) {
			g_selEnd = cur;
			g_movedSinceDown = true;
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
					if (idx < 0 && data->dest) idx = FPDFDest_GetDestPageIndex(g_doc, data->dest);
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
							if (idx < 0 && data->dest) idx = FPDFDest_GetDestPageIndex(g_doc, data->dest);
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
		bool wasDragging = g_dragging;
		bool wasSelecting = g_selecting;
		g_dragging = false;
		g_selecting = false;
		if (GetCapture() == hWnd) {
			ReleaseCapture();
		}
		if (wasSelecting) {
			if (g_movedSinceDown) {
				// 完成一次选择并尝试提取文本
				g_hasSelection = ExtractSelectedTextOnCurrentPage(hWnd, g_selectedText);
				InvalidateRect(hWnd, nullptr, TRUE);
				return 0;
			}
			// 未移动，当作点击处理
			POINT upPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			TryNavigateLinkAtPoint(hWnd, upPt);
			return 0;
		}
		if (!g_movedSinceDown && !wasDragging) {
			// 视为点击，尝试命中链接
			POINT upPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			TryNavigateLinkAtPoint(hWnd, upPt);
			return 0;
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
		// 绘制选区高亮
		if ((g_selecting || g_hasSelection) && g_doc) {
			EnsureGdiplus();
			RECT r = GetNormalizedClientRect(g_selStart, g_selEnd);
			// 限制在内容区域内
			int cw = 0, ch = 0; GetContentClientSize(hWnd, cw, ch);
			RECT content{ g_contentOriginX, 0, g_contentOriginX + cw, ch };
			RECT drawRc{};
			if (IntersectRect(&drawRc, &r, &content)) {
				Gdiplus::Graphics g(hdc);
				Gdiplus::SolidBrush br(Gdiplus::Color(80, 30, 144, 255)); // 半透明蓝
				Gdiplus::Rect gr(drawRc.left, drawRc.top, drawRc.right - drawRc.left, drawRc.bottom - drawRc.top);
				g.FillRectangle(&br, gr);
			}
		}
		EndPaint(hWnd, &ps);
		return 0; }
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

static std::wstring MakeProjectRelativePath(const std::wstring& abs) {
    // 以可执行所在目录的父目录作为项目根简化显示
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::filesystem::path root = std::filesystem::path(exe).parent_path().parent_path();
    std::error_code ec; std::filesystem::path p(abs);
    std::filesystem::path rel = std::filesystem::relative(p, root, ec);
    if (!ec) return rel.wstring();
    return p.filename().wstring();
}

static std::wstring MakeProjectAbsolutePath(const std::wstring& rel) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::filesystem::path root = std::filesystem::path(exe).parent_path().parent_path();
    std::filesystem::path abs = root / std::filesystem::path(rel);
    std::error_code ec; auto canon = std::filesystem::weakly_canonical(abs, ec);
    return ec ? abs.wstring() : canon.wstring();
}

static bool ParseSourceRef(const std::wstring& text, std::wstring& outRel, int& outLine) {
    outRel.clear(); outLine = -1;
    size_t bar = text.rfind(L'|');
    size_t start = (bar == std::wstring::npos) ? 0 : (bar + 1);
    while (start < text.size() && iswspace(text[start])) ++start;

    size_t colon = text.find(L':', start);
    if (colon == std::wstring::npos) return false;

    std::wstring path = text.substr(start, colon - start);
    size_t p = colon + 1, n = text.size();
    int line = 0, digits = 0;
    while (p < n && iswdigit(text[p])) { line = line * 10 + (text[p] - L'0'); ++p; ++digits; }
    if (digits == 0) return false;

    auto trim = [](std::wstring& s) {
        size_t a = 0, b = s.size();
        while (a < b && iswspace(s[a])) ++a;
        while (b > a && iswspace(s[b-1])) --b;
        s = s.substr(a, b - a);
    };
    trim(path);

    auto ends_with_ci = [](const std::wstring& s, const wchar_t* suf) {
        size_t ls = s.size(), lr = wcslen(suf);
        if (ls < lr) return false;
        return _wcsicmp(s.c_str() + (ls - lr), suf) == 0;
    };
    if (!ends_with_ci(path, L".cpp")) return false;

    outRel = path; outLine = line; return true;
}

static bool TryOpenInCursor(const std::wstring& absPath, int line) {
    std::wstring arg = L"-g \"" + absPath + L":" + std::to_wstring(line) + L"\"";
    HINSTANCE h1 = ShellExecuteW(nullptr, L"open", L"cursor", arg.c_str(), nullptr, SW_SHOWNORMAL);
    if ((UINT_PTR)h1 > 32) return true;

    HINSTANCE h2 = ShellExecuteW(nullptr, L"open", L"code", arg.c_str(), nullptr, SW_SHOWNORMAL);
    if ((UINT_PTR)h2 > 32) return true;

    std::wstring pathUri = absPath; for (auto& ch : pathUri) if (ch == L'\\') ch = L'/';
    std::wstring uri1 = L"cursor://file/" + pathUri + L":" + std::to_wstring(line);
    HINSTANCE h3 = ShellExecuteW(nullptr, L"open", uri1.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((UINT_PTR)h3 > 32) return true;

    std::wstring uri2 = L"vscode://file/" + pathUri + L":" + std::to_wstring(line);
    HINSTANCE h4 = ShellExecuteW(nullptr, L"open", uri2.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return (UINT_PTR)h4 > 32;
}

static void AdjustLogColumns() {
    if (!g_hLogList || !IsWindow(g_hLogList)) return;
    // Autosize to header, then content; keep Remarks wider
    for (int i=0;i<7;++i) ListView_SetColumnWidth(g_hLogList, i, LVSCW_AUTOSIZE_USEHEADER);
    ListView_SetColumnWidth(g_hLogList, 7, LVSCW_AUTOSIZE_USEHEADER);
    // Add extra padding for readability
    for (int i=0;i<8;++i) {
        int w = ListView_GetColumnWidth(g_hLogList, i);
        ListView_SetColumnWidth(g_hLogList, i, w + 12);
    }
}
