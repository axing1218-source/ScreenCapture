#pragma once

// File preview uses a different top toolbar from the direct Text/Image preview.
// The blank area between the file name and action buttons should still behave like
// a title bar. Subclass the already-stable file preview popup and return HTCAPTION
// for the whole header band except the actual buttons.
namespace ClipboardHistoryV099FilePreviewDragFix
{
    inline WNDPROC baseProc{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };

    inline bool validRect(const RECT& r)
    {
        return r.right > r.left && r.bottom > r.top;
    }

    inline bool overButton(HWND hwnd, POINT p)
    {
        auto b = ClipboardHistoryV099FilePreview::buttonRects(hwnd);
        RECT buttons[] = {
            b.zoomOut, b.zoomIn, b.actual, b.fit,
            b.open, b.folder, b.copyPath, b.close
        };
        for (auto& r : buttons) {
            if (validRect(r) && PtInRect(&r, p)) return true;
        }
        return false;
    }

    inline LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCHITTEST) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);

            // Header is 92px tall; content begins at y=92. Make every unused
            // header pixel draggable, including the large gap highlighted by the
            // user for text/document previews. Buttons stay fully interactive.
            if (p.y >= 0 && p.y < 92) {
                if (overButton(hwnd, p)) return HTCLIENT;
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        return baseProc ? CallWindowProcW(baseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void install(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd) || GetPropW(hwnd, L"StarCapFilePreviewDragFix")) return;
        baseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)proc));
        if (baseProc) SetPropW(hwnd, L"StarCapFilePreviewDragFix", (HANDLE)1);
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
        if (wcscmp(cls, L"StarCapClipboardFilePreviewV099") == 0)
            install(hwnd);
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
        }
    };

    inline Lifetime lifetime;
}
