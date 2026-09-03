#pragma once

// The original file-preview hook used the same non-zero SetTimer ID as the final
// clipboard cleanup layer. On the history HWND, one timer could replace the other
// before either subclass was installed, so file rows sometimes fell through to the
// legacy "完整内容" popup. Install one final, uniquely-timed interaction wrapper
// after every other list layer and route file-expand clicks straight to the real
// preview window.
namespace ClipboardHistoryV099FilePreviewFix
{
    inline WNDPROC listBase{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };
    static constexpr UINT_PTR INSTALL_TIMER = 0xC1B7;

    inline LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_LBUTTONUP && !ClipboardHistory::v099ScrollDragging) {
            const DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            const int idx = LOWORD(hit);
            if (!HIWORD(hit) && idx >= 0 && idx < (int)ClipboardHistoryLegacy::visibleItems.size()) {
                RECT itemRc{};
                if (SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc) != LB_ERR) {
                    POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    RECT expand = ClipboardHistoryLegacy::expandRect(itemRc);
                    auto* item = ClipboardHistoryLegacy::itemAtListIndex(idx);
                    if (item && item->type == ClipboardHistoryLegacy::ItemType::File &&
                        ClipboardHistoryLegacy::pointIn(expand, p)) {
                        SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                        if (ClipboardHistoryV099FilePreview::showForIndex(idx)) return 0;
                    }
                }
            }
        }

        return listBase ? CallWindowProcW(listBase, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void install()
    {
        HWND list = ClipboardHistoryLegacy::listWnd;
        if (!list || !IsWindow(list) || GetPropW(list, L"StarCapV099FilePreviewFinal")) return;
        listBase = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(list, GWLP_WNDPROC, (LONG_PTR)listProc));
        if (listBase) SetPropW(list, L"StarCapV099FilePreviewFinal", (HANDLE)1);
    }

    inline VOID CALLBACK timerProc(HWND hwnd, UINT, UINT_PTR id, DWORD)
    {
        if (hwnd && IsWindow(hwnd)) KillTimer(hwnd, id);
        install();
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
        if (wcscmp(cls, L"StarCapClipboardHistoryV099") == 0) {
            // PointerFix: 90 ms, original file preview: 140 ms, FinalFix: 160 ms.
            // Use a unique ID and install last so the file action cannot be masked.
            SetTimer(hwnd, INSTALL_TIMER, 240, timerProc);
        }
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
