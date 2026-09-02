        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 254, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 113, 90, 255, 114, 90, 255, 114, 90,
        255, 114, 90, 255, 114, 90, 255, 114, 90, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        250, 116, 94, 255, 115, 90, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 115, 90, 251, 118, 95,
        237, 168, 157, 255, 119, 93, 255, 113, 89, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 113, 89, 255, 119, 93, 236, 169, 157,
        230, 230, 230, 237, 169, 158, 249, 125, 102, 255, 113, 89, 255, 112, 89, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 89, 255, 113, 89, 249, 125, 102, 237, 167, 157, 230, 230, 230
    };

    inline void v099DrawAppLogo(HDC dc, RECT r)
    {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 32;
        bi.bmiHeader.biHeight = 32;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, HALFTONE);
        StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top,
            0, 0, 32, 32, v099AppLogoBgr, &bi, DIB_RGB_COLORS, SRCCOPY);
    }

    inline void v099DrawButton(HDC dc, RECT r, const std::wstring& label, bool active = false, bool danger = false)
    {
        const COLORREF bg = active ? v099PrimarySoft() : v099Surface();
        const COLORREF fg = danger ? RGB(205, 62, 62) : (active ? v099Primary() : v099Muted());
        fillRoundRect(dc, r, bg, 8);
        if (active) strokeRoundRect(dc, r, RGB(210, 219, 255), 1, 8);
        drawText(dc, label, r, fg, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void v099DrawSmallAllIcon(HDC dc, RECT r, COLORREF color)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HBRUSH brush = CreateSolidBrush(color);
        auto oldP = SelectObject(dc, pen);
        auto oldB = SelectObject(dc, brush);
        Rectangle(dc, r.left + 2, r.top + 2, r.left + 7, r.top + 7);
        Rectangle(dc, r.left + 9, r.top + 2, r.left + 14, r.top + 7);
        Rectangle(dc, r.left + 2, r.top + 9, r.left + 7, r.top + 14);
        Rectangle(dc, r.left + 9, r.top + 9, r.left + 14, r.top + 14);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(brush);
        DeleteObject(pen);
    }

    inline void v099DrawSmallImageIcon(HDC dc, RECT r, COLORREF color)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        auto oldP = SelectObject(dc, pen);
        auto oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left + 1, r.top + 2, r.right - 1, r.bottom - 1);
        Ellipse(dc, r.left + 4, r.top + 4, r.left + 7, r.top + 7);
        MoveToEx(dc, r.left + 3, r.bottom - 4, nullptr);
        LineTo(dc, r.left + 7, r.top + 9);
        LineTo(dc, r.left + 10, r.top + 12);
        LineTo(dc, r.right - 3, r.top + 7);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(pen);
    }

    inline void v099DrawSmallFileIcon(HDC dc, RECT r, COLORREF color)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        auto oldP = SelectObject(dc, pen);
        auto oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left + 3, r.top + 2, r.right - 3, r.bottom - 2);
        MoveToEx(dc, r.right - 7, r.top + 2, nullptr);
        LineTo(dc, r.right - 3, r.top + 6);
        LineTo(dc, r.right - 3, r.top + 2);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(pen);
    }

    inline void v099DrawTabVisual(HDC dc, RECT tab, int index, bool selected)
    {
        const COLORREF fg = selected ? v099Primary() : v099Muted();
        const COLORREF bg = selected ? v099PrimarySoft() : v099Canvas();
        if (selected) fillRoundRect(dc, tab, bg, 8);

        RECT icon{ tab.left + 14, tab.top + 9, tab.left + 30, tab.top + 25 };
        switch (index) {
        case 0: v099DrawSmallAllIcon(dc, icon, fg); break;
        case 1: drawText(dc, L"Tt", icon, fg, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE); break;
        case 2: v099DrawSmallImageIcon(dc, icon, fg); break;
        case 3: v099DrawSmallFileIcon(dc, icon, fg); break;
        default: drawText(dc, L"★", icon, fg, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE); break;
        }

        size_t favCount = 0;
        for (const auto& it : items) if (it.favorite) ++favCount;
        std::wstring labels[5] = {
            L"全部", L"文本", L"图像", L"文件",
            favCount ? std::format(L"收藏 ({})", favCount) : L"收藏"
        };
