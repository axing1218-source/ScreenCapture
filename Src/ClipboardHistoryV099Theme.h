#pragma once

namespace ClipboardHistory
{
    inline void v099RefreshTheme()
    {
        ClipboardHistoryLegacy::themeMode = v099DarkMode()
            ? ClipboardHistoryLegacy::ThemeMode::Dark
            : ClipboardHistoryLegacy::ThemeMode::Light;

        v099SyncBrushes();

        if (ClipboardHistoryWindowShim::previewEditBrush) {
            DeleteObject(ClipboardHistoryWindowShim::previewEditBrush);
            ClipboardHistoryWindowShim::previewEditBrush = nullptr;
        }

        if (ClipboardHistoryLegacy::historyWnd && IsWindow(ClipboardHistoryLegacy::historyWnd)) {
            v099ApplyRoundRegion(ClipboardHistoryLegacy::historyWnd);
            RedrawWindow(ClipboardHistoryLegacy::historyWnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        if (ClipboardHistoryLegacy::listWnd && IsWindow(ClipboardHistoryLegacy::listWnd))
            RedrawWindow(ClipboardHistoryLegacy::listWnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        if (ClipboardHistoryLegacy::searchWnd && IsWindow(ClipboardHistoryLegacy::searchWnd))
            RedrawWindow(ClipboardHistoryLegacy::searchWnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        if (ClipboardHistoryLegacy::fullWnd && IsWindow(ClipboardHistoryLegacy::fullWnd))
            RedrawWindow(ClipboardHistoryLegacy::fullWnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}
