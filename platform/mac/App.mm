//
// macOS 原生前端（路线 B）：最小可用 PDF 渲染窗口
// - 启动后弹出选择 PDF，渲染到窗口
// - 支持 Home/End 翻页、PgUp/PgDn、Cmd +/- 缩放
//
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <fpdfview.h>
#include <fpdf_text.h>
#include <fpdf_doc.h>
#include <vector>
#include <string>

static inline void LogFPDFLastError(const char* where) {
    unsigned long code = FPDF_GetLastError();
    const char* msg = "Unknown";
    switch (code) {
        case FPDF_ERR_SUCCESS: msg = "SUCCESS"; break;
        case FPDF_ERR_UNKNOWN: msg = "UNKNOWN"; break;
        case FPDF_ERR_FILE: msg = "FILE"; break;
        case FPDF_ERR_FORMAT: msg = "FORMAT"; break;
        case FPDF_ERR_PASSWORD: msg = "PASSWORD"; break;
        case FPDF_ERR_SECURITY: msg = "SECURITY"; break;
        case FPDF_ERR_PAGE: msg = "PAGE"; break;
        default: break;
    }
    NSLog(@"[PdfWinViewer] PDFium error at %s: %lu (%@)", where, code, [NSString stringWithUTF8String:msg]);
}

static inline std::string NSStringToUTF8(NSObject* obj) {
    if (!obj) return {};
    NSString* s = (NSString*)obj;
    return std::string([s UTF8String] ?: "");
}

@interface PdfView : NSView
- (BOOL)openPDFAtPath:(NSString*)path;
- (FPDF_DOCUMENT)document;
- (void)goToPage:(int)index;
- (NSSize)currentPageSizePt; // 当前页 PDF 尺寸（pt）
- (void)updateViewSizeToFitPage; // 根据页尺寸与缩放调整自身 frame 大小（供滚动容器使用）
@end

@implementation PdfView {
    FPDF_DOCUMENT _doc;
    int _pageIndex;
    double _zoom;
    // 选择与交互
    bool _selecting;
    NSPoint _selStart;
    NSPoint _selEnd;
}
- (FPDF_DOCUMENT)document { return _doc; }
- (void)goToPage:(int)index { if (!_doc) return; int pc = FPDF_GetPageCount(_doc); if (pc>0){ if (index<0) index=0; if(index>=pc) index=pc-1; _pageIndex=index; [self setNeedsDisplay:YES]; } }

- (instancetype)initWithFrame:(NSRect)frame {
    if (self = [super initWithFrame:frame]) {
        _doc = nullptr;
        _pageIndex = 0;
        _zoom = 1.0;
        _selecting = false;
        [self.window setAcceptsMouseMovedEvents:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (BOOL)openPDFAtPath:(NSString*)path {
    NSLog(@"[PdfWinViewer] openPDFAtPath: %@", path);
    if (_doc) { FPDF_CloseDocument(_doc); _doc = nullptr; _pageIndex = 0; _zoom = 1.0; }
    std::string u8 = NSStringToUTF8(path);
    FPDF_LIBRARY_CONFIG cfg{}; cfg.version = 3; FPDF_InitLibraryWithConfig(&cfg);
    _doc = FPDF_LoadDocument(u8.c_str(), nullptr);
    if (!_doc) {
        LogFPDFLastError("FPDF_LoadDocument");
        return NO;
    }
    int pc = FPDF_GetPageCount(_doc);
    NSLog(@"[PdfWinViewer] document loaded. pageCount=%d", pc);
    [self updateViewSizeToFitPage];
    [self setNeedsDisplay:YES];
    return YES;
}

- (NSSize)currentPageSizePt {
    if (!_doc) return NSMakeSize(0,0);
    double wpt = 0, hpt = 0; FPDF_GetPageSizeByIndex(_doc, _pageIndex, &wpt, &hpt);
    return NSMakeSize((CGFloat)wpt, (CGFloat)hpt);
}

- (void)updateViewSizeToFitPage {
    NSSize s = [self currentPageSizePt];
    if (s.width <= 0 || s.height <= 0) return;
    [self setFrameSize:NSMakeSize((CGFloat)(s.width * _zoom), (CGFloat)(s.height * _zoom))];
}

- (void)keyDown:(NSEvent *)event {
    if (!_doc) return;
    NSString* chars = [event charactersIgnoringModifiers];
    unichar c = chars.length ? [chars characterAtIndex:0] : 0;
    NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    if ((mods & NSEventModifierFlagCommand) != 0) {
        // Cmd-based shortcuts
        if (c == '=') { _zoom = std::min(8.0, _zoom * 1.1); [self updateViewSizeToFitPage]; [self setNeedsDisplay:YES]; return; }
        if (c == '-') { _zoom = std::max(0.1, _zoom / 1.1); [self updateViewSizeToFitPage]; [self setNeedsDisplay:YES]; return; }
        if (c == '0') { _zoom = 1.0; [self updateViewSizeToFitPage]; [self setNeedsDisplay:YES]; return; }
        if (c == 'g' || c == 'G') {
            [self promptGotoPage];
            return;
        }
        if (c == 'e' || c == 'E') {
            [self exportCurrentPagePNG];
            return;
        }
        if (c == 'c' || c == 'C') {
            [self copySelectionToPasteboard];
            return;
        }
    }
    switch (c) {
        case NSHomeFunctionKey: _pageIndex = 0; break;
        case NSEndFunctionKey: {
            int pc = FPDF_GetPageCount(_doc); if (pc > 0) _pageIndex = pc - 1; break;
        }
        case NSPageUpFunctionKey: _pageIndex = (_pageIndex > 0) ? _pageIndex - 1 : 0; break;
        case NSPageDownFunctionKey: {
            int pc = FPDF_GetPageCount(_doc); if (pc > 0 && _pageIndex < pc - 1) _pageIndex++; break;
        }
        default: break;
    }
    [self setNeedsDisplay:YES];
}

- (BOOL)acceptsFirstResponder { return YES; }

#pragma mark - First responder actions (Menu targets)

- (IBAction)copy:(id)sender { [self copySelectionToPasteboard]; }
- (IBAction)zoomIn:(id)sender { _zoom = std::min(8.0, _zoom * 1.1); [self updateViewSizeToFitPage]; [self setNeedsDisplay:YES]; }
- (IBAction)zoomOut:(id)sender { _zoom = std::max(0.1, _zoom / 1.1); [self updateViewSizeToFitPage]; [self setNeedsDisplay:YES]; }
- (IBAction)zoomActual:(id)sender { _zoom = 1.0; [self updateViewSizeToFitPage]; [self setNeedsDisplay:YES]; }
- (IBAction)goHome:(id)sender { if (_doc){ _pageIndex = 0; [self setNeedsDisplay:YES]; } }
- (IBAction)goEnd:(id)sender { if (_doc){ int pc = FPDF_GetPageCount(_doc); if (pc>0) _pageIndex = pc-1; [self setNeedsDisplay:YES]; } }
- (IBAction)goPrevPage:(id)sender { if (_doc){ if (_pageIndex>0) _pageIndex--; [self setNeedsDisplay:YES]; } }
- (IBAction)goNextPage:(id)sender { if (_doc){ int pc = FPDF_GetPageCount(_doc); if (pc>0 && _pageIndex<pc-1) _pageIndex++; [self setNeedsDisplay:YES]; } }
- (IBAction)gotoPage:(id)sender { [self promptGotoPage]; }
- (IBAction)exportPNG:(id)sender { [self exportCurrentPagePNG]; }

- (void)magnifyWithEvent:(NSEvent *)event {
    // 触控板捏合缩放
    _zoom = std::max(0.1, std::min(8.0, _zoom * (1.0 + event.magnification)));
    [self updateViewSizeToFitPage];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    [[NSColor whiteColor] setFill]; NSRectFill(self.bounds);
    if (!_doc) return;
    int pageCount = FPDF_GetPageCount(_doc); if (pageCount <= 0) return;
    if (_pageIndex < 0) _pageIndex = 0; if (_pageIndex >= pageCount) _pageIndex = pageCount - 1;

    FPDF_PAGE page = FPDF_LoadPage(_doc, _pageIndex);
    if (!page) { LogFPDFLastError("FPDF_LoadPage"); return; }

    double wpt = 0, hpt = 0; FPDF_GetPageSizeByIndex(_doc, _pageIndex, &wpt, &hpt);
    // 使用 Retina 比例计算像素，确保 1:1 像素映射，避免缩放导致的模糊
    double scale = [[self window] backingScaleFactor] ?: 1.0;
    int pxW = std::max(1, (int)llround(wpt * _zoom * scale));
    int pxH = std::max(1, (int)llround(hpt * _zoom * scale));

    std::vector<unsigned char> buffer((size_t)pxW * pxH * 4, 255);
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(pxW, pxH, FPDFBitmap_BGRA, buffer.data(), pxW * 4);
    if (bmp) {
        FPDFBitmap_FillRect(bmp, 0, 0, pxW, pxH, 0xFFFFFFFF);
        int flags = FPDF_ANNOT | FPDF_LCD_TEXT;
        FPDF_RenderPageBitmap(bmp, page, 0, 0, pxW, pxH, 0, flags);

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, buffer.data(), (size_t)buffer.size(), NULL);
        CGBitmapInfo bi = kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaPremultipliedFirst; // BGRA
        CGImageRef img = CGImageCreate(pxW, pxH, 8, 32, pxW * 4, cs, bi, dp, NULL, false, kCGRenderingIntentDefault);
        // 以点（pt）为单位的目标绘制尺寸，并对齐到像素边界
        double destWpt = wpt * _zoom;
        double destHpt = hpt * _zoom;
        destWpt = llround(destWpt * scale) / scale;
        destHpt = llround(destHpt * scale) / scale;
        CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
        CGContextSaveGState(ctx);
        // 插值关闭，保证位图锐利
        CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
        // 视图是 flipped（y 向下），需对图片做一次上下翻转
        CGContextTranslateCTM(ctx, 0, destHpt);
        CGContextScaleCTM(ctx, 1.0, -1.0);
        // 绘制统一的白色背景（随缩放变化），避免缩放后背景与内容不同步
        CGContextSetFillColorWithColor(ctx, [NSColor whiteColor].CGColor);
        CGContextFillRect(ctx, CGRectMake(0, 0, destWpt, destHpt));
        CGContextDrawImage(ctx, CGRectMake(0, 0, destWpt, destHpt), img);
        CGContextRestoreGState(ctx);
        CGImageRelease(img); CGDataProviderRelease(dp); CGColorSpaceRelease(cs);

        FPDFBitmap_Destroy(bmp);
    }
    // 绘制选择框
    if (_selecting || !NSEqualPoints(_selStart, _selEnd)) {
        NSRect sel = NSMakeRect(std::min(_selStart.x,_selEnd.x), std::min(_selStart.y,_selEnd.y), fabs(_selStart.x-_selEnd.x), fabs(_selStart.y-_selEnd.y));
        [[NSColor colorWithCalibratedRed:0 green:0.4 blue:1 alpha:0.2] setFill];
        NSRectFillUsingOperation(sel, NSCompositingOperationSourceOver);
        [[NSColor colorWithCalibratedRed:0 green:0.4 blue:1 alpha:0.8] setStroke];
        NSFrameRectWithWidth(sel, 1.0);
    }
    FPDF_ClosePage(page);
}

#pragma mark - Mouse events for selection and link navigation

- (void)mouseDown:(NSEvent *)event {
    if (!_doc) return;
    _selecting = true;
    _selStart = [self convertPoint:event.locationInWindow fromView:nil];
    _selEnd = _selStart;
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent *)event {
    if (!_doc || !_selecting) return;
    _selEnd = [self convertPoint:event.locationInWindow fromView:nil];
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent *)event {
    if (!_doc) return;
    NSPoint up = [self convertPoint:event.locationInWindow fromView:nil];
    if (_selecting) {
        _selEnd = up; _selecting = false; [self setNeedsDisplay:YES];
    } else {
        // 非选择：尝试链接跳转
        [self tryNavigateLinkAtPoint:up];
    }
}

- (void)rightMouseDown:(NSEvent *)event {
    if (!_doc) return;
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    [menu addItemWithTitle:@"复制选中文本" action:@selector(copySelectionToPasteboard) keyEquivalent:@""];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"导出当前页 PNG" action:@selector(exportCurrentPagePNG) keyEquivalent:@""];
    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}

- (void)scrollWheel:(NSEvent *)event {
    NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    if ((mods & NSEventModifierFlagCommand) != 0) {
        double delta = event.scrollingDeltaY;
        if (event.hasPreciseScrollingDeltas) delta *= 0.1;
        if (delta > 0) _zoom = std::min(8.0, _zoom * 1.05);
        else if (delta < 0) _zoom = std::max(0.1, _zoom / 1.05);
        [self updateViewSizeToFitPage];
        [self setNeedsDisplay:YES];
    } else {
        if (self.enclosingScrollView) {
            [self.enclosingScrollView scrollWheel:event];
        } else {
            [super scrollWheel:event];
        }
    }
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    if (menuItem.action == @selector(copySelectionToPasteboard)) {
        return !NSEqualPoints(_selStart, _selEnd);
    }
    if (menuItem.action == @selector(copy:)) {
        return _doc && !NSEqualPoints(_selStart, _selEnd);
    }
    if (menuItem.action == @selector(exportPNG:)) {
        return _doc != nullptr;
    }
    return YES;
}

- (void)copySelectionToPasteboard {
    if (!_doc) return;
    NSString* text = [self extractSelectedText];
    if (text.length == 0) return;
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:text forType:NSPasteboardTypeString];
}

- (NSString*)extractSelectedText {
    if (!_doc || NSEqualPoints(_selStart, _selEnd)) return @"";
    FPDF_PAGE page = FPDF_LoadPage(_doc, _pageIndex);
    if (!page) return @"";
    double wpt = 0, hpt = 0; FPDF_GetPageSizeByIndex(_doc, _pageIndex, &wpt, &hpt);
    // 视图坐标 -> 页面像素坐标（与渲染一致）
    int dpi = 72 * (int)ceil([self.window backingScaleFactor] ?: 2.0);
    auto toPagePx = ^(NSPoint p){
        double x = p.x * (dpi/72.0) / _zoom;
        double yTopDown = p.y * (dpi/72.0) / _zoom;
        double y = std::max(0.0, hpt - yTopDown);
        return NSMakePoint(x, y);
    };
    NSPoint a = toPagePx(_selStart), b = toPagePx(_selEnd);
    double left = std::min(a.x, b.x), right = std::max(a.x, b.x);
    double bottom = std::min(a.y, b.y), top = std::max(a.y, b.y);
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    if (!tp) { FPDF_ClosePage(page); return @""; }
    int n = FPDFText_GetBoundedText(tp, left, top, right, bottom, nullptr, 0);
    if (n <= 0) { FPDFText_ClosePage(tp); FPDF_ClosePage(page); return @""; }
    std::vector<unsigned short> wbuf((size_t)n+1, 0);
    FPDFText_GetBoundedText(tp, left, top, right, bottom, (unsigned short*)wbuf.data(), n);
    FPDFText_ClosePage(tp);
    FPDF_ClosePage(page);
    NSString* s = [[NSString alloc] initWithCharacters:(unichar*)wbuf.data() length:(NSUInteger)n];
    return s ?: @"";
}

- (void)tryNavigateLinkAtPoint:(NSPoint)viewPt {
    if (!_doc) return;
    FPDF_PAGE page = FPDF_LoadPage(_doc, _pageIndex);
    if (!page) return;
    double wpt = 0, hpt = 0; FPDF_GetPageSizeByIndex(_doc, _pageIndex, &wpt, &hpt);
    int dpi = 72 * (int)ceil([self.window backingScaleFactor] ?: 2.0);
    double px = viewPt.x * (dpi/72.0) / _zoom;
    double py = std::max(0.0, hpt - viewPt.y * (dpi/72.0) / _zoom);
    FPDF_LINK link = FPDFLink_GetLinkAtPoint(page, px, py);
    if (link) {
        FPDF_DEST dest = FPDFLink_GetDest(_doc, link);
        if (!dest) {
            FPDF_ACTION act = FPDFLink_GetAction(link);
            if (act) dest = FPDFAction_GetDest(_doc, act);
        }
        if (dest) {
            int pageIndex = FPDFDest_GetDestPageIndex(_doc, dest);
            if (pageIndex >= 0) { _pageIndex = pageIndex; [self setNeedsDisplay:YES]; }
        }
    }
    FPDF_ClosePage(page);
}

- (void)promptGotoPage {
    if (!_doc) return;
    NSInteger pc = FPDF_GetPageCount(_doc);
    NSAlert* alert = [NSAlert new];
    alert.messageText = @"跳转到页";
    NSTextField* tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0,0,200,24)];
    [tf setStringValue:[NSString stringWithFormat:@"%d", _pageIndex+1]];
    alert.accessoryView = tf;
    [alert addButtonWithTitle:@"OK"]; [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] == NSAlertFirstButtonReturn) {
        NSInteger v = tf.integerValue;
        if (v >= 1 && v <= pc) { _pageIndex = (int)v - 1; [self setNeedsDisplay:YES]; }
    }
}

- (BOOL)exportCurrentPagePNG {
    if (!_doc) return NO;
    FPDF_PAGE page = FPDF_LoadPage(_doc, _pageIndex);
    if (!page) return NO;
    double wpt = 0, hpt = 0; FPDF_GetPageSizeByIndex(_doc, _pageIndex, &wpt, &hpt);
    int dpiX = 72 * (int)ceil([self.window backingScaleFactor] ?: 2.0);
    int dpiY = dpiX; double z = _zoom;
    int pxW = std::max(1, (int)llround(wpt / 72.0 * dpiX * z));
    int pxH = std::max(1, (int)llround(hpt / 72.0 * dpiY * z));
    std::vector<unsigned char> buffer((size_t)pxW * pxH * 4, 255);
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(pxW, pxH, FPDFBitmap_BGRA, buffer.data(), pxW * 4);
    if (!bmp) { FPDF_ClosePage(page); return NO; }
    FPDFBitmap_FillRect(bmp, 0, 0, pxW, pxH, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bmp, page, 0, 0, pxW, pxH, 0, FPDF_ANNOT | FPDF_LCD_TEXT);

    NSSavePanel* sp = [NSSavePanel savePanel];
    [sp setNameFieldStringValue:[NSString stringWithFormat:@"page_%d.png", _pageIndex+1]];
    if ([sp runModal] != NSModalResponseOK) { FPDFBitmap_Destroy(bmp); FPDF_ClosePage(page); return NO; }
    NSURL* url = sp.URL;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, buffer.data(), (size_t)buffer.size(), NULL);
    CGBitmapInfo bi = kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaPremultipliedFirst;
    CGImageRef img = CGImageCreate(pxW, pxH, 8, 32, pxW * 4, cs, bi, dp, NULL, false, kCGRenderingIntentDefault);
    CFStringRef pngUti = (__bridge CFStringRef)UTTypePNG.identifier;
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, pngUti, 1, NULL);
    if (dst && img) {
        CGImageDestinationAddImage(dst, img, NULL);
        CGImageDestinationFinalize(dst);
    }
    if (dst) CFRelease(dst);
    if (img) CGImageRelease(img);
    if (dp) CGDataProviderRelease(dp);
    if (cs) CGColorSpaceRelease(cs);
    FPDFBitmap_Destroy(bmp);
    FPDF_ClosePage(page);
    return YES;
}
@end

// 书签节点模型
@interface TocNode : NSObject
@property(nonatomic, strong) NSString* title;
@property(nonatomic, assign) int pageIndex; // -1 表示无跳转
@property(nonatomic, strong) NSMutableArray<TocNode*>* children;
@end

@implementation TocNode
@end

static NSString* BookmarkTitle(FPDF_DOCUMENT doc, FPDF_BOOKMARK bm) {
    int len = FPDFBookmark_GetTitle(bm, nullptr, 0);
    if (len <= 0) return @"";
    std::vector<unsigned short> w((size_t)len+1, 0);
    FPDFBookmark_GetTitle(bm, (unsigned short*)w.data(), len);
    return [[NSString alloc] initWithCharacters:(unichar*)w.data() length:(NSUInteger)len];
}

static int BookmarkPage(FPDF_DOCUMENT doc, FPDF_BOOKMARK bm) {
    FPDF_DEST dest = FPDFBookmark_GetDest(doc, bm);
    if (!dest) {
        FPDF_ACTION act = FPDFBookmark_GetAction(bm);
        if (act) dest = FPDFAction_GetDest(doc, act);
    }
    if (!dest) return -1;
    return FPDFDest_GetDestPageIndex(doc, dest);
}

static void BuildBookmarkChildren(FPDF_DOCUMENT doc, FPDF_BOOKMARK parentBm, TocNode* parentNode) {
    FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(doc, parentBm);
    while (child) {
        TocNode* node = [TocNode new];
        node.title = BookmarkTitle(doc, child);
        node.pageIndex = BookmarkPage(doc, child);
        node.children = [NSMutableArray new];
        [parentNode.children addObject:node];
        // 递归
        BuildBookmarkChildren(doc, child, node);
        child = FPDFBookmark_GetNextSibling(doc, child);
    }
}

static TocNode* BuildBookmarksTree(FPDF_DOCUMENT doc) {
    TocNode* root = [TocNode new];
    root.title = @"ROOT"; root.pageIndex = -1; root.children = [NSMutableArray new];
    BuildBookmarkChildren(doc, nullptr, root);
    return root;
}

@interface AppDelegate : NSObject <NSApplicationDelegate, NSOutlineViewDataSource, NSOutlineViewDelegate>
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) NSSplitView* split;
@property (nonatomic, strong) NSOutlineView* outline;
@property (nonatomic, strong) NSScrollView* outlineScroll;
@property (nonatomic, strong) PdfView* view;
@property (nonatomic, strong) TocNode* tocRoot;
@property (nonatomic, strong) NSMutableArray<NSString*>* recentPaths;
@property (nonatomic, strong) NSMenu* recentMenu;
@property (nonatomic, strong) NSMenuItem* recentMenuItem;
@property (nonatomic, strong) NSMutableDictionary* settingsDict; // 与 Windows 对齐的 settings.json 容器
@end

// 前置声明以避免分类方法编译告警
@interface AppDelegate ()
- (void)loadSettingsJSON;
- (void)extractRecentFromSettings;
- (void)rebuildRecentMenu;
- (void)persistRecentIntoSettings;
- (void)openPathAndAdjust:(NSString*)path;
@end

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    NSRect rect = NSMakeRect(200, 200, 1200, 800);
    self.window = [[NSWindow alloc] initWithContentRect:rect
                                              styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                                backing:NSBackingStoreBuffered defer:NO];

    // 左侧书签，右侧渲染
    self.split = [[NSSplitView alloc] initWithFrame:self.window.contentView.bounds];
    self.split.dividerStyle = NSSplitViewDividerStyleThin;
    self.split.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self.split setVertical:YES]; // 左右分栏：左侧书签，右侧内容

    NSView* left = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, rect.size.height)];
    NSView* right = [[NSView alloc] initWithFrame:NSMakeRect(260, 0, rect.size.width-260, rect.size.height)];

    // Outline
    self.outline = [[NSOutlineView alloc] initWithFrame:left.bounds];
    NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:@"toc"];
    col.title = @"书签";
    [self.outline addTableColumn:col];
    self.outline.outlineTableColumn = col;
    self.outline.delegate = self;
    self.outline.dataSource = self;
    self.outline.headerView = nil;
    self.outline.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.outline.rowSizeStyle = NSTableViewRowSizeStyleDefault;

    self.outlineScroll = [[NSScrollView alloc] initWithFrame:left.bounds];
    self.outlineScroll.documentView = self.outline;
    self.outlineScroll.hasVerticalScroller = YES;
    self.outlineScroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [left addSubview:self.outlineScroll];

    self.view = [[PdfView alloc] initWithFrame:NSMakeRect(0,0,800,600)];
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:right.bounds];
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.borderType = NSNoBorder;
    scroll.documentView = self.view;
    [right addSubview:scroll];

    [self.split addSubview:left];
    [self.split addSubview:right];
    [self.split setPosition:260 ofDividerAtIndex:0];
    self.window.contentView = self.split;
    [self.window setTitle:@"PdfWinViewer (macOS)"];
    [self.window makeKeyAndOrderFront:nil];

    // 构建主菜单（应用/文件/编辑/视图）并设置为主菜单
    NSMenu* mainMenu = [NSMenu new];
    // App 菜单
    NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"App" action:nil keyEquivalent:@""];
    NSMenu* appMenu = [NSMenu new];
    [appMenu addItemWithTitle:@"关于 PdfWinViewer" action:nil keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"退出" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [mainMenu addItem:appItem];

    // 文件菜单
    NSMenuItem* fileItem = [[NSMenuItem alloc] initWithTitle:@"文件" action:nil keyEquivalent:@""];
    NSMenu* fileMenu = [NSMenu new];
    NSMenuItem* openItem = [fileMenu addItemWithTitle:@"打开…" action:@selector(openDocument:) keyEquivalent:@"o"];
    openItem.target = self;
    NSMenuItem* exportItem = [fileMenu addItemWithTitle:@"导出当前页为 PNG" action:@selector(exportPNG:) keyEquivalent:@"e"];
    exportItem.target = self;
    [fileMenu addItem:[NSMenuItem separatorItem]];
    // 最近浏览子菜单
    self.recentMenuItem = [[NSMenuItem alloc] initWithTitle:@"最近浏览" action:nil keyEquivalent:@""];
    self.recentMenu = [NSMenu new];
    [self.recentMenuItem setSubmenu:self.recentMenu];
    [fileMenu addItem:self.recentMenuItem];
    NSLog(@"[PdfWinViewer] File menu constructed. recentMenuItem=%@ recentMenu=%@", self.recentMenuItem, self.recentMenu);
    // 调试：枚举文件菜单条目
    for (NSInteger i=0; i<fileMenu.numberOfItems; ++i) {
        NSMenuItem* mi = [fileMenu itemAtIndex:i];
        NSLog(@"[PdfWinViewer] File menu item[%ld]: title='%@' hasSubmenu=%@ action=%@", (long)i, mi.title, (mi.submenu?@"YES":@"NO"), NSStringFromSelector(mi.action));
    }
    [fileItem setSubmenu:fileMenu];
    [mainMenu addItem:fileItem];

    // 编辑菜单
    NSMenuItem* editItem = [[NSMenuItem alloc] initWithTitle:@"编辑" action:nil keyEquivalent:@""];
    NSMenu* editMenu = [NSMenu new];
    [editMenu addItemWithTitle:@"复制" action:@selector(copy:) keyEquivalent:@"c"];
    [editItem setSubmenu:editMenu];
    [mainMenu addItem:editItem];

    // 视图/导航菜单
    NSMenuItem* viewItem = [[NSMenuItem alloc] initWithTitle:@"视图" action:nil keyEquivalent:@""];
    NSMenu* viewMenu = [NSMenu new];
    NSMenuItem* zoomInItem = [viewMenu addItemWithTitle:@"放大" action:@selector(zoomIn:) keyEquivalent:@"="];
    zoomInItem.target = self.view;
    NSMenuItem* zoomOutItem = [viewMenu addItemWithTitle:@"缩小" action:@selector(zoomOut:) keyEquivalent:@"-"];
    zoomOutItem.target = self.view;
    NSMenuItem* zoomActualItem = [viewMenu addItemWithTitle:@"实际大小" action:@selector(zoomActual:) keyEquivalent:@"0"];
    zoomActualItem.target = self.view;
    [viewMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* homeItem = [viewMenu addItemWithTitle:@"第一页" action:@selector(goHome:) keyEquivalent:@""];
    homeItem.target = self.view;
    NSMenuItem* endItem = [viewMenu addItemWithTitle:@"最后一页" action:@selector(goEnd:) keyEquivalent:@""];
    endItem.target = self.view;
    NSMenuItem* prevItem = [viewMenu addItemWithTitle:@"上一页" action:@selector(goPrevPage:) keyEquivalent:@"["];
    prevItem.target = self.view;
    NSMenuItem* nextItem = [viewMenu addItemWithTitle:@"下一页" action:@selector(goNextPage:) keyEquivalent:@"]"];
    nextItem.target = self.view;
    NSMenuItem* gotoItem = [viewMenu addItemWithTitle:@"跳转页…" action:@selector(gotoPage:) keyEquivalent:@"g"];
    gotoItem.target = self.view;
    [viewItem setSubmenu:viewMenu];
    [mainMenu addItem:viewItem];

    [NSApp setMainMenu:mainMenu];
    // 移除窗口顶部“黑块”来源：不给 window.contentView 额外填充视图，直接使用 splitView；
    // 当前实现中黑块通常来自未初始化的上方填充或 titlebar 自定义区域，这里无需额外处理。

    // 启动时设定 first responder，确保菜单快捷键能落到 PdfView
    [self.window makeFirstResponder:self.view];

    // 初始化并重建“最近浏览”菜单
    NSLog(@"[PdfWinViewer] App start: will load settings.json and rebuild recent menu");
    [self loadSettingsJSON];
    NSLog(@"[PdfWinViewer] Loaded settings: keys=%@", self.settingsDict.allKeys);
    [self extractRecentFromSettings];
    NSLog(@"[PdfWinViewer] extracted recent_paths count=%lu from settings", (unsigned long)self.recentPaths.count);
    [self rebuildRecentMenu];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    FPDF_DestroyLibrary();
    return NSTerminateNow;
}
@end

#pragma mark - TOC (Outline)

@implementation AppDelegate (TOC)

- (void)rebuildToc {
    FPDF_DOCUMENT doc = [self.view document];
    if (!doc) { self.tocRoot = nil; [self.outline reloadData]; return; }
    self.tocRoot = BuildBookmarksTree(doc);
    [self.outline reloadData];
    [self.outline expandItem:nil expandChildren:YES];
}

// DataSource
- (NSInteger)outlineView:(NSOutlineView *)outlineView numberOfChildrenOfItem:(id)item {
    TocNode* n = item ?: self.tocRoot;
    return n ? (NSInteger)n.children.count : 0;
}
- (id)outlineView:(NSOutlineView *)outlineView child:(NSInteger)index ofItem:(id)item {
    TocNode* n = item ?: self.tocRoot;
    return (index >=0 && index < (NSInteger)n.children.count) ? n.children[(NSUInteger)index] : nil;
}
- (BOOL)outlineView:(NSOutlineView *)outlineView isItemExpandable:(id)item {
    TocNode* n = (TocNode*)item; return n.children.count > 0;
}
- (NSView *)outlineView:(NSOutlineView *)outlineView viewForTableColumn:(NSTableColumn *)tableColumn item:(id)item {
    NSTableCellView* cell = [outlineView makeViewWithIdentifier:@"tocCell" owner:self];
    if (!cell) {
        cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0,0,tableColumn.width,20)];
        cell.identifier = @"tocCell";
        NSTextField* text = [[NSTextField alloc] initWithFrame:cell.bounds];
        text.bezeled = NO; text.drawsBackground = NO; text.editable = NO; text.selectable = NO;
        text.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        cell.textField = text; [cell addSubview:text];
    }
    TocNode* n = (TocNode*)item;
    cell.textField.stringValue = n.title ?: @"";
    return cell;
}

// Delegate: 双击跳页
- (void)outlineView:(NSOutlineView *)outlineView didClickTableColumn:(NSTableColumn *)tableColumn {}
- (void)outlineViewSelectionDidChange:(NSNotification *)notification {
    NSInteger row = self.outline.selectedRow; if (row < 0) return; id item = [self.outline itemAtRow:row];
    TocNode* n = (TocNode*)item; if (n.pageIndex >= 0) { [self.view goToPage:n.pageIndex]; }
}

@end

#pragma mark - File menu actions

@implementation AppDelegate (FileActions)

- (IBAction)openDocument:(id)sender {
    NSLog(@"[PdfWinViewer] openDocument clicked");
    [NSApp activateIgnoringOtherApps:YES];
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    if (@available(macOS 12.0, *)) {
        panel.allowedContentTypes = @[UTTypePDF];
    } else {
        // 避免直接引用已废弃 API 引发告警，使用 KVC 设置
        [panel setValue:@[@"pdf"] forKey:@"allowedFileTypes"];
    }
    NSModalResponse resp = [panel runModal];
    NSLog(@"[PdfWinViewer] openPanel resp=%ld", (long)resp);
    if (resp == NSModalResponseOK) {
        NSString* path = panel.URL.path;
        NSLog(@"[PdfWinViewer] opening: %@", path);
        [self openPathAndAdjust:path];
    }
}

- (IBAction)exportPNG:(id)sender {
    [self.view exportCurrentPagePNG];
}

@end

#pragma mark - Recent menu

@implementation AppDelegate (Recent)

// 与 Windows 保持一致：统一使用 settings.json，包含 recent_files 数组
- (NSString*)settingsJSONPath {
    NSString* execPath = [[NSBundle mainBundle] executablePath];
    NSString* execDir = [execPath stringByDeletingLastPathComponent];
    NSString* path = [execDir stringByAppendingPathComponent:@"settings.json"];
    NSLog(@"[PdfWinViewer] settings.json path=%@", path);
    return path;
}

- (void)loadSettingsJSON {
    self.settingsDict = [NSMutableDictionary new];
    NSString* path = [self settingsJSONPath];
    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) { NSLog(@"[PdfWinViewer] settings.json not found"); return; }
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!data) { NSLog(@"[PdfWinViewer] settings.json read failed"); return; }
    NSError* err = nil;
    id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:&err];
    if (err || ![json isKindOfClass:[NSDictionary class]]) { NSLog(@"[PdfWinViewer] settings.json parse failed: %@", err); return; }
    self.settingsDict = [((NSDictionary*)json) mutableCopy];
    NSLog(@"[PdfWinViewer] settings loaded with %lu keys", (unsigned long)self.settingsDict.count);
}

- (void)saveSettingsJSON {
    if (!self.settingsDict) self.settingsDict = [NSMutableDictionary new];
    NSError* err = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:self.settingsDict options:NSJSONWritingPrettyPrinted error:&err];
    if (err || !data) { NSLog(@"[PdfWinViewer] Failed to serialize settings.json: %@", err); return; }
    NSString* path = [self settingsJSONPath];
    BOOL ok = [data writeToFile:path options:NSDataWritingAtomic error:&err];
    if (!ok || err) NSLog(@"[PdfWinViewer] Failed to write settings.json: %@", err);
    else NSLog(@"[PdfWinViewer] settings.json saved OK");
}

- (void)extractRecentFromSettings {
    self.recentPaths = [NSMutableArray new];
    id arr = self.settingsDict[@"recent_files"];
    if (![arr isKindOfClass:[NSArray class]]) { NSLog(@"[PdfWinViewer] settings has no recent_files (or wrong type)" ); return; }
    for (id item in (NSArray*)arr) {
        if ([item isKindOfClass:[NSString class]] && [((NSString*)item) length] > 0) {
            if (![self.recentPaths containsObject:item]) [self.recentPaths addObject:item];
            if (self.recentPaths.count >= 10) break;
        }
    }
    NSLog(@"[PdfWinViewer] recent_files loaded: %@", self.recentPaths);
}

- (void)persistRecentIntoSettings {
    if (!self.settingsDict) self.settingsDict = [NSMutableDictionary new];
    self.settingsDict[@"recent_files"] = [self.recentPaths copy];
    [self saveSettingsJSON];
}

- (void)openPathAndAdjust:(NSString*)path {
    if (path.length == 0) return;
    NSLog(@"[PdfWinViewer] openPathAndAdjust: %@", path);
    if ([self.view openPDFAtPath:path]) {
        [self rebuildToc];
        [self.window makeFirstResponder:self.view];
        // 根据文档页尺寸调整窗口适配（保持在屏幕可视范围内）
        NSSize s = [self.view currentPageSizePt];
        CGFloat newW = MIN(MAX(800, s.width + 300), 1600);   // 预留左栏与边距
        CGFloat newH = MIN(MAX(600, s.height + 120), 1200);
        NSRect f = self.window.frame;
        f.size = NSMakeSize(newW, newH);
        [self.window setFrame:f display:YES animate:YES];
        // 更新窗口标题
        self.window.title = [NSString stringWithFormat:@"PdfWinViewer - %@", path.lastPathComponent];
        // 写入最近
        [self addRecentPath:path];
        NSLog(@"[PdfWinViewer] after addRecentPath, recent count=%lu", (unsigned long)self.recentPaths.count);
    } else {
        NSAlert* alert = [NSAlert new];
        alert.messageText = @"无法打开 PDF";
        alert.informativeText = path ?: @"";
        [alert runModal];
    }
}

- (void)rebuildRecentMenu {
    if (!self.recentMenu) return;
    [self.recentMenu removeAllItems];
    NSUInteger count = self.recentPaths.count;
    NSLog(@"[PdfWinViewer] rebuildRecentMenu count=%lu", (unsigned long)count);
    if (count == 0) {
        NSMenuItem* none = [[NSMenuItem alloc] initWithTitle:@"无最近项目" action:nil keyEquivalent:@""];
        none.enabled = NO;
        [self.recentMenu addItem:none];
        self.recentMenuItem.enabled = NO;
        return;
    }
    self.recentMenuItem.enabled = YES;
    NSUInteger idx = 0;
    for (NSString* path in self.recentPaths) {
        NSLog(@"[PdfWinViewer] recent item %lu: %@", (unsigned long)idx, path);
        NSString* title = path.lastPathComponent.length ? path.lastPathComponent : path;
        // 带序号
        NSString* label = [NSString stringWithFormat:@"%lu. %@", (unsigned long)(idx+1), title];
        NSMenuItem* it = [self.recentMenu addItemWithTitle:label action:@selector(openRecent:) keyEquivalent:@""];
        it.target = self;
        it.representedObject = path;
        idx++;
    }
    [self.recentMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* clear = [self.recentMenu addItemWithTitle:@"清空最近浏览" action:@selector(clearRecent:) keyEquivalent:@""];
    clear.target = self;
}

- (void)addRecentPath:(NSString*)path {
    if (path.length == 0) return;
    if (!self.recentPaths) self.recentPaths = [NSMutableArray new];
    // 去重并置顶
    [self.recentPaths removeObject:path];
    [self.recentPaths insertObject:path atIndex:0];
    // 限制为最多 10 条
    while (self.recentPaths.count > 10) {
        [self.recentPaths removeLastObject];
    }
    // 持久化到 settings.json
    [self persistRecentIntoSettings];
    // 重建菜单
    [self rebuildRecentMenu];
    NSLog(@"[PdfWinViewer] addRecentPath done. paths=%@", self.recentPaths);
}

- (IBAction)openRecent:(id)sender {
    if (![sender isKindOfClass:[NSMenuItem class]]) return;
    NSString* path = ((NSMenuItem*)sender).representedObject;
    NSLog(@"[PdfWinViewer] openRecent: %@", path);
    if (path.length == 0) return;
    BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:path];
    if (!exists) {
        NSAlert* alert = [NSAlert new];
        alert.messageText = @"文件不存在";
        alert.informativeText = path;
        [alert runModal];
        // 从列表中移除并更新
        [self.recentPaths removeObject:path];
        [self persistRecentIntoSettings];
        [self rebuildRecentMenu];
        return;
    }
    [self openPathAndAdjust:path];
}

- (IBAction)clearRecent:(id)sender {
    [self.recentPaths removeAllObjects];
    [self persistRecentIntoSettings];
    [self rebuildRecentMenu];
}

@end

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        AppDelegate* del = [AppDelegate new];
        app.delegate = del;
        [app run];
    }
    return 0;
}


