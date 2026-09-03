// Buffer each owner-drawn clipboard row off-screen, then present it in one blit.
// Large image rows are expensive to paint directly into the ListBox DC; exposing
// those intermediate GDI operations made a selected row look like its contents
// jumped even though its geometry did not change.

inline void v099DrawTypedFileIcon(HDC dc, int x, int y, const std::filesystem::path& path)
{
    const std::wstring ext = path.extension().wstring();
    auto eq = [&](const wchar_t* value) {
        return _wcsicmp(ext.c_str(), value) == 0;
    };

    std::error_code dirError;
    const bool isDir = std::filesystem::is_directory(path, dirError);

    COLORREF bg = RGB(244, 246, 249);
    COLORREF fg = RGB(105, 112, 122);
    COLORREF border = RGB(222, 226, 232);
    const wchar_t* label = L"F";

    if (isDir) {
        bg = RGB(255, 247, 225);
        fg = RGB(190, 137, 35);
        border = RGB(244, 221, 166);
        label = L"DIR";
    }
    else if (eq(L".png") || eq(L".jpg") || eq(L".jpeg") || eq(L".bmp") ||
             eq(L".webp") || eq(L".gif") || eq(L".tif") || eq(L".tiff")) {
        bg = RGB(240, 243, 255);
        fg = RGB(88, 112, 255);
        border = RGB(211, 219, 255);
        label = L"IMG";
    }
    else if (eq(L".pdf")) {
        bg = RGB(255, 240, 242);
        fg = RGB(215, 67, 82);
        border = RGB(247, 205, 211);
        label = L"PDF";
    }
    else if (eq(L".doc") || eq(L".docx") || eq(L".txt") || eq(L".rtf") || eq(L".md")) {
        bg = RGB(238, 246, 255);
        fg = RGB(65, 116, 205);
        border = RGB(204, 224, 247);
        label = L"DOC";
    }
    else if (eq(L".xls") || eq(L".xlsx") || eq(L".csv")) {
        bg = RGB(238, 250, 243);
        fg = RGB(47, 143, 87);
        border = RGB(202, 232, 214);
        label = L"XLS";
    }
    else if (eq(L".ppt") || eq(L".pptx")) {
        bg = RGB(255, 244, 235);
        fg = RGB(207, 105, 49);
        border = RGB(245, 218, 196);
        label = L"PPT";
    }
    else if (eq(L".zip") || eq(L".rar") || eq(L".7z") || eq(L".tar") || eq(L".gz") || eq(L".bz2")) {
        bg = RGB(255, 248, 230);
        fg = RGB(174, 123, 32);
        border = RGB(240, 221, 171);
        label = L"ZIP";
    }
    else if (eq(L".exe") || eq(L".msi") || eq(L".bat") || eq(L".cmd") || eq(L".ps1")) {
        bg = RGB(240, 245, 249);
        fg = RGB(71, 102, 126);
        border = RGB(207, 220, 230);
        label = L"EXE";
    }
    else if (eq(L".json") || eq(L".xml") || eq(L".yaml") || eq(L".yml") ||
             eq(L".ini") || eq(L".log") || eq(L".cpp") || eq(L".c") ||
             eq(L".h") || eq(L".hpp") || eq(L".js") || eq(L".ts") ||
             eq(L".py") || eq(L".html") || eq(L".css")) {
        bg = RGB(246, 241, 255);
        fg = RGB(119, 82, 176);
        border = RGB(224, 211, 244);
        label = L"<>";
    }

    RECT icon{ x, y, x + 22, y + 22 };
    fillRoundRect(dc, icon, bg, 4);
    strokeRoundRect(dc, icon, border, 1, 4);
    RECT textRc = icon;
    textRc.left += 1;
    textRc.right -= 1;
    drawText(dc, label, textRc, fg, smallFont,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
        // Cover the legacy generic icon first, then place the type-specific icon.
        // The filename starts 28 px to the right, so this remains inside its slot.
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
