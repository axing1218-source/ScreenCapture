#pragma once

// Final dark-mode polish for the legacy full-content text preview.
// The original EDIT control still asks its parent for a fixed light brush through
// WM_CTLCOLOREDIT. Wrap the already-created full-preview window and answer that
// message with the app theme palette so the text surface no longer becomes a
// large bright rectangle in dark mode.
namespace ClipboardHistoryV099DarkPreviewFix
{
    inline WNDPROC baseProc{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };
    inline HBRUSH editBrush{ nullptr };
    inline COLORREF editBrushColor{ CLR_INVALID };

    inline COLORREF editBg()
    {
        return ClipboardHistory::v099DarkMode() ? RGB(31, 38, 51) : RGB(244, 244, 244);
    }

    inline COLORREF editText()
    {
        return ClipboardHistory::v099DarkMode() ? RGB(232, 234, 237) : RGB(32, 33, 36);
    }

    inline HBRUSH currentBrush()
    {
        const COLORREF wanted = editBg();
        if (!editBrush || editBrushColor != wanted) {
            if (editBrush) DeleteObject(editBrush);
            editBrush = CreateSolidBrush(wanted);
            editBrushColor = wanted;
        }
        return editBrush;
    }

    inline LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_CTLCOLOREDIT &&
            (HWND)lParam == ClipboardHistoryWindowShim::previewEdit) {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, editText());
            SetBkColor(dc, editBg());
            return (LRESULT)currentBrush();
        }

        LRESULT result = baseProc
            ? CallWindowProcW(baseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        if ((msg == WM_SHOWWINDOW && wParam) || msg == WM_SETTINGCHANGE) {
            if (ClipboardHistoryWindowShim::previewEdit &&
                IsWindow(ClipboardHistoryWindowShim::previewEdit)) {
                InvalidateRect(ClipboardHistoryWindowShim::previewEdit, nullptr, TRUE);
            }
        }
        if (msg == WM_DESTROY) {
            baseProc = nullptr;
        }
        return result;
    }

    inline void install(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd) ||
            GetPropW(hwnd, L"StarCapV099DarkPreviewFix")) return;

        baseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)proc));
        if (!baseProc) return;

        SetPropW(hwnd, L"StarCapV099DarkPreviewFix", (HANDLE)1);
        if (ClipboardHistoryWindowShim::previewEdit &&
            IsWindow(ClipboardHistoryWindowShim::previewEdit)) {
            InvalidateRect(ClipboardHistoryWindowShim::previewEdit, nullptr, TRUE);
        }
    }

    inline void CALLBACK onShow(HWINEVENTHOOK, DWORD event, HWND hwnd,
        LONG idObject, LONG, DWORD, DWORD)
    {
        if (event != EVENT_OBJECT_SHOW || idObject != OBJID_WINDOW || !hwnd) return;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return;

        wchar_t cls[96]{};
        GetClassNameW(hwnd, cls, (int)std::size(cls));
        if (wcscmp(cls, L"StarCapClipboardFullView") == 0)
            install(hwnd);
    }

    struct Lifetime
    {
        Lifetime()
        {
            showHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                onShow, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
            if (ClipboardHistoryLegacy::fullWnd && IsWindow(ClipboardHistoryLegacy::fullWnd))
                install(ClipboardHistoryLegacy::fullWnd);
        }
        ~Lifetime()
        {
            if (showHook) UnhookWinEvent(showHook);
            if (editBrush) DeleteObject(editBrush);
        }
    };

    inline Lifetime lifetime;
}
