// Buffer each owner-drawn clipboard row off-screen, then present it in one blit.
// Large image rows are expensive to paint directly into the ListBox DC; exposing
// those intermediate GDI operations made a selected row look like its contents
// jumped even though its geometry did not change.
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

    SetViewportOrgEx(mem, oldOrigin.x, oldOrigin.y, nullptr);
    BitBlt(dis->hDC, rc.left, rc.top, width, height, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
}
