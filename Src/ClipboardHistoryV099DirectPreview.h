#pragma once

// Stable popup preview for direct clipboard Text/Image records.
//
// The legacy StarCapClipboardFullView starts life as an overlapped window and is
// later restyled by several compatibility layers. Windows can recreate that old
// non-client frame after a hide/show cycle, which is why the first preview looks
// correct while later previews can regain a bright outline. File preview does not
// have this problem because it is a custom-painted WS_POPUP from creation time.
//
// Use the same architecture here for Text/Image records instead of trying to
// repair the legacy full window after it has already acquired non-client state.
namespace ClipboardHistoryV099DirectPreview
{
    enum class Kind : uint8_t { Text, Image };
    enum class ImageMode : uint8_t { Fit, Actual, Custom };

    inline HWND window{ nullptr };
    inline HWND textEdit{ nullptr };
    inline WNDPROC textEditBase{ nullptr };
    inline WNDPROC listBase{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };
    inline HBRUSH editBrush{ nullptr };

    inline uint64_t currentHash{ 0 };
    inline Kind kind{ Kind::Text };
    inline ImageMode imageMode{ ImageMode::Fit };
    inline double imageScale{ 1.0 };
    inline int panX{ 0 };
    inline int panY{ 0 };
    inline bool imageDragging{ false };
    inline POINT imageLast{};
    inline bool copiedFeedback{ false };

    static constexpr UINT_PTR INSTALL_TIMER = 0xC2A9;
    static constexpr UINT_PTR COPY_TIMER = 0xC2AA;

    inline COLORREF canvas()  { return ClipboardHistory::v099Canvas(); }
    inline COLORREF surface() { return ClipboardHistory::v099Surface(); }
    inline COLORREF border()  { return ClipboardHistory::v099Border(); }
    inline COLORREF text()    { return ClipboardHistory::v099Text(); }
    inline COLORREF muted()   { return ClipboardHistory::v099Muted(); }
    inline COLORREF primary() { return ClipboardHistory::v099Primary(); }
    inline COLORREF editorBg()
    {
        return ClipboardHistory::v099DarkMode() ? RGB(31, 38, 51) : RGB(254, 254, 254);
    }
    inline COLORREF floatingBorder()
    {
        return ClipboardHistory::v099DarkMode() ? RGB(68, 76, 96) : RGB(205, 210, 220);
    }

    inline ClipboardHistoryLegacy::Item* currentItem()
    {
        return ClipboardHistoryLegacy::findByHash(currentHash);
    }

    struct Buttons
    {
        RECT zoomOut{}, zoomIn{}, actual{}, fit{}, copy{}, close{};
    };

    inline Buttons buttonRects(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        Buttons b{};
        int right = std::max(360, (int)rc.right - 18);
        const int y1 = 12, y2 = 50, gap = 8;
        b.close = { right - 70, y1, right, y2 }; right = b.close.left - gap;
        b.copy = { right - 58, y1, right, y2 }; right = b.copy.left - gap;
        if (kind == Kind::Image) {
            b.fit = { right - 54, y1, right, y2 }; right = b.fit.left - gap;
            b.actual = { right - 54, y1, right, y2 }; right = b.actual.left - gap;
            b.zoomIn = { right - 40, y1, right, y2 }; right = b.zoomIn.left - gap;
            b.zoomOut = { right - 40, y1, right, y2 };
        }
        return b;
    }

    inline RECT contentRect(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        return { 18, 62, std::max(19, (int)rc.right - 18), std::max(63, (int)rc.bottom - 18) };
    }

    inline void drawButton(HDC dc, RECT rc, const wchar_t* label, bool active = false)
    {
        ClipboardHistoryV099Enhance::fillRoundRectAA(dc, rc,
            active ? ClipboardHistory::v099PrimarySoft() : surface(), 9);
        if (active)
            ClipboardHistoryV099Enhance::strokeRoundRectAA(dc, rc, primary(), 1, 9);
        ClipboardHistoryLegacy::drawText(dc, label, rc,
            active ? primary() : text(), ClipboardHistoryLegacy::smallFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void paintClientBorder(HWND hwnd, HDC dc)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) return;
        HPEN pen = CreatePen(PS_SOLID, 1, floatingBorder());
        HGDIOBJ oldPen = pen ? SelectObject(dc, pen) : nullptr;
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, 0, 0, rc.right, rc.bottom);
        SelectObject(dc, oldBrush);
        if (oldPen) SelectObject(dc, oldPen);
        if (pen) DeleteObject(pen);
    }

    inline int lineHeight(HWND edit)
    {
        HDC dc = GetDC(edit);
        if (!dc) return 18;
        HFONT font = (HFONT)SendMessageW(edit, WM_GETFONT, 0, 0);
        HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
        TEXTMETRICW tm{}; GetTextMetricsW(dc, &tm);
        if (old) SelectObject(dc, old);
        ReleaseDC(edit, dc);
        return std::max(14, (int)tm.tmHeight + (int)tm.tmExternalLeading);
    }

    inline RECT textThumb(HWND hwnd)
    {
        if (!textEdit || !IsWindowVisible(textEdit)) return {0,0,0,0};
        RECT er{}; GetClientRect(textEdit, &er);
        const int total = std::max(1, (int)SendMessageW(textEdit, EM_GETLINECOUNT, 0, 0));
        const int visible = std::max(1, (int)er.bottom / lineHeight(textEdit));
        if (total <= visible) return {0,0,0,0};
        RECT c = contentRect(hwnd);
        const int trackTop = c.top + 8, trackBottom = c.bottom - 8;
        const int trackH = std::max(1, trackBottom - trackTop);
        const int thumbH = std::clamp(trackH * visible / total, 28, trackH);
        const int first = std::max(0, (int)SendMessageW(textEdit, EM_GETFIRSTVISIBLELINE, 0, 0));
        const int maxFirst = std::max(1, total - visible);
        const int travel = std::max(0, trackH - thumbH);
        const int y = trackTop + (int)((long long)std::min(first, maxFirst) * travel / maxFirst);
        return { c.right - 7, y, c.right - 3, y + thumbH };
    }

    inline void layoutText(HWND hwnd)
    {
        if (!textEdit) return;
        RECT c = contentRect(hwnd);
        MoveWindow(textEdit, c.left + 16, c.top + 16,
            std::max(40, (int)(c.right - c.left) - 40),
            std::max(40, (int)(c.bottom - c.top) - 32), TRUE);
    }

    inline LRESULT CALLBACK textProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_MOUSEWHEEL) {
            int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            int per = lines == WHEEL_PAGESCROLL ? 8 : std::clamp((int)lines, 1, 8);
            if (steps) SendMessageW(hwnd, EM_LINESCROLL, 0, -steps * per);
            if (window) InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
            if (window) ShowWindow(window, SW_HIDE);
            ClipboardHistoryWindowShim::restoreClipboardFocus();
            return 0;
        }
        return textEditBase ? CallWindowProcW(textEditBase, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void ensureTextEdit(HWND hwnd)
    {
        if (textEdit && IsWindow(textEdit)) return;
        textEdit = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
            0,0,0,0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!textEdit) return;
        SendMessageW(textEdit, WM_SETFONT, (WPARAM)ClipboardHistoryLegacy::uiFont, TRUE);
        SendMessageW(textEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6,6));
        textEditBase = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(textEdit, GWLP_WNDPROC, (LONG_PTR)textProc));
        layoutText(hwnd);
    }

    inline bool imageInfo(const ClipboardHistoryLegacy::Item& item,
        const BITMAPINFOHEADER*& bi, size_t& off, int& sw, int& sh)
    {
        if (item.data.size() < sizeof(BITMAPINFOHEADER)) return false;
        bi = reinterpret_cast<const BITMAPINFOHEADER*>(item.data.data());
        if (!bi || bi->biWidth == 0 || bi->biHeight == 0) return false;
        off = ClipboardHistoryLegacy::dibBitsOffset(bi);
        if (off >= item.data.size()) return false;
        sw = std::abs(bi->biWidth);
        sh = std::abs(bi->biHeight);
        return sw > 0 && sh > 0;
    }

    inline double effectiveScale(const RECT& c)
    {
        auto* item = currentItem();
        if (!item) return 1.0;
        const BITMAPINFOHEADER* bi = nullptr; size_t off = 0; int sw = 0, sh = 0;
        if (!imageInfo(*item, bi, off, sw, sh)) return 1.0;
        const double aw = std::max(1, (int)(c.right - c.left) - 30);
        const double ah = std::max(1, (int)(c.bottom - c.top) - 30);
        const double fit = std::min(aw / sw, ah / sh);
        if (imageMode == ImageMode::Fit) return std::min(1.0, fit);
        if (imageMode == ImageMode::Actual) return 1.0;
        return std::clamp(imageScale, 0.10, 8.0);
    }

    inline void paintImage(HDC dc, const RECT& c)
    {
        auto* item = currentItem();
        if (!item) return;
        const BITMAPINFOHEADER* bi = nullptr; size_t off = 0; int sw = 0, sh = 0;
        if (!imageInfo(*item, bi, off, sw, sh)) return;

        const double scale = effectiveScale(c);
        const int dw = std::max(1, (int)std::lround(sw * scale));
        const int dh = std::max(1, (int)std::lround(sh * scale));
        const int cx = (c.left + c.right) / 2 + panX;
        const int cy = (c.top + c.bottom) / 2 + panY;
        const int x = cx - dw / 2;
        const int y = cy - dh / 2;

        int saved = SaveDC(dc);
        IntersectClipRect(dc, c.left, c.top, c.right, c.bottom);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, x, y, dw, dh, 0, 0, sw, sh,
            item->data.data() + off, reinterpret_cast<const BITMAPINFO*>(bi),
            DIB_RGB_COLORS, SRCCOPY);
        RestoreDC(dc, saved);
    }

    inline void copyCurrent()
    {
        if (!currentHash) return;
        if (ClipboardHistoryLegacy::restoreByHash(currentHash, false)) {
            copiedFeedback = true;
            if (window) SetTimer(window, COPY_TIMER, 900, nullptr);
        }
    }

    inline void paintWindow(HWND hwnd, HDC dc)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        ClipboardHistoryLegacy::fillRect(dc, rc, canvas());

        RECT title{ 20, 12, std::max(220, (int)rc.right - 360), 52 };
        ClipboardHistoryLegacy::drawText(dc,
            kind == Kind::Image ? L"图片预览" : L"完整内容",
            title, text(), ClipboardHistoryLegacy::boldFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        auto b = buttonRects(hwnd);
        if (kind == Kind::Image) {
            drawButton(dc, b.zoomOut, L"−");
            drawButton(dc, b.zoomIn, L"+");
            drawButton(dc, b.actual, L"1:1", imageMode == ImageMode::Actual);
            drawButton(dc, b.fit, L"适应", imageMode == ImageMode::Fit);
        }
        drawButton(dc, b.copy, copiedFeedback ? L"已复制" : L"复制", copiedFeedback);
        drawButton(dc, b.close, L"关闭");

        RECT c = contentRect(hwnd);
        ClipboardHistoryV099Enhance::fillRoundRectAA(dc, c,
            kind == Kind::Text ? (ClipboardHistory::v099DarkMode() ? RGB(43,44,48) : RGB(250,250,250)) : surface(), 10);
        ClipboardHistoryV099Enhance::strokeRoundRectAA(dc, c, border(), 1, 10);

        if (kind == Kind::Image) paintImage(dc, c);
        else {
            RECT thumb = textThumb(hwnd);
            if (thumb.right > thumb.left)
                ClipboardHistoryV099Enhance::fillRoundRectAA(dc, thumb, muted(), 4);
        }

        paintClientBorder(hwnd, dc);
    }

    inline LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_NCCALCSIZE:
        case WM_NCPAINT:
            return 0;
        case WM_CREATE:
            ensureTextEdit(hwnd);
            return 0;
        case WM_SIZE:
            layoutText(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            if ((HWND)lParam == textEdit) {
                if (editBrush) { DeleteObject(editBrush); editBrush = nullptr; }
                editBrush = CreateSolidBrush(editorBg());
                HDC dc = (HDC)wParam;
                SetTextColor(dc, text());
                SetBkColor(dc, editorBg());
                return (LRESULT)editBrush;
            }
            break;
        case WM_NCHITTEST:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            RECT rc{}; GetClientRect(hwnd, &rc);
            if (p.y >= 0 && p.y < 58 && p.x >= 0 && p.x < rc.right - 320)
                return HTCAPTION;
            return HTCLIENT;
        }
        case WM_LBUTTONDOWN:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            auto b = buttonRects(hwnd);
            if (PtInRect(&b.close, p)) {
                ShowWindow(hwnd, SW_HIDE);
                ClipboardHistoryWindowShim::restoreClipboardFocus();
                return 0;
            }
            if (PtInRect(&b.copy, p)) {
                copyCurrent(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            if (kind == Kind::Image) {
                if (PtInRect(&b.zoomOut, p)) {
                    imageScale = effectiveScale(contentRect(hwnd)) / 1.20;
                    imageMode = ImageMode::Custom;
                    InvalidateRect(hwnd, nullptr, FALSE); return 0;
                }
                if (PtInRect(&b.zoomIn, p)) {
                    imageScale = effectiveScale(contentRect(hwnd)) * 1.20;
                    imageMode = ImageMode::Custom;
                    InvalidateRect(hwnd, nullptr, FALSE); return 0;
                }
                if (PtInRect(&b.actual, p)) {
                    imageMode = ImageMode::Actual; panX = panY = 0;
                    InvalidateRect(hwnd, nullptr, FALSE); return 0;
                }
                if (PtInRect(&b.fit, p)) {
                    imageMode = ImageMode::Fit; panX = panY = 0;
                    InvalidateRect(hwnd, nullptr, FALSE); return 0;
                }
                RECT c = contentRect(hwnd);
                if (PtInRect(&c, p)) {
                    imageDragging = true; imageLast = p; SetCapture(hwnd); return 0;
                }
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (imageDragging) {
                POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                panX += p.x - imageLast.x; panY += p.y - imageLast.y; imageLast = p;
                InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (imageDragging) { imageDragging = false; ReleaseCapture(); return 0; }
            break;
        case WM_MOUSEWHEEL:
            if (kind == Kind::Image) {
                const int d = GET_WHEEL_DELTA_WPARAM(wParam);
                const double s = effectiveScale(contentRect(hwnd));
                imageScale = std::clamp(s * (d > 0 ? 1.12 : 1.0 / 1.12), 0.10, 8.0);
                imageMode = ImageMode::Custom;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == COPY_TIMER) {
                KillTimer(hwnd, COPY_TIMER);
                copiedFeedback = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
                ClipboardHistoryWindowShim::restoreClipboardFocus();
                return 0;
            }
            break;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            ClipboardHistoryWindowShim::restoreClipboardFocus();
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{}; HDC target = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            const int w = std::max(1, (int)rc.right);
            const int h = std::max(1, (int)rc.bottom);
            HDC mem = CreateCompatibleDC(target);
            HBITMAP bm = mem ? CreateCompatibleBitmap(target, w, h) : nullptr;
            HGDIOBJ old = (mem && bm) ? SelectObject(mem, bm) : nullptr;
            if (mem && bm) {
                paintWindow(hwnd, mem);
                BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
            }
            else paintWindow(hwnd, target);
            if (old) SelectObject(mem, old);
            if (bm) DeleteObject(bm);
            if (mem) DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            textEdit = nullptr;
            textEditBase = nullptr;
            window = nullptr;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void ensureWindow()
    {
        if (window && IsWindow(window)) return;
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"StarCapClipboardDirectPreviewV099";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            registered = RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }
        if (!registered) return;

        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int ww = std::min(760, std::max(620, (int)(work.right - work.left) * 46 / 100));
        const int wh = std::min(540, std::max(420, (int)(work.bottom - work.top) * 58 / 100));
        const int x = work.left + ((work.right - work.left) - ww) / 2;
        const int y = work.top + ((work.bottom - work.top) - wh) / 2;
        window = CreateWindowExW(WS_EX_TOOLWINDOW,
            L"StarCapClipboardDirectPreviewV099", L"StarCap 预览",
            WS_POPUP | WS_CLIPCHILDREN | WS_SYSMENU,
            x, y, ww, wh, ClipboardHistoryLegacy::historyWnd, nullptr,
            GetModuleHandleW(nullptr), nullptr);
    }

    inline bool showForIndex(int idx)
    {
        auto* item = ClipboardHistoryLegacy::itemAtListIndex(idx);
        if (!item || (item->type != ClipboardHistoryLegacy::ItemType::Text &&
                      item->type != ClipboardHistoryLegacy::ItemType::Image))
            return false;

        currentHash = item->hash;
        kind = item->type == ClipboardHistoryLegacy::ItemType::Image ? Kind::Image : Kind::Text;
        imageMode = ImageMode::Fit;
        imageScale = 1.0;
        panX = panY = 0;
        imageDragging = false;
        copiedFeedback = false;

        ensureWindow();
        if (!window) return false;
        ensureTextEdit(window);

        if (kind == Kind::Text) {
            const std::wstring s = ClipboardHistoryLegacy::textFromData(item->data);
            SetWindowTextW(textEdit, s.c_str());
            SendMessageW(textEdit, EM_SETSEL, 0, 0);
            ShowWindow(textEdit, SW_SHOW);
            layoutText(window);
        }
        else if (textEdit) {
            ShowWindow(textEdit, SW_HIDE);
        }

        InvalidateRect(window, nullptr, FALSE);
        ShowWindow(window, SW_SHOW);
        SetWindowPos(window, HWND_TOP, 0,0,0,0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetForegroundWindow(window);
        if (kind == Kind::Text && textEdit) SetFocus(textEdit);
        else SetFocus(window);
        return true;
    }

    inline LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_LBUTTONUP && !ClipboardHistory::v099ScrollDragging) {
            const DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            const int idx = LOWORD(hit);
            if (!HIWORD(hit) && idx >= 0 && idx < (int)ClipboardHistoryLegacy::visibleItems.size()) {
                RECT itemRc{};
                if (SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc) != LB_ERR) {
                    POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    RECT expand = ClipboardHistoryLegacy::expandRect(itemRc);
                    auto* item = ClipboardHistoryLegacy::itemAtListIndex(idx);
                    if (item && (item->type == ClipboardHistoryLegacy::ItemType::Text ||
                                 item->type == ClipboardHistoryLegacy::ItemType::Image) &&
                        ClipboardHistoryLegacy::pointIn(expand, p)) {
                        SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                        if (showForIndex(idx)) return 0;
                    }
                }
            }
        }
        return listBase ? CallWindowProcW(listBase, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void install()
    {
        HWND list = ClipboardHistoryLegacy::listWnd;
        if (!list || !IsWindow(list) || GetPropW(list, L"StarCapV099DirectPreview")) return;
        listBase = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(list, GWLP_WNDPROC, (LONG_PTR)listProc));
        if (listBase) SetPropW(list, L"StarCapV099DirectPreview", (HANDLE)1);
    }

    inline VOID CALLBACK timerProc(HWND hwnd, UINT, UINT_PTR id, DWORD)
    {
        if (hwnd && IsWindow(hwnd)) KillTimer(hwnd, id);
        install();
    }

    inline void CALLBACK onShow(HWINEVENTHOOK, DWORD event, HWND hwnd,
        LONG idObject, LONG, DWORD, DWORD)
    {
        if (event != EVENT_OBJECT_SHOW || idObject != OBJID_WINDOW || !hwnd) return;
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return;
        wchar_t cls[96]{}; GetClassNameW(hwnd, cls, (int)std::size(cls));
        if (wcscmp(cls, L"StarCapClipboardHistoryV099") == 0)
            SetTimer(hwnd, INSTALL_TIMER, 340, timerProc);
    }

    struct Lifetime
    {
        Lifetime()
        {
            showHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                onShow, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
        }
        ~Lifetime()
        {
            if (showHook) UnhookWinEvent(showHook);
            if (editBrush) DeleteObject(editBrush);
        }
    };

    inline Lifetime lifetime;
}
