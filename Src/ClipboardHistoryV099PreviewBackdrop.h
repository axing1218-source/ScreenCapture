#pragma once

// Modal-like visual separation for clipboard previews.
//
// Text/image/file previews are intentionally clean WS_POPUP windows. To make them
// read as a foreground layer (especially when both surfaces are white in light
// mode), dim only the clipboard window behind them with a restrained black overlay.
// The preview itself stays at full brightness and the existing 1px themed outline
// remains unchanged. No DWM/frame manipulation is involved here.
namespace ClipboardHistoryV099PreviewBackdrop
{
    inline HWND overlay{ nullptr };
    inline HWND activePreview{ nullptr };
    inline HWINEVENTHOOK eventHook{ nullptr };

    inline BYTE overlayAlpha()
    {
        // Light mode needs stronger separation because both layers are mostly white.
        // Dark mode already has more natural depth, so keep it a little softer.
        // LWA_ALPHA is opacity of the black overlay: 77 ~= 30%, 56 ~= 22%.
        return ClipboardHistory::v099DarkMode() ? (BYTE)56 : (BYTE)77;
    }

    inline bool isPreviewWindow(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return false;
        wchar_t cls[96]{};
        GetClassNameW(hwnd, cls, (int)std::size(cls));
        return wcscmp(cls, L"StarCapClipboardDirectPreviewV099") == 0 ||
               wcscmp(cls, L"StarCapClipboardFilePreviewV099") == 0;
    }

    inline LRESULT CALLBACK overlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MOUSEWHEEL:
            // The backdrop is deliberately modal: it prevents accidental clicks
            // on history rows while a preview is open.
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(dc, &rc, b);
            DeleteObject(b);
            EndPaint(hwnd, &ps);
            return 0;
        }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void ensureOverlay()
    {
        HWND parent = ClipboardHistoryLegacy::historyWnd;
        if (!parent || !IsWindow(parent)) return;
        if (overlay && IsWindow(overlay) && GetParent(overlay) == parent) return;

        if (overlay && IsWindow(overlay)) DestroyWindow(overlay);
        overlay = nullptr;

        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = overlayProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"StarCapClipboardPreviewBackdropV099";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            registered = RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }
        if (!registered) return;

        RECT rc{}; GetClientRect(parent, &rc);
        overlay = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE,
            L"StarCapClipboardPreviewBackdropV099", L"",
            WS_CHILD,
            0, 0, std::max(1L, rc.right), std::max(1L, rc.bottom),
            parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (overlay)
            SetLayeredWindowAttributes(overlay, 0, overlayAlpha(), LWA_ALPHA);
    }

    inline void showFor(HWND preview)
    {
        HWND parent = ClipboardHistoryLegacy::historyWnd;
        if (!parent || !IsWindow(parent) || !IsWindowVisible(parent)) return;
        ensureOverlay();
        if (!overlay) return;

        activePreview = preview;
        RECT rc{}; GetClientRect(parent, &rc);
        SetWindowPos(overlay, HWND_TOP, 0, 0,
            std::max(1L, rc.right), std::max(1L, rc.bottom),
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetLayeredWindowAttributes(overlay, 0, overlayAlpha(), LWA_ALPHA);
        InvalidateRect(overlay, nullptr, TRUE);
    }

    inline void hideFor(HWND preview)
    {
        if (preview && activePreview && preview != activePreview) return;
        activePreview = nullptr;
        if (overlay && IsWindow(overlay)) ShowWindow(overlay, SW_HIDE);
    }

    inline void CALLBACK onPreviewEvent(HWINEVENTHOOK, DWORD event, HWND hwnd,
        LONG idObject, LONG, DWORD, DWORD)
    {
        if (idObject != OBJID_WINDOW || !isPreviewWindow(hwnd)) return;
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return;

        if (event == EVENT_OBJECT_SHOW)
            showFor(hwnd);
        else if (event == EVENT_OBJECT_HIDE || event == EVENT_OBJECT_DESTROY)
            hideFor(hwnd);
    }

    struct Lifetime
    {
        Lifetime()
        {
            eventHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_DESTROY, nullptr,
                onPreviewEvent, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
        }
        ~Lifetime()
        {
            if (eventHook) UnhookWinEvent(eventHook);
        }
    };

    inline Lifetime lifetime;
}
