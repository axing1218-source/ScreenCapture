#pragma once

// Final dark-mode polish for the legacy full-content text preview.
// The preview body is a read-only EDIT control. Windows sends WM_CTLCOLORSTATIC
// (rather than WM_CTLCOLOREDIT) for ES_READONLY edits, so handle both messages.
// The compact preview also reapplies its native DWM frame while it is shown or
// activated; keep that frame color synchronized with the active app theme too.
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

    inline COLORREF frameBorder()
    {
        return ClipboardHistory::v099DarkMode() ? RGB(58, 62, 70) : RGB(229, 231, 235);
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

    inline void applyFrame(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return;
        ClipboardHistoryWindowShim::applyNativeFrame(hwnd, frameBorder());
    }

    inline void refreshEdit()
    {
        HWND edit = ClipboardHistoryWindowShim::previewEdit;
        if (!edit || !IsWindow(edit)) return;
        InvalidateRect(edit, nullptr, TRUE);
        UpdateWindow(edit);
    }

    inline LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // ES_READONLY EDIT controls normally request WM_CTLCOLORSTATIC. Keep
        // WM_CTLCOLOREDIT as well so the surface stays correct if editability
        // changes later.
        if ((msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT) &&
            (HWND)lParam == ClipboardHistoryWindowShim::previewEdit) {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, editText());
            SetBkColor(dc, editBg());
            SetBkMode(dc, OPAQUE);
            return (LRESULT)currentBrush();
        }

        LRESULT result = baseProc
            ? CallWindowProcW(baseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        // previewFrameProc calls applyNativeFrame() with its legacy light border
        // on these messages. Reapply the theme-aware frame after the base proc so
        // dark mode cannot leave a one-pixel light outline around the popup.
        if (msg == WM_NCACTIVATE || msg == WM_SIZE || msg == WM_ACTIVATE ||
            msg == WM_SETFOCUS || (msg == WM_SHOWWINDOW && wParam) ||
            msg == WM_SETTINGCHANGE) {
            applyFrame(hwnd);
        }

        if ((msg == WM_SHOWWINDOW && wParam) || msg == WM_SETTINGCHANGE ||
            msg == WM_ACTIVATE || msg == WM_SETFOCUS) {
            refreshEdit();
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
        applyFrame(hwnd);
        refreshEdit();
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
