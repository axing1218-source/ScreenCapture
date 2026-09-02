    {
        if (msg == WM_KEYDOWN) {
            if (wParam == VK_ESCAPE) {
                if (GetWindowTextLengthW(hwnd) > 0) {
                    SetWindowTextW(hwnd, L"");
                    refreshList();
                    return 0;
                }
                if (listWnd) SetFocus(listWnd);
                return 0;
            }
            if (wParam == VK_RETURN) {
                useListItem(currentListIndex(), true);
                return 0;
            }
            if (wParam == VK_DOWN || wParam == VK_UP) {
                int idx = currentListIndex();
                int next = idx + (wParam == VK_DOWN ? 1 : -1);
                next = std::clamp(next, 0, std::max(0, (int)visibleItems.size() - 1));
                if (listWnd) {
                    SendMessageW(listWnd, LB_SETCURSEL, next, 0);
                    SendMessageW(listWnd, LB_SETTOPINDEX, std::max(0, next - 3), 0);
                    SetFocus(listWnd);
                }
                return 0;
            }
        }
        return ClipboardHistoryLegacy::searchProc(hwnd, msg, wParam, lParam);
    }

    inline int v099TotalContentHeight()
    {
        int total = 0;
        for (size_t idx : visibleItems) total += v099ItemHeight(items[idx]);
        return total;
    }

    inline int v099TopPixelForIndex(int topIndex)
    {
        int y = 0;
        const int limit = std::clamp(topIndex, 0, (int)visibleItems.size());
        for (int i = 0; i < limit; ++i) y += v099ItemHeight(items[visibleItems[(size_t)i]]);
        return y;
    }

    inline RECT v099ScrollThumbRect(HWND hwnd)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int viewport = std::max(1, (int)rc.bottom);
        const int total = v099TotalContentHeight();
        if (total <= viewport || visibleItems.empty()) return { 0,0,0,0 };

        const int trackTop = 5;
        const int trackBottom = std::max(trackTop + 1, (int)rc.bottom - 5);
        const int trackH = trackBottom - trackTop;
        const int thumbH = std::clamp((int)((long long)viewport * trackH / total), 28, std::max(28, trackH));
        const int topIndex = std::max(0, (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0));
        const int topPx = v099TopPixelForIndex(topIndex);
        const int maxTopPx = std::max(1, total - viewport);
        const int travel = std::max(0, trackH - thumbH);
        const int y = trackTop + (int)((long long)std::min(topPx, maxTopPx) * travel / maxTopPx);
        return { std::max(0, (int)rc.right - 6), y, std::max(0, (int)rc.right - 2), y + thumbH };
    }

    inline void v099DrawFloatingThumb(HWND hwnd, HDC dc)
    {
        RECT thumb = v099ScrollThumbRect(hwnd);
        if (thumb.right <= thumb.left || thumb.bottom <= thumb.top) return;
        fillRoundRect(dc, thumb, RGB(178, 181, 186), 4);
    }

    inline void v099SetScrollFromThumbY(HWND hwnd, int mouseY, int dragOffset)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int viewport = std::max(1, (int)rc.bottom);
        const int total = v099TotalContentHeight();
        if (total <= viewport || visibleItems.empty()) return;

        RECT thumb = v099ScrollThumbRect(hwnd);
        const int thumbH = std::max(1, (int)(thumb.bottom - thumb.top));
        const int trackTop = 5;
        const int trackBottom = std::max(trackTop + 1, (int)rc.bottom - 5);
        const int travel = std::max(1, trackBottom - trackTop - thumbH);
        const int y = std::clamp(mouseY - dragOffset, trackTop, trackTop + travel);
        const int maxTopPx = std::max(1, total - viewport);
        const int targetPx = (int)((long long)(y - trackTop) * maxTopPx / travel);

        int acc = 0;
        int targetIndex = 0;
        for (size_t i = 0; i < visibleItems.size(); ++i) {
            const int h = v099ItemHeight(items[visibleItems[i]]);
            if (acc + h / 2 >= targetPx) { targetIndex = (int)i; break; }
            acc += h;
            targetIndex = (int)i;
        }
        SendMessageW(hwnd, LB_SETTOPINDEX, targetIndex, 0);
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    inline LRESULT CALLBACK v099ListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_LBUTTONDOWN) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc{}; GetClientRect(hwnd, &rc);
            if (p.x >= rc.right - 12) {
                RECT thumb = v099ScrollThumbRect(hwnd);
                if (thumb.right > thumb.left) {
