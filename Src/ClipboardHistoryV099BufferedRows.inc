// Buffer each owner-drawn clipboard row off-screen, then present it in one blit.
// Large image rows are expensive to paint directly into the ListBox DC; exposing
// those intermediate GDI operations made a selected row look like its contents
// jumped even though its geometry did not change.

inline void v099IconLine(HDC dc, COLORREF color, int width,
    int x1, int y1, int x2, int y2)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

inline void v099IconFrame(HDC dc, const RECT& rc, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldP = SelectObject(dc, pen);
    HGDIOBJ oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(pen);
}

inline void v099IconPage(HDC dc, int x, int y, COLORREF color)
{
    RECT page{ x + 3, y + 1, x + 18, y + 21 };
    v099IconFrame(dc, page, color);
    v099IconLine(dc, color, 1, x + 13, y + 1, x + 18, y + 6);
    v099IconLine(dc, color, 1, x + 13, y + 1, x + 13, y + 6);
    v099IconLine(dc, color, 1, x + 13, y + 6, x + 18, y + 6);
}

inline void v099DrawTypedFileIcon(HDC dc, int x, int y, const std::filesystem::path& path)
{
    const std::wstring ext = path.extension().wstring();
    auto eq = [&](const wchar_t* value) {
        return _wcsicmp(ext.c_str(), value) == 0;
    };

    std::error_code dirError;
    const bool isDir = std::filesystem::is_directory(path, dirError);

    const COLORREF gray   = RGB(116, 123, 132);
    const COLORREF blue   = RGB(88, 112, 255);
    const COLORREF red    = RGB(215, 67, 82);
    const COLORREF green  = RGB(47, 143, 87);
    const COLORREF orange = RGB(207, 105, 49);
    const COLORREF amber  = RGB(190, 137, 35);
    const COLORREF purple = RGB(119, 82, 176);
    const COLORREF teal   = RGB(54, 137, 151);

    // Folder.
    if (isDir) {
        HBRUSH brush = CreateSolidBrush(RGB(255, 246, 218));
        HPEN pen = CreatePen(PS_SOLID, 1, amber);
        HGDIOBJ oldB = SelectObject(dc, brush);
        HGDIOBJ oldP = SelectObject(dc, pen);
        POINT pts[] = {
            { x + 2, y + 7 }, { x + 8, y + 7 }, { x + 10, y + 4 },
            { x + 19, y + 4 }, { x + 20, y + 18 }, { x + 2, y + 18 }
        };
        Polygon(dc, pts, (int)std::size(pts));
        SelectObject(dc, oldP);
        SelectObject(dc, oldB);
        DeleteObject(pen);
        DeleteObject(brush);
        return;
    }

    // Image.
    if (eq(L".png") || eq(L".jpg") || eq(L".jpeg") || eq(L".bmp") ||
        eq(L".webp") || eq(L".gif") || eq(L".tif") || eq(L".tiff")) {
        RECT frame{ x + 2, y + 3, x + 20, y + 19 };
        v099IconFrame(dc, frame, blue);
        HPEN pen = CreatePen(PS_SOLID, 1, blue);
        HGDIOBJ oldP = SelectObject(dc, pen);
        HGDIOBJ oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, x + 5, y + 6, x + 9, y + 10);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(pen);
        v099IconLine(dc, blue, 1, x + 4, y + 17, x + 9, y + 12);
        v099IconLine(dc, blue, 1, x + 9, y + 12, x + 12, y + 15);
        v099IconLine(dc, blue, 1, x + 12, y + 15, x + 17, y + 10);
        v099IconLine(dc, blue, 1, x + 17, y + 10, x + 19, y + 13);
        return;
    }

    // Video.
    if (eq(L".mov") || eq(L".mp4") || eq(L".m4v") || eq(L".avi") ||
        eq(L".mkv") || eq(L".wmv") || eq(L".webm") || eq(L".mpeg") || eq(L".mpg")) {
        RECT frame{ x + 2, y + 4, x + 20, y + 18 };
        v099IconFrame(dc, frame, purple);
        HBRUSH brush = CreateSolidBrush(purple);
        HPEN pen = CreatePen(PS_SOLID, 1, purple);
        HGDIOBJ oldB = SelectObject(dc, brush);
        HGDIOBJ oldP = SelectObject(dc, pen);
        POINT tri[] = { {x + 9, y + 8}, {x + 9, y + 15}, {x + 15, y + 11} };
        Polygon(dc, tri, 3);
        SelectObject(dc, oldP);
        SelectObject(dc, oldB);
        DeleteObject(pen);
        DeleteObject(brush);
        return;
    }

    // Audio.
    if (eq(L".mp3") || eq(L".wav") || eq(L".flac") || eq(L".aac") ||
        eq(L".m4a") || eq(L".ogg") || eq(L".wma")) {
        v099IconLine(dc, teal, 2, x + 11, y + 5, x + 11, y + 16);
        v099IconLine(dc, teal, 2, x + 11, y + 5, x + 18, y + 3);
        v099IconLine(dc, teal, 2, x + 18, y + 3, x + 18, y + 14);
        HBRUSH b = CreateSolidBrush(teal);
        HGDIOBJ oldB = SelectObject(dc, b);
        HGDIOBJ oldP = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, x + 5, y + 14, x + 12, y + 20);
        Ellipse(dc, x + 12, y + 12, x + 19, y + 18);
        SelectObject(dc, oldP);
        SelectObject(dc, oldB);
        DeleteObject(b);
        return;
    }

    // PDF.
    if (eq(L".pdf")) {
        v099IconPage(dc, x, y, red);
        v099IconLine(dc, red, 2, x + 6, y + 11, x + 15, y + 11);
        v099IconLine(dc, red, 2, x + 6, y + 15, x + 13, y + 15);
        return;
    }

    // Spreadsheet.
    if (eq(L".xls") || eq(L".xlsx") || eq(L".csv")) {
        v099IconPage(dc, x, y, green);
        RECT grid{ x + 6, y + 9, x + 16, y + 18 };
        v099IconFrame(dc, grid, green);
        v099IconLine(dc, green, 1, x + 11, y + 9, x + 11, y + 18);
        v099IconLine(dc, green, 1, x + 6, y + 13, x + 16, y + 13);
        return;
    }

    // Presentation.
    if (eq(L".ppt") || eq(L".pptx")) {
        v099IconPage(dc, x, y, orange);
        RECT screen{ x + 6, y + 9, x + 16, y + 16 };
        v099IconFrame(dc, screen, orange);
        v099IconLine(dc, orange, 1, x + 11, y + 16, x + 11, y + 19);
        return;
    }

    // Archive.
    if (eq(L".zip") || eq(L".rar") || eq(L".7z") || eq(L".tar") ||
        eq(L".gz") || eq(L".bz2")) {
        RECT box{ x + 3, y + 5, x + 19, y + 19 };
        v099IconFrame(dc, box, amber);
        v099IconLine(dc, amber, 1, x + 3, y + 9, x + 19, y + 9);
        for (int yy = 7; yy <= 16; yy += 3) {
            RECT dot{ x + 10, y + yy, x + 12, y + yy + 2 };
            HBRUSH b = CreateSolidBrush(amber);
            FillRect(dc, &dot, b);
            DeleteObject(b);
        }
        return;
    }

    // Executable / installer / command.
    if (eq(L".exe") || eq(L".msi") || eq(L".bat") || eq(L".cmd") || eq(L".ps1")) {
        RECT win{ x + 2, y + 4, x + 20, y + 19 };
        v099IconFrame(dc, win, gray);
        v099IconLine(dc, gray, 1, x + 2, y + 8, x + 20, y + 8);
        v099IconLine(dc, gray, 1, x + 6, y + 12, x + 9, y + 14);
        v099IconLine(dc, gray, 1, x + 9, y + 14, x + 6, y + 16);
        v099IconLine(dc, gray, 1, x + 11, y + 16, x + 16, y + 16);
        return;
    }

    // Code / config.
    if (eq(L".json") || eq(L".xml") || eq(L".yaml") || eq(L".yml") ||
        eq(L".ini") || eq(L".log") || eq(L".cpp") || eq(L".c") ||
        eq(L".h") || eq(L".hpp") || eq(L".js") || eq(L".ts") ||
        eq(L".py") || eq(L".html") || eq(L".css")) {
        v099IconPage(dc, x, y, purple);
        v099IconLine(dc, purple, 1, x + 9, y + 11, x + 6, y + 14);
        v099IconLine(dc, purple, 1, x + 6, y + 14, x + 9, y + 17);
        v099IconLine(dc, purple, 1, x + 13, y + 11, x + 16, y + 14);
        v099IconLine(dc, purple, 1, x + 16, y + 14, x + 13, y + 17);
        return;
    }

    // Documents and plain text.
    if (eq(L".doc") || eq(L".docx") || eq(L".txt") || eq(L".rtf") || eq(L".md")) {
        v099IconPage(dc, x, y, blue);
        v099IconLine(dc, blue, 1, x + 6, y + 10, x + 15, y + 10);
        v099IconLine(dc, blue, 1, x + 6, y + 13, x + 15, y + 13);
        v099IconLine(dc, blue, 1, x + 6, y + 16, x + 13, y + 16);
        return;
    }

    // Generic fallback.
    v099IconPage(dc, x, y, gray);
    v099IconLine(dc, gray, 1, x + 6, y + 11, x + 15, y + 11);
    v099IconLine(dc, gray, 1, x + 6, y + 15, x + 13, y + 15);
}

inline void v099OverlayTypedFileIcons(HDC dc, const DRAWITEMSTRUCT* dis)
{
    if (!dc || !dis || dis->itemID == (UINT)-1 || dis->itemID >= visibleItems.size()) return;

    const Item& item = items[visibleItems[dis->itemID]];
    if (item.type != ItemType::File) return;

    auto paths = filePaths(item.data);
    if (paths.empty()) return;

    RECT panel = dis->rcItem;
    panel.left += 2;
    panel.right -= 9;
    panel.top += 1;
    panel.bottom -= 1;

    const int bodyLeft = panel.left + 20;
    const int bodyTop = panel.top + 10;
    const int rowH = 28;
    const int count = std::min((int)paths.size(), 4);

    for (int i = 0; i < count; ++i) {
        const int y = bodyTop + i * rowH + 1;
        // Cover the legacy generic icon first, then draw the type-specific glyph.
        RECT clear{ bodyLeft - 1, y - 1, bodyLeft + 24, y + 23 };
        fillRect(dc, clear, v099Canvas());
        v099DrawTypedFileIcon(dc, bodyLeft, y, std::filesystem::path(paths[(size_t)i]));
    }
}

inline void v099DrawListItem(const DRAWITEMSTRUCT* dis)
{
    if (!dis || !dis->hDC) {
        v099DrawListItemDirect(dis);
        return;
    }

    const RECT rc = dis->rcItem;
    const int width = std::max(1, (int)(rc.right - rc.left));
    const int height = std::max(1, (int)(rc.bottom - rc.top));

    HDC mem = CreateCompatibleDC(dis->hDC);
    HBITMAP bitmap = mem ? CreateCompatibleBitmap(dis->hDC, width, height) : nullptr;
    if (!mem || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (mem) DeleteDC(mem);
        v099DrawListItemDirect(dis);
        return;
    }

    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);
    POINT oldOrigin{};
    SetViewportOrgEx(mem, -rc.left, -rc.top, &oldOrigin);

    DRAWITEMSTRUCT buffered = *dis;
    buffered.hDC = mem;
    v099DrawListItemDirect(&buffered);
    v099OverlayTypedFileIcons(mem, &buffered);

    SetViewportOrgEx(mem, oldOrigin.x, oldOrigin.y, nullptr);
    BitBlt(dis->hDC, rc.left, rc.top, width, height, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
}
