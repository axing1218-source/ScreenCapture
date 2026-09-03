#pragma once

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// v0.9.9 visual/preview refinements live in this layer so the proven clipboard
// capture/storage engine can stay untouched.
namespace ClipboardHistoryV099Enhance
{
    struct GdiPlusLifetime
    {
        ULONG_PTR token{ 0 };
        GdiPlusLifetime()
        {
            Gdiplus::GdiplusStartupInput input;
            Gdiplus::GdiplusStartup(&token, &input, nullptr);
        }
        ~GdiPlusLifetime()
        {
            if (token) Gdiplus::GdiplusShutdown(token);
        }
    };

    inline GdiPlusLifetime gdiPlusLifetime;

    inline Gdiplus::Color gpColor(COLORREF c, BYTE alpha = 255)
    {
        return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
    }

    inline void addRoundedRectPath(Gdiplus::GraphicsPath& path, const RECT& rc, int roundSize)
    {
        const Gdiplus::REAL x = (Gdiplus::REAL)rc.left;
        const Gdiplus::REAL y = (Gdiplus::REAL)rc.top;
        const Gdiplus::REAL w = (Gdiplus::REAL)std::max<LONG>(0, rc.right - rc.left);
        const Gdiplus::REAL h = (Gdiplus::REAL)std::max<LONG>(0, rc.bottom - rc.top);
        if (w <= 0 || h <= 0) return;

        // Keep the old Win32 RoundRect geometry: roundSize is the corner ellipse
        // diameter, not the mathematical radius. Only the rasterizer changes.
        const Gdiplus::REAL d = std::min<Gdiplus::REAL>((Gdiplus::REAL)std::max(1, roundSize), std::min(w, h));
        if (d <= 1.0f) {
            path.AddRectangle(Gdiplus::RectF(x, y, w, h));
            return;
        }

        path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
        path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
        path.AddArc(x + w - d, y + h - d, d, d,   0.0f, 90.0f);
        path.AddArc(x,         y + h - d, d, d,  90.0f, 90.0f);
        path.CloseFigure();
    }

    inline void fillRoundRectAA(HDC dc, const RECT& rc, COLORREF color, int roundSize)
    {
        if (!dc || rc.right <= rc.left || rc.bottom <= rc.top) return;
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::GraphicsPath path;
        addRoundedRectPath(path, rc, roundSize);
        Gdiplus::SolidBrush brush(gpColor(color));
        g.FillPath(&brush, &path);
    }

    inline void strokeRoundRectAA(HDC dc, const RECT& rc, COLORREF color, int width, int roundSize)
    {
        if (!dc || rc.right <= rc.left || rc.bottom <= rc.top) return;
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::GraphicsPath path;
        addRoundedRectPath(path, rc, roundSize);
        Gdiplus::Pen pen(gpColor(color), (Gdiplus::REAL)std::max(1, width));
        pen.SetAlignment(Gdiplus::PenAlignmentInset);
        g.DrawPath(&pen, &path);
    }

    inline Gdiplus::PointF logoPoint(const RECT& rc, float x, float y)
    {
        const float w = (float)(rc.right - rc.left);
        const float h = (float)(rc.bottom - rc.top);
        return {
            (Gdiplus::REAL)(rc.left + x * w / 1093.0f),
            (Gdiplus::REAL)(rc.top + y * h / 1093.0f)
        };
    }

    inline void fillLogoPolygon(Gdiplus::Graphics& g, const RECT& rc,
        COLORREF color, const float points[][2], int count)
    {
        Gdiplus::PointF p[8]{};
        count = std::clamp(count, 0, 8);
        for (int i = 0; i < count; ++i) p[i] = logoPoint(rc, points[i][0], points[i][1]);
        Gdiplus::SolidBrush brush(gpColor(color));
        g.FillPolygon(&brush, p, count, Gdiplus::FillModeWinding);
    }

    inline void drawStarCapLogoAA(HDC dc, const RECT& rc)
    {
        if (!dc || rc.right <= rc.left || rc.bottom <= rc.top) return;
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

        static constexpr float blue[][2] = {
            {547,100},{438,418},{541,575},{653,421}
        };
        static constexpr float red[][2] = {
            {76,425},{353,632},{515,589},{408,426}
        };
        static constexpr float green[][2] = {
            {1017,425},{684,426},{577,589},{739,632}
        };
        static constexpr float orange[][2] = {
            {528,617},{359,662},{252,991},{530,786}
        };
        static constexpr float yellow[][2] = {
            {564,616},{558,784},{837,992},{732,661}
        };

        fillLogoPolygon(g, rc, RGB(0,129,253), blue, 4);
        fillLogoPolygon(g, rc, RGB(249,49,50), red, 4);
        fillLogoPolygon(g, rc, RGB(24,180,79), green, 4);
        fillLogoPolygon(g, rc, RGB(254,114,1), orange, 4);
        fillLogoPolygon(g, rc, RGB(254,189,2), yellow, 4);
    }
}

// These local overloads intentionally shadow ClipboardHistoryLegacy's GDI
// RoundRect helpers inside the v0.9.9 presentation namespace.
namespace ClipboardHistory
{
    inline void fillRoundRect(HDC dc, const RECT& rc, COLORREF color, int radius = 8)
    {
        ClipboardHistoryV099Enhance::fillRoundRectAA(dc, rc, color, radius);
    }

    inline void strokeRoundRect(HDC dc, const RECT& rc, COLORREF color, int width = 1, int radius = 8)
    {
        ClipboardHistoryV099Enhance::strokeRoundRectAA(dc, rc, color, width, radius);
    }

    inline void v099DrawStarCapLogoAA(HDC dc, RECT rc)
    {
        ClipboardHistoryV099Enhance::drawStarCapLogoAA(dc, rc);
    }
}

namespace ClipboardHistoryWindowShim
{
    enum class EnhancedImageMode : uint8_t { Fit = 0, Actual = 1, Custom = 2 };

    inline WNDPROC enhancedPreviewBaseProc{ nullptr };
    inline WNDPROC enhancedPreviewEditBaseProc{ nullptr };
    inline HWINEVENTHOOK enhancedPreviewShowHook{ nullptr };
    inline HWND enhancedPreviewWindow{ nullptr };
    inline uint64_t enhancedPreviewHash{ 0 };
    inline EnhancedImageMode enhancedImageMode{ EnhancedImageMode::Fit };
    inline double enhancedImageScale{ 1.0 };
    inline int enhancedImagePanX{ 0 };
    inline int enhancedImagePanY{ 0 };
    inline bool enhancedImageDragging{ false };
    inline POINT enhancedImageLastPoint{};
    inline bool enhancedTextScrollHover{ false };
    inline bool enhancedTextScrollDragging{ false };
    inline int enhancedTextScrollDragOffset{ 0 };

    inline LRESULT CALLBACK enhancedPreviewProc(HWND, UINT, WPARAM, LPARAM);
    inline LRESULT CALLBACK enhancedPreviewEditProc(HWND, UINT, WPARAM, LPARAM);

    inline COLORREF enhancedCanvas()  { return RGB(254, 254, 254); }
    inline COLORREF enhancedSurface() { return RGB(244, 244, 244); }
    inline COLORREF enhancedBorder()  { return RGB(229, 231, 235); }
    inline COLORREF enhancedText()    { return RGB(32, 33, 36); }
    inline COLORREF enhancedMuted()   { return RGB(112, 117, 124); }
    inline COLORREF enhancedPrimary() { return RGB(88, 112, 255); }
    inline COLORREF enhancedSoft()    { return RGB(240, 243, 255); }

    struct EnhancedPreviewButtons
    {
        RECT zoomOut{};
        RECT zoomIn{};
        RECT actual{};
        RECT fit{};
        RECT copy{};
        RECT close{};
        bool image{ false };
    };

    inline RECT enhancedContentRect(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        return { 18, 62, std::max(19, (int)rc.right - 18), std::max(63, (int)rc.bottom - 18) };
    }

    inline RECT enhancedInnerRect(HWND hwnd)
    {
        RECT r = enhancedContentRect(hwnd);
        InflateRect(&r, -16, -16);
        return r;
    }

    inline EnhancedPreviewButtons enhancedButtonRects(HWND hwnd, bool image)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        EnhancedPreviewButtons b{};
        b.image = image;
        const int y1 = 12, y2 = 50, gap = 8;
        int right = std::max(120, (int)rc.right - 18);

        b.close = { right - 70, y1, right, y2 };
        right = b.close.left - gap;
        b.copy = { right - 58, y1, right, y2 };
        right = b.copy.left - gap;

        if (image) {
            b.fit = { right - 52, y1, right, y2 };
            right = b.fit.left - gap;
            b.actual = { right - 52, y1, right, y2 };
            right = b.actual.left - gap;
            b.zoomIn = { right - 40, y1, right, y2 };
            right = b.zoomIn.left - gap;
            b.zoomOut = { right - 40, y1, right, y2 };
        }
        return b;
    }

    inline bool pointInRect(const RECT& r, POINT p)
    {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    }

    inline bool pointInAnyButton(const EnhancedPreviewButtons& b, POINT p)
    {
        if (pointInRect(b.copy, p) || pointInRect(b.close, p)) return true;
        if (!b.image) return false;
        return pointInRect(b.zoomOut, p) || pointInRect(b.zoomIn, p) ||
            pointInRect(b.actual, p) || pointInRect(b.fit, p);
    }

    inline void drawEnhancedButton(HDC dc, RECT rc, const wchar_t* label,
        bool active = false, bool primary = false)
    {
        const COLORREF bg = active ? enhancedSoft() : enhancedSurface();
        ClipboardHistoryV099Enhance::fillRoundRectAA(dc, rc, bg, 12);
        if (active)
            ClipboardHistoryV099Enhance::strokeRoundRectAA(dc, rc, RGB(210, 219, 255), 1, 12);
        ClipboardHistoryLegacy::drawText(dc, label, rc,
            primary || active ? enhancedPrimary() : enhancedText(),
            ClipboardHistoryLegacy::smallFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void resetEnhancedImageState()
    {
        enhancedImageMode = EnhancedImageMode::Fit;
        enhancedImageScale = 1.0;
        enhancedImagePanX = 0;
        enhancedImagePanY = 0;
        enhancedImageDragging = false;
    }

    inline void syncEnhancedItemState(HWND hwnd)
    {
        const uint64_t hash = ClipboardHistoryLegacy::fullItemHash;
        if (hash != enhancedPreviewHash) {
            enhancedPreviewHash = hash;
            resetEnhancedImageState();
            enhancedTextScrollDragging = false;
            enhancedTextScrollHover = false;
            syncPreviewContent(hwnd);
            if (previewEdit && IsWindow(previewEdit))
                SendMessageW(previewEdit, EM_LINESCROLL, 0, -32767);
        }
    }

    inline int enhancedEditLineHeight(HWND edit)
    {
        if (!edit) return 18;
        HDC dc = GetDC(edit);
        if (!dc) return 18;
        HFONT font = (HFONT)SendMessageW(edit, WM_GETFONT, 0, 0);
        HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        if (old) SelectObject(dc, old);
        ReleaseDC(edit, dc);
        return std::max(14, (int)tm.tmHeight + (int)tm.tmExternalLeading);
    }

    inline void layoutEnhancedPreviewEdit(HWND hwnd)
    {
        if (!previewEdit || !IsWindow(previewEdit)) return;
        auto* item = ClipboardHistoryLegacy::findByHash(ClipboardHistoryLegacy::fullItemHash);
        if (!item || item->type == ClipboardHistoryLegacy::ItemType::Image) {
            ShowWindow(previewEdit, SW_HIDE);
            return;
        }

        RECT inner = enhancedInnerRect(hwnd);
        const int reserveScrollbar = 14;
        MoveWindow(previewEdit,
            inner.left, inner.top,
            std::max(40, (int)(inner.right - inner.left) - reserveScrollbar),
            std::max(40, (int)(inner.bottom - inner.top)), TRUE);
        ShowWindow(previewEdit, SW_SHOW);
    }

    inline void enhancePreviewEditControl(HWND hwnd)
    {
        if (!previewEdit || !IsWindow(previewEdit)) return;
        if (!GetPropW(previewEdit, L"StarCapV099PreviewEditEnhanced")) {
            LONG_PTR style = GetWindowLongPtrW(previewEdit, GWL_STYLE);
            style &= ~WS_VSCROLL;
            SetWindowLongPtrW(previewEdit, GWL_STYLE, style);
            SetWindowPos(previewEdit, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            enhancedPreviewEditBaseProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(previewEdit, GWLP_WNDPROC, (LONG_PTR)enhancedPreviewEditProc));
            SetPropW(previewEdit, L"StarCapV099PreviewEditEnhanced", (HANDLE)1);
        }
        SendMessageW(previewEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
        layoutEnhancedPreviewEdit(hwnd);
    }

    inline RECT enhancedTextScrollTrack(HWND hwnd)
    {
        RECT inner = enhancedInnerRect(hwnd);
        return { inner.right - 8, inner.top + 2, inner.right - 2, inner.bottom - 2 };
    }

    inline RECT enhancedTextScrollThumb(HWND hwnd)
    {
        if (!previewEdit || !IsWindow(previewEdit) || !IsWindowVisible(previewEdit)) return { 0,0,0,0 };
        const int totalLines = std::max(1, (int)SendMessageW(previewEdit, EM_GETLINECOUNT, 0, 0));
        RECT er{}; GetClientRect(previewEdit, &er);
        const int lineH = enhancedEditLineHeight(previewEdit);
        const int visibleLines = std::max(1, (int)(er.bottom - er.top) / lineH);
        if (totalLines <= visibleLines) return { 0,0,0,0 };

        RECT track = enhancedTextScrollTrack(hwnd);
        const int trackH = std::max(1, (int)(track.bottom - track.top));
        const int thumbH = std::clamp((int)((long long)visibleLines * trackH / totalLines), 26, trackH);
        const int first = std::max(0, (int)SendMessageW(previewEdit, EM_GETFIRSTVISIBLELINE, 0, 0));
        const int maxFirst = std::max(1, totalLines - visibleLines);
        const int travel = std::max(0, trackH - thumbH);
        const int top = track.top + (int)((long long)std::min(first, maxFirst) * travel / maxFirst);
        const int width = enhancedTextScrollHover || enhancedTextScrollDragging ? 6 : 4;
        return { track.right - width, top, track.right, top + thumbH };
    }

    inline void drawEnhancedTextScrollbar(HWND hwnd, HDC dc)
    {
        RECT thumb = enhancedTextScrollThumb(hwnd);
        if (thumb.right <= thumb.left || thumb.bottom <= thumb.top) return;
        const COLORREF c = enhancedTextScrollHover || enhancedTextScrollDragging
            ? RGB(150, 154, 160) : RGB(184, 187, 191);
        ClipboardHistoryV099Enhance::fillRoundRectAA(dc, thumb, c, 6);
    }

    inline void setEnhancedTextScrollFromY(HWND hwnd, int mouseY, int dragOffset)
    {
        if (!previewEdit || !IsWindow(previewEdit)) return;
        const int totalLines = std::max(1, (int)SendMessageW(previewEdit, EM_GETLINECOUNT, 0, 0));
        RECT er{}; GetClientRect(previewEdit, &er);
        const int visibleLines = std::max(1, (int)(er.bottom - er.top) / enhancedEditLineHeight(previewEdit));
        if (totalLines <= visibleLines) return;

        RECT track = enhancedTextScrollTrack(hwnd);
        RECT thumb = enhancedTextScrollThumb(hwnd);
        const int thumbH = std::max(1, (int)(thumb.bottom - thumb.top));
        const int travel = std::max(1, (int)(track.bottom - track.top) - thumbH);
        const int y = std::clamp(mouseY - dragOffset, (int)track.top, (int)track.top + travel);
        const int maxFirst = std::max(1, totalLines - visibleLines);
        const int targetFirst = (int)((long long)(y - track.top) * maxFirst / travel);
        const int currentFirst = std::max(0, (int)SendMessageW(previewEdit, EM_GETFIRSTVISIBLELINE, 0, 0));
        SendMessageW(previewEdit, EM_LINESCROLL, 0, targetFirst - currentFirst);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    inline double enhancedFitScale(const ClipboardHistoryLegacy::Item& item, const RECT& viewport)
    {
        if (item.data.size() < sizeof(BITMAPINFOHEADER)) return 1.0;
        const auto* bi = reinterpret_cast<const BITMAPINFOHEADER*>(item.data.data());
        const int sw = std::max(1, std::abs(bi->biWidth));
        const int sh = std::max(1, std::abs(bi->biHeight));
        const int aw = std::max(1, (int)(viewport.right - viewport.left));
        const int ah = std::max(1, (int)(viewport.bottom - viewport.top));
        return std::min((double)aw / sw, (double)ah / sh);
    }

    inline double enhancedCurrentImageScale(const ClipboardHistoryLegacy::Item& item, const RECT& viewport)
    {
        if (enhancedImageMode == EnhancedImageMode::Actual) return 1.0;
        if (enhancedImageMode == EnhancedImageMode::Custom) return enhancedImageScale;
        return enhancedFitScale(item, viewport);
    }

    inline void zoomEnhancedImage(HWND hwnd, double factor)
    {
        auto* item = ClipboardHistoryLegacy::findByHash(ClipboardHistoryLegacy::fullItemHash);
        if (!item || item->type != ClipboardHistoryLegacy::ItemType::Image) return;
        RECT viewport = enhancedInnerRect(hwnd);
        const double current = enhancedCurrentImageScale(*item, viewport);
        enhancedImageMode = EnhancedImageMode::Custom;
        enhancedImageScale = std::clamp(current * factor, 0.05, 8.0);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    inline void drawEnhancedImage(HWND hwnd, HDC dc, const ClipboardHistoryLegacy::Item& item)
    {
        if (item.data.size() < sizeof(BITMAPINFOHEADER)) return;
        const auto* bi = reinterpret_cast<const BITMAPINFOHEADER*>(item.data.data());
        if (!bi->biWidth || !bi->biHeight) return;
        const size_t off = ClipboardHistoryLegacy::dibBitsOffset(bi);
        if (off >= item.data.size()) return;

        RECT viewport = enhancedInnerRect(hwnd);
        const int sw = std::max(1, std::abs(bi->biWidth));
        const int sh = std::max(1, std::abs(bi->biHeight));
        const int aw = std::max(1, (int)(viewport.right - viewport.left));
        const int ah = std::max(1, (int)(viewport.bottom - viewport.top));
        const double scale = enhancedCurrentImageScale(item, viewport);
        const int dw = std::max(1, (int)std::lround(sw * scale));
        const int dh = std::max(1, (int)std::lround(sh * scale));

        const int overflowX = std::max(0, (dw - aw) / 2);
        const int overflowY = std::max(0, (dh - ah) / 2);
        enhancedImagePanX = overflowX ? std::clamp(enhancedImagePanX, -overflowX, overflowX) : 0;
        enhancedImagePanY = overflowY ? std::clamp(enhancedImagePanY, -overflowY, overflowY) : 0;
        if (enhancedImageMode == EnhancedImageMode::Fit) {
            enhancedImagePanX = 0;
            enhancedImagePanY = 0;
        }

        const int x = viewport.left + (aw - dw) / 2 + enhancedImagePanX;
        const int y = viewport.top + (ah - dh) / 2 + enhancedImagePanY;
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, x, y, dw, dh, 0, 0, sw, sh,
            item.data.data() + off,
            reinterpret_cast<const BITMAPINFO*>(bi), DIB_RGB_COLORS, SRCCOPY);
        RestoreDC(dc, saved);
    }

    inline void paintEnhancedPreview(HWND hwnd, HDC dc)
    {
        syncEnhancedItemState(hwnd);
        auto* item = ClipboardHistoryLegacy::findByHash(ClipboardHistoryLegacy::fullItemHash);
        RECT rc{}; GetClientRect(hwnd, &rc);
        ClipboardHistoryLegacy::fillRect(dc, rc, enhancedCanvas());
        if (!item) return;

        const bool image = item->type == ClipboardHistoryLegacy::ItemType::Image;
        EnhancedPreviewButtons b = enhancedButtonRects(hwnd, image);
        const int buttonLeft = image ? b.zoomOut.left : b.copy.left;
        RECT heading{ 20, 10, std::max(120, buttonLeft - 14), 52 };
        const wchar_t* title = image ? L"图片预览" : L"完整内容";
        ClipboardHistoryLegacy::drawText(dc, title, heading, enhancedText(),
            ClipboardHistoryLegacy::boldFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (image) {
            drawEnhancedButton(dc, b.zoomOut, L"−");
            drawEnhancedButton(dc, b.zoomIn, L"+");
            drawEnhancedButton(dc, b.actual, L"1:1", enhancedImageMode == EnhancedImageMode::Actual);
            drawEnhancedButton(dc, b.fit, L"适应", enhancedImageMode == EnhancedImageMode::Fit);
        }
        drawEnhancedButton(dc, b.copy, previewCopyFeedback ? L"已复制" : L"复制", previewCopyFeedback, true);
        drawEnhancedButton(dc, b.close, L"关闭");

        RECT content = enhancedContentRect(hwnd);
        ClipboardHistoryV099Enhance::fillRoundRectAA(dc, content, enhancedSurface(), 16);
        ClipboardHistoryV099Enhance::strokeRoundRectAA(dc, content, enhancedBorder(), 1, 16);

        if (image) {
            if (previewEdit && IsWindow(previewEdit)) ShowWindow(previewEdit, SW_HIDE);
            drawEnhancedImage(hwnd, dc, *item);
        }
        else {
            enhancePreviewEditControl(hwnd);
            layoutEnhancedPreviewEdit(hwnd);
            drawEnhancedTextScrollbar(hwnd, dc);
        }
    }

    inline void enhancePreviewWindowNow(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return;
        if (GetPropW(hwnd, L"StarCapV099PreviewEnhanced")) return;

        enhancedPreviewBaseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)enhancedPreviewProc));
        if (!enhancedPreviewBaseProc) return;

        enhancedPreviewWindow = hwnd;
        SetPropW(hwnd, L"StarCapV099PreviewEnhanced", (HANDLE)1);
        applyNativeFrame(hwnd, enhancedBorder());
        syncPreviewContent(hwnd);
        enhancePreviewEditControl(hwnd);
        syncEnhancedItemState(hwnd);
        layoutEnhancedPreviewEdit(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    inline void CALLBACK enhancedPreviewWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd,
        LONG idObject, LONG, DWORD, DWORD)
    {
        if (event != EVENT_OBJECT_SHOW || idObject != OBJID_WINDOW || !hwnd) return;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return;
        wchar_t className[96]{};
        GetClassNameW(hwnd, className, (int)std::size(className));
        if (wcscmp(className, L"StarCapClipboardFullView") == 0)
            enhancePreviewWindowNow(hwnd);
    }

    struct EnhancedPreviewHookLifetime
    {
        EnhancedPreviewHookLifetime()
        {
            enhancedPreviewShowHook = SetWinEventHook(
                EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                enhancedPreviewWinEvent, GetCurrentProcessId(), 0,
                WINEVENT_OUTOFCONTEXT);
        }
        ~EnhancedPreviewHookLifetime()
        {
            if (enhancedPreviewShowHook) {
                UnhookWinEvent(enhancedPreviewShowHook);
                enhancedPreviewShowHook = nullptr;
            }
        }
    };

    inline EnhancedPreviewHookLifetime enhancedPreviewHookLifetime;

    inline LRESULT CALLBACK enhancedPreviewEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
            HWND parent = GetParent(hwnd);
            if (parent) {
                ShowWindow(parent, SW_HIDE);
                restoreClipboardFocus();
            }
            return 0;
        }

        if (msg == WM_MOUSEWHEEL) {
            int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            int perStep = lines == WHEEL_PAGESCROLL ? 8 : std::clamp((int)lines, 1, 8);
            SendMessageW(hwnd, EM_LINESCROLL, 0, -steps * perStep);
            HWND parent = GetParent(hwnd);
            if (parent) InvalidateRect(parent, nullptr, FALSE);
            return 0;
        }

        LRESULT result = enhancedPreviewEditBaseProc
            ? CallWindowProcW(enhancedPreviewEditBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        if (msg == WM_KEYDOWN || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
            msg == WM_MOUSEMOVE || msg == WM_CHAR) {
            HWND parent = GetParent(hwnd);
            if (parent) InvalidateRect(parent, nullptr, FALSE);
        }
        return result;
    }

    inline LRESULT CALLBACK enhancedPreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_NCCALCSIZE:
            return 0;

        case WM_NCACTIVATE:
            applyNativeFrame(hwnd, enhancedBorder());
            return TRUE;

        case WM_NCHITTEST:
        {
            if (!IsZoomed(hwnd)) {
                POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                RECT wr{}; GetWindowRect(hwnd, &wr);
                const UINT dpi = GetDpiForWindow(hwnd);
                const int edge = std::max(5, MulDiv(6, (int)dpi, 96));
                const bool left = p.x >= wr.left && p.x < wr.left + edge;
                const bool right = p.x < wr.right && p.x >= wr.right - edge;
                const bool top = p.y >= wr.top && p.y < wr.top + edge;
                const bool bottom = p.y < wr.bottom && p.y >= wr.bottom - edge;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }

            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            auto* item = ClipboardHistoryLegacy::findByHash(ClipboardHistoryLegacy::fullItemHash);
            const bool image = item && item->type == ClipboardHistoryLegacy::ItemType::Image;
            EnhancedPreviewButtons b = enhancedButtonRects(hwnd, image);
            if (pointInAnyButton(b, p)) return HTCLIENT;
            if (p.y < 58) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_SIZE:
            applyNativeFrame(hwnd, enhancedBorder());
            syncEnhancedItemState(hwnd);
            layoutEnhancedPreviewEdit(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        case WM_SHOWWINDOW:
            if (wParam) {
                applyNativeFrame(hwnd, enhancedBorder());
                syncPreviewContent(hwnd);
                enhancePreviewEditControl(hwnd);
                syncEnhancedItemState(hwnd);
                layoutEnhancedPreviewEdit(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;

        case WM_ACTIVATE:
        case WM_SETFOCUS:
            applyNativeFrame(hwnd, enhancedBorder());
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLOREDIT:
            if ((HWND)lParam == previewEdit) {
                if (!previewEditBrush) previewEditBrush = CreateSolidBrush(enhancedSurface());
                HDC dc = (HDC)wParam;
                SetTextColor(dc, enhancedText());
                SetBkColor(dc, enhancedSurface());
                return (LRESULT)previewEditBrush;
            }
            break;

        case WM_MOUSEWHEEL:
        {
            auto* item = ClipboardHistoryLegacy::findByHash(ClipboardHistoryLegacy::fullItemHash);
            if (item && item->type == ClipboardHistoryLegacy::ItemType::Image) {
                const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                if (steps != 0) zoomEnhancedImage(hwnd, std::pow(1.2, steps));
                return 0;
            }
            if (previewEdit && IsWindowVisible(previewEdit))
                return SendMessageW(previewEdit, msg, wParam, lParam);
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            syncEnhancedItemState(hwnd);
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            auto* item = ClipboardHistoryLegacy::findByHash(ClipboardHistoryLegacy::fullItemHash);
            const bool image = item && item->type == ClipboardHistoryLegacy::ItemType::Image;
            EnhancedPreviewButtons b = enhancedButtonRects(hwnd, image);

            if (pointInRect(b.copy, p)) {
                ClipboardHistoryLegacy::restoreByHash(ClipboardHistoryLegacy::fullItemHash, false);
                previewCopyFeedback = true;
                SetTimer(hwnd, PREVIEW_COPY_TIMER, 900, nullptr);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (pointInRect(b.close, p)) {
                ShowWindow(hwnd, SW_HIDE);
                restoreClipboardFocus();
                return 0;
            }
            if (image) {
                if (pointInRect(b.zoomOut, p)) { zoomEnhancedImage(hwnd, 0.8); return 0; }
                if (pointInRect(b.zoomIn, p)) { zoomEnhancedImage(hwnd, 1.25); return 0; }
                if (pointInRect(b.actual, p)) {
                    enhancedImageMode = EnhancedImageMode::Actual;
                    enhancedImageScale = 1.0;
                    enhancedImagePanX = enhancedImagePanY = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (pointInRect(b.fit, p)) {
                    enhancedImageMode = EnhancedImageMode::Fit;
                    enhancedImagePanX = enhancedImagePanY = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                RECT viewport = enhancedInnerRect(hwnd);
                if (pointInRect(viewport, p)) {
                    enhancedImageDragging = true;
                    enhancedImageLastPoint = p;
                    SetCapture(hwnd);
                    return 0;
                }
            }
            else {
                RECT track = enhancedTextScrollTrack(hwnd);
                if (pointInRect(track, p)) {
                    RECT thumb = enhancedTextScrollThumb(hwnd);
                    if (thumb.right > thumb.left && thumb.bottom > thumb.top) {
                        enhancedTextScrollDragging = true;
                        enhancedTextScrollHover = true;
                        if (pointInRect(thumb, p))
                            enhancedTextScrollDragOffset = p.y - thumb.top;
                        else {
                            enhancedTextScrollDragOffset = std::max(1, (thumb.bottom - thumb.top) / 2);
                            setEnhancedTextScrollFromY(hwnd, p.y, enhancedTextScrollDragOffset);
                        }
                        SetCapture(hwnd);
                    }
                    return 0;
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (enhancedImageDragging) {
                enhancedImagePanX += p.x - enhancedImageLastPoint.x;
                enhancedImagePanY += p.y - enhancedImageLastPoint.y;
                enhancedImageLastPoint = p;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (enhancedTextScrollDragging) {
                setEnhancedTextScrollFromY(hwnd, p.y, enhancedTextScrollDragOffset);
                return 0;
            }

            auto* item = ClipboardHistoryLegacy::findByHash(ClipboardHistoryLegacy::fullItemHash);
            if (item && item->type != ClipboardHistoryLegacy::ItemType::Image) {
                const bool next = pointInRect(enhancedTextScrollTrack(hwnd), p);
                if (next != enhancedTextScrollHover) {
                    enhancedTextScrollHover = next;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            if (!enhancedTextScrollDragging && enhancedTextScrollHover) {
                enhancedTextScrollHover = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONUP:
            if (enhancedImageDragging || enhancedTextScrollDragging) {
                enhancedImageDragging = false;
                enhancedTextScrollDragging = false;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;

        case WM_CAPTURECHANGED:
            enhancedImageDragging = false;
            enhancedTextScrollDragging = false;
            return 0;

        case WM_TIMER:
            if (wParam == PREVIEW_COPY_TIMER) {
                KillTimer(hwnd, PREVIEW_COPY_TIMER);
                previewCopyFeedback = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_KEYDOWN:
        {
            if (wParam == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
                restoreClipboardFocus();
                return 0;
            }
            auto* item = ClipboardHistoryLegacy::findByHash(ClipboardHistoryLegacy::fullItemHash);
            if (item && item->type == ClipboardHistoryLegacy::ItemType::Image) {
                if (wParam == VK_OEM_PLUS || wParam == VK_ADD) { zoomEnhancedImage(hwnd, 1.25); return 0; }
                if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) { zoomEnhancedImage(hwnd, 0.8); return 0; }
                if (wParam == '0') {
                    enhancedImageMode = EnhancedImageMode::Actual;
                    enhancedImageScale = 1.0;
                    enhancedImagePanX = enhancedImagePanY = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (wParam == 'F') {
                    enhancedImageMode = EnhancedImageMode::Fit;
                    enhancedImagePanX = enhancedImagePanY = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            if (wParam == VK_RETURN) {
                ClipboardHistoryLegacy::restoreByHash(ClipboardHistoryLegacy::fullItemHash, true);
                return 0;
            }
            return 0;
        }

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            restoreClipboardFocus();
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            paintEnhancedPreview(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            RemovePropW(hwnd, L"StarCapV099PreviewEnhanced");
            enhancedPreviewWindow = nullptr;
            enhancedPreviewHash = 0;
            enhancedPreviewBaseProc = nullptr;
            enhancedPreviewEditBaseProc = nullptr;
            enhancedImageDragging = false;
            enhancedTextScrollDragging = false;
            break;
        }

        return enhancedPreviewBaseProc
            ? CallWindowProcW(enhancedPreviewBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
