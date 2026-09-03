#pragma once

// Practical file preview for clipboard CF_HDROP records. The preview deliberately
// avoids heavyweight Office/PDF engines: images are rendered directly, text/code
// files are readable/selectable, and all other formats get useful file metadata
// plus Open / Show in folder actions.
namespace ClipboardHistoryV099FilePreview
{
    enum class PreviewKind : uint8_t { Info, Text, Image };
    enum class ImageMode : uint8_t { Fit, Actual, Custom };

    inline HWND window{ nullptr };
    inline HWND textEdit{ nullptr };
    inline WNDPROC textEditBase{ nullptr };
    inline WNDPROC listBase{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };
    inline HBRUSH editBrush{ nullptr };
    inline std::filesystem::path currentPath;
    inline std::vector<std::wstring> currentPaths;
    inline std::wstring currentText;
    inline std::unique_ptr<Gdiplus::Image> currentImage;
    inline PreviewKind kind{ PreviewKind::Info };
    inline ImageMode imageMode{ ImageMode::Fit };
    inline double imageScale{ 1.0 };
    inline int panX{ 0 };
    inline int panY{ 0 };
    inline bool imageDragging{ false };
    inline POINT imageLast{};
    inline bool copiedFeedback{ false };

    static constexpr UINT_PTR INSTALL_TIMER = 0xC19D;
    static constexpr UINT_PTR COPY_TIMER = 0xC19E;

    inline COLORREF canvas()  { return ClipboardHistory::v099Canvas(); }
    inline COLORREF surface() { return ClipboardHistory::v099Surface(); }
    inline COLORREF border()  { return ClipboardHistory::v099Border(); }
    inline COLORREF text()    { return ClipboardHistory::v099Text(); }
    inline COLORREF muted()   { return ClipboardHistory::v099Muted(); }
    inline COLORREF primary() { return ClipboardHistory::v099Primary(); }

    inline bool eqExt(const std::filesystem::path& p, const wchar_t* value)
    {
        return _wcsicmp(p.extension().c_str(), value) == 0;
    }

    inline bool isImageFile(const std::filesystem::path& p)
    {
        return eqExt(p,L".png") || eqExt(p,L".jpg") || eqExt(p,L".jpeg") ||
            eqExt(p,L".bmp") || eqExt(p,L".gif") || eqExt(p,L".tif") ||
            eqExt(p,L".tiff") || eqExt(p,L".ico");
    }

    inline bool isTextFile(const std::filesystem::path& p)
    {
        return eqExt(p,L".txt") || eqExt(p,L".log") || eqExt(p,L".md") ||
            eqExt(p,L".json") || eqExt(p,L".xml") || eqExt(p,L".yaml") ||
            eqExt(p,L".yml") || eqExt(p,L".ini") || eqExt(p,L".cfg") ||
            eqExt(p,L".conf") || eqExt(p,L".cpp") || eqExt(p,L".c") ||
            eqExt(p,L".h") || eqExt(p,L".hpp") || eqExt(p,L".js") ||
            eqExt(p,L".ts") || eqExt(p,L".py") || eqExt(p,L".html") ||
            eqExt(p,L".css") || eqExt(p,L".csv") || eqExt(p,L".bat") ||
            eqExt(p,L".cmd") || eqExt(p,L".ps1");
    }

    inline std::wstring fileTypeLabel(const std::filesystem::path& p)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) return L"文件夹";
        if (isImageFile(p)) return L"图片文件";
        if (isTextFile(p)) return L"文本 / 代码文件";
        if (eqExt(p,L".pdf")) return L"PDF 文档";
        if (eqExt(p,L".doc") || eqExt(p,L".docx")) return L"Word 文档";
        if (eqExt(p,L".xls") || eqExt(p,L".xlsx")) return L"Excel 工作簿";
        if (eqExt(p,L".ppt") || eqExt(p,L".pptx")) return L"PowerPoint 演示文稿";
        if (eqExt(p,L".mp4") || eqExt(p,L".mov") || eqExt(p,L".mkv") ||
            eqExt(p,L".avi") || eqExt(p,L".webm")) return L"视频文件";
        if (eqExt(p,L".mp3") || eqExt(p,L".wav") || eqExt(p,L".flac") ||
            eqExt(p,L".m4a") || eqExt(p,L".aac")) return L"音频文件";
        if (eqExt(p,L".zip") || eqExt(p,L".rar") || eqExt(p,L".7z")) return L"压缩文件";
        if (eqExt(p,L".exe") || eqExt(p,L".msi")) return L"程序文件";
        auto ext = p.extension().wstring();
        return ext.empty() ? L"文件" : ext + L" 文件";
    }

    inline std::wstring formatSize(uintmax_t bytes)
    {
        const double value = (double)bytes;
        if (bytes >= 1024ull * 1024ull * 1024ull)
            return std::format(L"{:.2f} GB", value / (1024.0 * 1024.0 * 1024.0));
        if (bytes >= 1024ull * 1024ull)
            return std::format(L"{:.1f} MB", value / (1024.0 * 1024.0));
        if (bytes >= 1024ull)
            return std::format(L"{:.0f} KB", value / 1024.0);
        return std::format(L"{} B", bytes);
    }

    inline std::wstring modifiedTime(const std::filesystem::path& p)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &data)) return L"";
        FILETIME local{};
        SYSTEMTIME st{};
        if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) ||
            !FileTimeToSystemTime(&local, &st)) return L"";
        return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    }

    inline std::wstring readTextFile(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f) return L"无法读取此文件。";
        constexpr size_t MAX_PREVIEW = 2ull * 1024ull * 1024ull;
        std::vector<char> bytes;
        bytes.resize(MAX_PREVIEW);
        f.read(bytes.data(), (std::streamsize)bytes.size());
        bytes.resize((size_t)std::max<std::streamsize>(0, f.gcount()));
        if (bytes.empty()) return L"";

        if (bytes.size() >= 2 && (BYTE)bytes[0] == 0xFF && (BYTE)bytes[1] == 0xFE) {
            const size_t chars = (bytes.size() - 2) / sizeof(wchar_t);
            return std::wstring(reinterpret_cast<const wchar_t*>(bytes.data() + 2), chars);
        }

        size_t offset = 0;
        if (bytes.size() >= 3 && (BYTE)bytes[0] == 0xEF && (BYTE)bytes[1] == 0xBB && (BYTE)bytes[2] == 0xBF)
            offset = 3;
        const char* src = bytes.data() + offset;
        const int srcLen = (int)(bytes.size() - offset);

        UINT cp = CP_UTF8;
        int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, src, srcLen, nullptr, 0);
        if (n <= 0) {
            cp = CP_ACP;
            n = MultiByteToWideChar(cp, 0, src, srcLen, nullptr, 0);
        }
        if (n <= 0) return L"无法解码此文本文件。";
        std::wstring out((size_t)n, L'\0');
        MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
            src, srcLen, out.data(), n);
        return out;
    }

    struct Buttons
    {
        RECT zoomOut{}, zoomIn{}, actual{}, fit{};
        RECT open{}, folder{}, copyPath{}, close{};
    };

    inline Buttons buttonRects(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        Buttons b{};
        int right = std::max(440, (int)rc.right - 18);
        const int y1 = 12, y2 = 48, gap = 8;
        b.close = { right - 64, y1, right, y2 }; right = b.close.left - gap;
        b.copyPath = { right - 74, y1, right, y2 }; right = b.copyPath.left - gap;
        b.folder = { right - 64, y1, right, y2 }; right = b.folder.left - gap;
        b.open = { right - 54, y1, right, y2 }; right = b.open.left - gap;
        if (kind == PreviewKind::Image) {
            b.fit = { right - 52, y1, right, y2 }; right = b.fit.left - gap;
            b.actual = { right - 52, y1, right, y2 }; right = b.actual.left - gap;
            b.zoomIn = { right - 38, y1, right, y2 }; right = b.zoomIn.left - gap;
            b.zoomOut = { right - 38, y1, right, y2 };
        }
        return b;
    }

    inline RECT contentRect(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        return { 18, 92, std::max(19, (int)rc.right - 18), std::max(93, (int)rc.bottom - 18) };
    }

    inline void drawButton(HDC dc, RECT rc, const wchar_t* label, bool active = false)
    {
        ClipboardHistoryV099Enhance::fillRoundRectAA(dc, rc,
            active ? ClipboardHistory::v099PrimarySoft() : surface(), 10);
        if (active)
            ClipboardHistoryV099Enhance::strokeRoundRectAA(dc, rc, primary(), 1, 10);
        ClipboardHistoryLegacy::drawText(dc, label, rc,
            active ? primary() : text(), ClipboardHistoryLegacy::smallFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void copyPathToClipboard(HWND hwnd)
    {
        std::wstring value = currentPath.wstring();
        if (value.empty() || !OpenClipboard(hwnd)) return;
        EmptyClipboard();
        const SIZE_T bytes = (value.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            void* p = GlobalLock(mem);
            if (p) {
                memcpy(p, value.c_str(), bytes);
                GlobalUnlock(mem);
                if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
            }
            else GlobalFree(mem);
        }
        CloseClipboard();
        copiedFeedback = true;
        if (window) SetTimer(window, COPY_TIMER, 900, nullptr);
    }

    inline void openCurrent()
    {
        if (!currentPath.empty())
            ShellExecuteW(window, L"open", currentPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    inline void revealCurrent()
    {
        if (currentPath.empty()) return;
        std::wstring args = std::format(L"/select,\"{}\"", currentPath.wstring());
        ShellExecuteW(window, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
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
        MoveWindow(textEdit, c.left + 14, c.top + 12,
            std::max(40, (int)(c.right - c.left) - 36),
            std::max(40, (int)(c.bottom - c.top) - 24), TRUE);
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
            0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!textEdit) return;
        SendMessageW(textEdit, WM_SETFONT, (WPARAM)ClipboardHistoryLegacy::uiFont, TRUE);
        SendMessageW(textEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6,6));
        textEditBase = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(textEdit,GWLP_WNDPROC,(LONG_PTR)textProc));
        layoutText(hwnd);
    }

    inline void resetContent()
    {
        currentImage.reset(); currentText.clear(); kind=PreviewKind::Info;
        imageMode=ImageMode::Fit; imageScale=1.0; panX=panY=0; imageDragging=false; copiedFeedback=false;

        std::error_code ec;
        if (std::filesystem::is_regular_file(currentPath, ec) && isImageFile(currentPath)) {
            auto img = std::make_unique<Gdiplus::Image>(currentPath.c_str(), FALSE);
            if (img && img->GetLastStatus() == Gdiplus::Ok && img->GetWidth() > 0 && img->GetHeight() > 0) {
                currentImage = std::move(img);
                kind = PreviewKind::Image;
                return;
            }
        }
        if (std::filesystem::is_regular_file(currentPath, ec) && isTextFile(currentPath)) {
            currentText = readTextFile(currentPath);
            kind = PreviewKind::Text;
            return;
        }
        kind = PreviewKind::Info;
    }

    inline double effectiveScale(const RECT& c)
    {
        if (!currentImage) return 1.0;
        const double iw = (double)currentImage->GetWidth();
        const double ih = (double)currentImage->GetHeight();
        const double availW = std::max(1, (int)(c.right - c.left) - 30);
        const double availH = std::max(1, (int)(c.bottom - c.top) - 30);
        const double fit = std::min(availW / iw, availH / ih);
        if (imageMode == ImageMode::Fit) return std::min(1.0, fit);
        if (imageMode == ImageMode::Actual) return 1.0;
        return std::clamp(imageScale, 0.10, 8.0);
    }

    inline void paintImage(HDC dc, const RECT& c)
    {
        if (!currentImage) return;
        Gdiplus::Graphics g(dc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        const double scale = effectiveScale(c);
        const int w = std::max(1, (int)std::round(currentImage->GetWidth() * scale));
        const int h = std::max(1, (int)std::round(currentImage->GetHeight() * scale));
        const int cx = (c.left + c.right) / 2 + panX;
        const int cy = (c.top + c.bottom) / 2 + panY;
        Gdiplus::Rect dest(cx - w/2, cy - h/2, w, h);
        g.DrawImage(currentImage.get(), dest, 0, 0,
            currentImage->GetWidth(), currentImage->GetHeight(), Gdiplus::UnitPixel);
    }

    inline std::wstring infoLine()
    {
        std::error_code ec;
        uintmax_t size = std::filesystem::is_regular_file(currentPath, ec)
            ? std::filesystem::file_size(currentPath, ec) : 0;
        std::wstring result = fileTypeLabel(currentPath);
        if (!ec && size) result += L"  ·  " + formatSize(size);
        auto mt = modifiedTime(currentPath);
        if (!mt.empty()) result += L"  ·  修改于 " + mt;
        if (currentPaths.size() > 1)
            result += std::format(L"  ·  本次共 {} 个项目", currentPaths.size());
        return result;
    }

    inline void paintInfo(HDC dc, const RECT& c)
    {
        RECT icon{ c.left + 34, c.top + 38, c.left + 106, c.top + 110 };
        ClipboardHistoryV099Enhance::fillRoundRectAA(dc, icon, surface(), 14);
        std::wstring ext = currentPath.extension().wstring();
        if (ext.empty()) ext = L"FILE";
        else {
            if (ext.front() == L'.') ext.erase(ext.begin());
            for (auto& ch : ext) ch = (wchar_t)towupper(ch);
            if (ext.size() > 5) ext.resize(5);
        }
        ClipboardHistoryLegacy::drawText(dc, ext, icon, primary(), ClipboardHistoryLegacy::boldFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT name{ icon.right + 28, c.top + 38, c.right - 30, c.top + 66 };
        auto filename = currentPath.filename().wstring();
        ClipboardHistoryLegacy::drawText(dc, filename, name, text(), ClipboardHistoryLegacy::boldFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT meta{ icon.right + 28, c.top + 70, c.right - 30, c.top + 98 };
        ClipboardHistoryLegacy::drawText(dc, infoLine(), meta, muted(), ClipboardHistoryLegacy::uiFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT path{ c.left + 34, c.top + 132, c.right - 30, c.top + 188 };
        ClipboardHistoryLegacy::drawText(dc, currentPath.wstring(), path, muted(), ClipboardHistoryLegacy::smallFont,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

        RECT hint{ c.left + 34, c.top + 210, c.right - 30, c.top + 262 };
        std::wstring h = L"此类型使用轻量预览，不解析完整文档内容。可使用右上角“打开”或“位置”继续查看。";
        ClipboardHistoryLegacy::drawText(dc, h, hint, muted(), ClipboardHistoryLegacy::uiFont,
            DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    inline void paintWindow(HWND hwnd, HDC dc)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        ClipboardHistoryLegacy::fillRect(dc, rc, canvas());

        RECT title{ 20, 14, 330, 48 };
        ClipboardHistoryLegacy::drawText(dc, L"文件预览", title, text(), ClipboardHistoryLegacy::boldFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT fileName{ 20, 48, std::max(260, (int)rc.right - 20), 78 };
        ClipboardHistoryLegacy::drawText(dc, currentPath.filename().wstring(), fileName, muted(),
            ClipboardHistoryLegacy::smallFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        auto b = buttonRects(hwnd);
        if (kind == PreviewKind::Image) {
            drawButton(dc, b.zoomOut, L"−");
            drawButton(dc, b.zoomIn, L"+");
            drawButton(dc, b.actual, L"1:1", imageMode == ImageMode::Actual);
            drawButton(dc, b.fit, L"适应", imageMode == ImageMode::Fit);
        }
        drawButton(dc, b.open, L"打开");
        drawButton(dc, b.folder, L"位置");
        drawButton(dc, b.copyPath, copiedFeedback ? L"已复制" : L"复制路径", copiedFeedback);
        drawButton(dc, b.close, L"关闭");

        RECT c = contentRect(hwnd);
        ClipboardHistoryV099Enhance::fillRoundRectAA(dc, c, surface(), 10);
        ClipboardHistoryV099Enhance::strokeRoundRectAA(dc, c, border(), 1, 10);

        if (kind == PreviewKind::Image) paintImage(dc, c);
        else if (kind == PreviewKind::Info) paintInfo(dc, c);

        if (kind == PreviewKind::Text) {
            RECT thumb = textThumb(hwnd);
            if (thumb.right > thumb.left)
                ClipboardHistoryV099Enhance::fillRoundRectAA(dc, thumb, muted(), 4);
        }
    }

    inline LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
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
            if ((HWND)lParam == textEdit) {
                if (editBrush) DeleteObject(editBrush);
                editBrush = CreateSolidBrush(surface());
                HDC dc = (HDC)wParam;
                SetTextColor(dc, text());
                SetBkColor(dc, surface());
                return (LRESULT)editBrush;
            }
            break;
        case WM_LBUTTONDOWN:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            auto b = buttonRects(hwnd);
            if (PtInRect(&b.close, p)) { ShowWindow(hwnd, SW_HIDE); ClipboardHistoryWindowShim::restoreClipboardFocus(); return 0; }
            if (PtInRect(&b.open, p)) { openCurrent(); return 0; }
            if (PtInRect(&b.folder, p)) { revealCurrent(); return 0; }
            if (PtInRect(&b.copyPath, p)) { copyPathToClipboard(hwnd); InvalidateRect(hwnd,nullptr,FALSE); return 0; }
            if (kind == PreviewKind::Image) {
                if (PtInRect(&b.zoomOut, p)) {
                    imageScale = effectiveScale(contentRect(hwnd)) / 1.20;
                    imageMode = ImageMode::Custom; InvalidateRect(hwnd,nullptr,FALSE); return 0;
                }
                if (PtInRect(&b.zoomIn, p)) {
                    imageScale = effectiveScale(contentRect(hwnd)) * 1.20;
                    imageMode = ImageMode::Custom; InvalidateRect(hwnd,nullptr,FALSE); return 0;
                }
                if (PtInRect(&b.actual, p)) { imageMode=ImageMode::Actual; panX=panY=0; InvalidateRect(hwnd,nullptr,FALSE); return 0; }
                if (PtInRect(&b.fit, p)) { imageMode=ImageMode::Fit; panX=panY=0; InvalidateRect(hwnd,nullptr,FALSE); return 0; }
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
            if (imageDragging) { imageDragging=false; ReleaseCapture(); return 0; }
            break;
        case WM_MOUSEWHEEL:
            if (kind == PreviewKind::Image) {
                int d = GET_WHEEL_DELTA_WPARAM(wParam);
                double s = effectiveScale(contentRect(hwnd));
                imageScale = std::clamp(s * (d > 0 ? 1.12 : 1.0/1.12), 0.10, 8.0);
                imageMode = ImageMode::Custom;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == COPY_TIMER) {
                KillTimer(hwnd, COPY_TIMER); copiedFeedback=false; InvalidateRect(hwnd,nullptr,FALSE); return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); ClipboardHistoryWindowShim::restoreClipboardFocus(); return 0; }
            break;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE); ClipboardHistoryWindowShim::restoreClipboardFocus(); return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{}; HDC target = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            int w = std::max(1,(int)rc.right), h=std::max(1,(int)rc.bottom);
            HDC mem=CreateCompatibleDC(target); HBITMAP bm=mem?CreateCompatibleBitmap(target,w,h):nullptr;
            HGDIOBJ old=(mem&&bm)?SelectObject(mem,bm):nullptr;
            if (mem&&bm) { paintWindow(hwnd,mem); BitBlt(target,0,0,w,h,mem,0,0,SRCCOPY); }
            else paintWindow(hwnd,target);
            if (old) SelectObject(mem,old); if (bm) DeleteObject(bm); if (mem) DeleteDC(mem);
            EndPaint(hwnd,&ps); return 0;
        }
        case WM_DESTROY:
            textEdit=nullptr; textEditBase=nullptr; window=nullptr; currentImage.reset(); return 0;
        }
        return DefWindowProcW(hwnd,msg,wParam,lParam);
    }

    inline void ensureWindow()
    {
        if (window && IsWindow(window)) return;
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc{}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandleW(nullptr);
            wc.lpszClassName=L"StarCapClipboardFilePreviewV099"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
            registered = RegisterClassW(&wc) || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
        }
        if (!registered) return;
        // Keep file preview exactly aligned with the stable direct Text/Image popup.
        // All three preview kinds should open at the same size and screen position.
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        const int ww=std::min(760,std::max(620,(int)(work.right-work.left)*46/100));
        const int wh=std::min(540,std::max(420,(int)(work.bottom-work.top)*58/100));
        const int x=work.left+((work.right-work.left)-ww)/2;
        const int y=work.top+((work.bottom-work.top)-wh)/2;
        window=CreateWindowExW(WS_EX_TOOLWINDOW,L"StarCapClipboardFilePreviewV099",L"StarCap 文件预览",
            WS_POPUP|WS_THICKFRAME|WS_CLIPCHILDREN|WS_SYSMENU,x,y,ww,wh,
            ClipboardHistoryLegacy::historyWnd,nullptr,GetModuleHandleW(nullptr),nullptr);
    }

    inline bool showForIndex(int idx)
    {
        auto* item = ClipboardHistoryLegacy::itemAtListIndex(idx);
        if (!item || item->type != ClipboardHistoryLegacy::ItemType::File) return false;
        auto paths = ClipboardHistoryLegacy::filePaths(item->data);
        if (paths.empty()) return false;
        currentPaths = paths;
        currentPath = std::filesystem::path(paths.front());
        resetContent();
        ensureWindow();
        if (!window) return false;
        ensureTextEdit(window);
        if (kind == PreviewKind::Text) {
            SetWindowTextW(textEdit,currentText.c_str());
            SendMessageW(textEdit,EM_SETSEL,0,0);
            ShowWindow(textEdit,SW_SHOW);
            layoutText(window);
        } else if (textEdit) ShowWindow(textEdit,SW_HIDE);
        InvalidateRect(window,nullptr,FALSE);
        ShowWindow(window,SW_SHOW);
        SetForegroundWindow(window);
        if (kind==PreviewKind::Text && textEdit) SetFocus(textEdit);
        return true;
    }

    inline LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_LBUTTONUP && !ClipboardHistory::v099ScrollDragging) {
            DWORD hit=(DWORD)SendMessageW(hwnd,LB_ITEMFROMPOINT,0,lParam);
            int idx=LOWORD(hit);
            if (!HIWORD(hit) && idx>=0 && idx<(int)ClipboardHistoryLegacy::visibleItems.size()) {
                RECT itemRc{}; SendMessageW(hwnd,LB_GETITEMRECT,idx,(LPARAM)&itemRc);
                POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
                RECT expand=ClipboardHistoryLegacy::expandRect(itemRc);
                auto* item=ClipboardHistoryLegacy::itemAtListIndex(idx);
                if (item && item->type==ClipboardHistoryLegacy::ItemType::File &&
                    ClipboardHistoryLegacy::pointIn(expand,p)) {
                    SendMessageW(hwnd,LB_SETCURSEL,idx,0);
                    if (showForIndex(idx)) return 0;
                }
            }
        }
        return listBase ? CallWindowProcW(listBase,hwnd,msg,wParam,lParam)
            : DefWindowProcW(hwnd,msg,wParam,lParam);
    }

    inline void install()
    {
        HWND list=ClipboardHistoryLegacy::listWnd;
        if (!list || !IsWindow(list) || GetPropW(list,L"StarCapV099FilePreview")) return;
        listBase=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(list,GWLP_WNDPROC,(LONG_PTR)listProc));
        if(listBase) SetPropW(list,L"StarCapV099FilePreview",(HANDLE)1);
    }

    inline VOID CALLBACK installTimerProc(HWND hwnd, UINT, UINT_PTR id, DWORD)
    {
        if(hwnd&&IsWindow(hwnd)) KillTimer(hwnd,id); install();
    }

    inline void CALLBACK onShow(HWINEVENTHOOK,DWORD event,HWND hwnd,LONG idObject,LONG,DWORD,DWORD)
    {
        if(event!=EVENT_OBJECT_SHOW||idObject!=OBJID_WINDOW||!hwnd) return;
        DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid); if(pid!=GetCurrentProcessId()) return;
        wchar_t cls[96]{}; GetClassNameW(hwnd,cls,(int)std::size(cls));
        if(wcscmp(cls,L"StarCapClipboardHistoryV099")==0) SetTimer(hwnd,INSTALL_TIMER,140,installTimerProc);
    }

    struct Lifetime
    {
        Lifetime(){ showHook=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_SHOW,nullptr,onShow,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT); }
        ~Lifetime(){ if(showHook) UnhookWinEvent(showHook); if(editBrush) DeleteObject(editBrush); }
    };
    inline Lifetime lifetime;
}
