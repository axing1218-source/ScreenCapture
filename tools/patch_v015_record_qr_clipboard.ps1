$ErrorActionPreference = 'Stop'

function Replace-Checked([ref]$textRef, [string]$old, [string]$new, [string]$label) {
    if (-not $textRef.Value.Contains($old)) { throw "Patch target not found: $label" }
    $textRef.Value = $textRef.Value.Replace($old, $new)
}

# -----------------------------------------------------------------------------
# 1) Recording: make the selected recording rectangle a REAL window-region hole.
#    Transparent pixels alone can still make chat apps consider themselves
#    occluded, which pauses animated stickers.  WindowFromPoint after SetWindowRgn
#    then resolves to the actual app below; return foreground focus to it.
# -----------------------------------------------------------------------------
$path = 'Src\\Win\\WinCap.cpp'
$src = Get-Content $path -Raw
Replace-Checked ([ref]$src) @'
void WinCap::startVideo()
{
    if (stage != CapStage::Adjust || !cutMask->hasRect()) return;
    stage = CapStage::Video;
    enterLiveStage();
    capVideo = std::make_unique<CapVideo>(this);
    // ToolCap 原地换成 ToolVideo
    capVideo->makeTool();
}
'@ @'
void WinCap::startVideo()
{
    if (stage != CapStage::Adjust || !cutMask->hasRect()) return;
    stage = CapStage::Video;
    enterLiveStage();

    // Do not merely draw the selected rectangle transparent. Remove it from the
    // capture window's actual Win32 region so the application below is genuinely
    // visible to occlusion/focus heuristics (important for animated stickers).
    hollowWin();

    // The action was initiated by the user, so Windows normally permits this
    // foreground hand-off. WindowFromPoint sees through the region hole.
    auto& r = cutMask->maskRect;
    POINT center{
        x + (LONG)((r.left + r.right) * .5f),
        y + (LONG)((r.top + r.bottom) * .5f)
    };
    HWND below = WindowFromPoint(center);
    if (below && below != hwnd) {
        below = GetAncestor(below, GA_ROOT);
        if (below && below != hwnd) SetForegroundWindow(below);
    }

    capVideo = std::make_unique<CapVideo>(this);
    // ToolCap 原地换成 ToolVideo; WS_EX_NOACTIVATE keeps focus on the recorded app.
    capVideo->makeTool();
}
'@ 'real recording region hole'
Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 2) QR recognition: multi-pass recovery for small / anti-aliased / inverted QR.
#    Keep quirc, but feed it stronger candidate images before giving up:
#    original -> quiet-zone padding -> Otsu binary -> upscale -> inverted variants.
# -----------------------------------------------------------------------------
$path = 'Src\\Util.cpp'
$src = Get-Content $path -Raw
$pattern = '(?s)std::wstring Util::decodeQrCode\(const int w, const int h, BYTE\* data\)\r?\n\{.*?\r?\n\}'
$replacement = @'
std::wstring Util::decodeQrCode(const int w, const int h, BYTE* data)
{
    if (w <= 0 || h <= 0 || !data) return L"";

    auto decodeGray = [](const std::vector<uint8_t>& gray, int gw, int gh) -> std::wstring {
        if (gw <= 0 || gh <= 0 || gray.size() < (size_t)gw * gh) return L"";
        auto qr = quirc_new();
        if (!qr) return L"";
        if (quirc_resize(qr, gw, gh) < 0) { quirc_destroy(qr); return L""; }
        int bw = 0, bh = 0;
        auto dst = quirc_begin(qr, &bw, &bh);
        memcpy(dst, gray.data(), (size_t)gw * gh);
        quirc_end(qr);

        std::wstring out;
        const int count = quirc_count(qr);
        for (int i = 0; i < count; ++i) {
            quirc_code code{};
            quirc_data qrData{};
            quirc_extract(qr, i, &code);
            auto err = quirc_decode(&code, &qrData);
            if (err == QUIRC_ERROR_DATA_ECC) {
                quirc_flip(&code);
                err = quirc_decode(&code, &qrData);
            }
            if (err != QUIRC_SUCCESS) continue;
            auto text = qrPayloadToWStr(qrData.payload, qrData.payload_len, qrData.data_type);
            if (text.empty()) continue;
            if (!out.empty()) out += L"\n";
            out += text;
        }
        quirc_destroy(qr);
        return out;
    };

    std::vector<uint8_t> gray((size_t)w * h);
    std::array<unsigned long long, 256> hist{};
    for (size_t i = 0; i < gray.size(); ++i) {
        auto px = data + i * 4; // BGRA
        uint8_t g = (uint8_t)((px[2] * 77 + px[1] * 150 + px[0] * 29) >> 8);
        gray[i] = g;
        ++hist[g];
    }

    // Fast path: preserve existing behaviour first.
    if (auto text = decodeGray(gray, w, h); !text.empty()) return text;

    auto addQuietZone = [](const std::vector<uint8_t>& src, int sw, int sh, int pad, uint8_t bg,
                           int& ow, int& oh) {
        ow = sw + pad * 2; oh = sh + pad * 2;
        std::vector<uint8_t> dst((size_t)ow * oh, bg);
        for (int y = 0; y < sh; ++y)
            memcpy(dst.data() + (size_t)(y + pad) * ow + pad,
                   src.data() + (size_t)y * sw, (size_t)sw);
        return dst;
    };

    // Tight screenshot selections often cut away QR's required quiet zone.
    {
        int pw = 0, ph = 0;
        int pad = std::clamp(std::min(w, h) / 12, 12, 48);
        auto padded = addQuietZone(gray, w, h, pad, 255, pw, ph);
        if (auto text = decodeGray(padded, pw, ph); !text.empty()) return text;
    }

    // Otsu threshold restores square modules that were softened by chat-app scaling.
    unsigned long long total = (unsigned long long)w * h;
    unsigned long long sum = 0;
    for (int i = 0; i < 256; ++i) sum += (unsigned long long)i * hist[i];
    unsigned long long wB = 0, sumB = 0;
    double best = -1.0;
    int threshold = 128;
    for (int t = 0; t < 255; ++t) {
        wB += hist[t];
        if (!wB) continue;
        const auto wF = total - wB;
        if (!wF) break;
        sumB += (unsigned long long)t * hist[t];
        const double mB = (double)sumB / wB;
        const double mF = (double)(sum - sumB) / wF;
        const double between = (double)wB * (double)wF * (mB - mF) * (mB - mF);
        if (between > best) { best = between; threshold = t; }
    }

    std::vector<uint8_t> binary(gray.size()), inverted(gray.size());
    for (size_t i = 0; i < gray.size(); ++i) {
        binary[i] = gray[i] <= threshold ? 0 : 255;
        inverted[i] = 255 - binary[i];
    }
    if (auto text = decodeGray(binary, w, h); !text.empty()) return text;
    if (auto text = decodeGray(inverted, w, h); !text.empty()) return text;

    // Small QR codes benefit from integer nearest-neighbour enlargement: no new
    // information is invented, but quirc gets several pixels per module again.
    int factor = 1;
    const int minSide = std::min(w, h), maxSide = std::max(w, h);
    if (minSide < 180) factor = 4;
    else if (minSide < 360) factor = 3;
    else if (minSide < 700) factor = 2;
    while (factor > 1 && (maxSide * factor > 2800 || (long long)w * h * factor * factor > 8000000LL)) --factor;

    if (factor > 1) {
        auto upscale = [factor](const std::vector<uint8_t>& src, int sw, int sh, int& ow, int& oh) {
            ow = sw * factor; oh = sh * factor;
            std::vector<uint8_t> dst((size_t)ow * oh);
            for (int y = 0; y < oh; ++y) {
                const int sy = y / factor;
                for (int x = 0; x < ow; ++x) dst[(size_t)y * ow + x] = src[(size_t)sy * sw + x / factor];
            }
            return dst;
        };
        int uw = 0, uh = 0;
        auto up = upscale(binary, w, h, uw, uh);
        int pw = 0, ph = 0;
        const int pad = std::clamp(16 * factor, 24, 96);
        auto padded = addQuietZone(up, uw, uh, pad, 255, pw, ph);
        if (auto text = decodeGray(padded, pw, ph); !text.empty()) return text;

        for (auto& v : up) v = 255 - v;
        auto paddedInv = addQuietZone(up, uw, uh, pad, 255, pw, ph);
        if (auto text = decodeGray(paddedInv, pw, ph); !text.empty()) return text;
    }

    return L"";
}
'@
$patched = [regex]::Replace($src, $pattern, $replacement, 1)
if ($patched -eq $src) { throw 'Patch target not found: QR decode function' }
Set-Content $path $patched -Encoding utf8

# -----------------------------------------------------------------------------
# 3) Clipboard history.  Session-only by design in this first version: clipboard
#    contents are not silently persisted to disk.  Keep up to 30 recent items,
#    each <= 8 MiB and a total <= 48 MiB. Supports text, DIB images, file drops.
# -----------------------------------------------------------------------------
$clipboardHeader = @'
#pragma once
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <algorithm>

namespace ClipboardHistory
{
    struct Item {
        UINT format{ 0 };
        std::vector<BYTE> data;
        std::wstring label;
    };

    inline std::vector<Item> items;
    inline size_t totalBytes{ 0 };
    inline HWND listenerWnd{ nullptr };
    inline HWND historyWnd{ nullptr };
    inline HWND listWnd{ nullptr };
    inline HFONT uiFont{ nullptr };
    inline bool suppressNext{ false };

    static constexpr UINT ID_LIST = 3101;
    static constexpr UINT ID_COPY = 3102;
    static constexpr UINT ID_DELETE = 3103;
    static constexpr UINT ID_CLEAR = 3104;
    static constexpr size_t MAX_ITEMS = 30;
    static constexpr size_t MAX_ITEM_BYTES = 8 * 1024 * 1024;
    static constexpr size_t MAX_TOTAL_BYTES = 48 * 1024 * 1024;

    inline std::wstring textPreview(const std::vector<BYTE>& bytes)
    {
        if (bytes.size() < sizeof(wchar_t)) return L"";
        auto p = reinterpret_cast<const wchar_t*>(bytes.data());
        const size_t maxChars = bytes.size() / sizeof(wchar_t);
        size_t n = 0;
        while (n < maxChars && p[n]) ++n;
        std::wstring s(p, p + n);
        for (auto& ch : s) if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
        while (s.find(L"  ") != std::wstring::npos) s.replace(s.find(L"  "), 2, L" ");
        if (s.size() > 68) s = s.substr(0, 68) + L"…";
        return s;
    }

    inline std::wstring makeLabel(UINT format, const std::vector<BYTE>& bytes)
    {
        if (format == CF_UNICODETEXT) {
            auto p = textPreview(bytes);
            return L"[文字] " + (p.empty() ? std::wstring(L"(空)") : p);
        }
        if ((format == CF_DIB || format == CF_DIBV5) && bytes.size() >= sizeof(BITMAPINFOHEADER)) {
            auto bi = reinterpret_cast<const BITMAPINFOHEADER*>(bytes.data());
            return std::format(L"[图片] {} × {}", std::abs(bi->biWidth), std::abs(bi->biHeight));
        }
        if (format == CF_HDROP && bytes.size() >= sizeof(DROPFILES)) {
            auto df = reinterpret_cast<const DROPFILES*>(bytes.data());
            if (df->pFiles < bytes.size()) {
                std::wstring first;
                if (df->fWide) {
                    auto p = reinterpret_cast<const wchar_t*>(bytes.data() + df->pFiles);
                    size_t cap = (bytes.size() - df->pFiles) / sizeof(wchar_t), n = 0;
                    while (n < cap && p[n]) ++n;
                    first.assign(p, p + n);
                }
                if (first.size() > 58) first = L"…" + first.substr(first.size() - 58);
                return L"[文件] " + (first.empty() ? std::wstring(L"文件/文件夹") : first);
            }
            return L"[文件] 文件/文件夹";
        }
        return L"[剪贴板项目]";
    }

    inline void refreshList()
    {
        if (!listWnd) return;
        SendMessageW(listWnd, WM_SETREDRAW, FALSE, 0);
        SendMessageW(listWnd, LB_RESETCONTENT, 0, 0);
        for (const auto& item : items) SendMessageW(listWnd, LB_ADDSTRING, 0, (LPARAM)item.label.c_str());
        SendMessageW(listWnd, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(listWnd, nullptr, TRUE);
    }

    inline void trim()
    {
        while (!items.empty() && (items.size() > MAX_ITEMS || totalBytes > MAX_TOTAL_BYTES)) {
            totalBytes -= items.back().data.size();
            items.pop_back();
        }
    }

    inline void captureCurrent()
    {
        if (suppressNext) { suppressNext = false; return; }
        if (!OpenClipboard(listenerWnd)) return;

        UINT format = 0;
        // Prefer a visual image when one is present, then file references, then text.
        if (IsClipboardFormatAvailable(CF_DIB)) format = CF_DIB;
        else if (IsClipboardFormatAvailable(CF_DIBV5)) format = CF_DIBV5;
        else if (IsClipboardFormatAvailable(CF_HDROP)) format = CF_HDROP;
        else if (IsClipboardFormatAvailable(CF_UNICODETEXT)) format = CF_UNICODETEXT;

        std::vector<BYTE> bytes;
        if (format) {
            HANDLE h = GetClipboardData(format);
            if (h) {
                const SIZE_T sz = GlobalSize(h);
                if (sz > 0 && sz <= MAX_ITEM_BYTES) {
                    auto p = GlobalLock(h);
                    if (p) {
                        bytes.resize((size_t)sz);
                        memcpy(bytes.data(), p, (size_t)sz);
                        GlobalUnlock(h);
                    }
                }
            }
        }
        CloseClipboard();
        if (!format || bytes.empty()) return;

        if (!items.empty() && items.front().format == format && items.front().data == bytes) return;
        Item item{ format, std::move(bytes), L"" };
        item.label = makeLabel(item.format, item.data);
        totalBytes += item.data.size();
        items.insert(items.begin(), std::move(item));
        trim();
        refreshList();
    }

    inline bool restoreIndex(int index)
    {
        if (index < 0 || index >= (int)items.size()) return false;
        const auto& item = items[(size_t)index];
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, item.data.size());
        if (!h) return false;
        auto p = GlobalLock(h);
        if (!p) { GlobalFree(h); return false; }
        memcpy(p, item.data.data(), item.data.size());
        GlobalUnlock(h);
        if (!OpenClipboard(historyWnd)) { GlobalFree(h); return false; }
        EmptyClipboard();
        suppressNext = true;
        if (!SetClipboardData(item.format, h)) {
            suppressNext = false;
            GlobalFree(h);
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    }

    inline int selectedIndex()
    {
        return listWnd ? (int)SendMessageW(listWnd, LB_GETCURSEL, 0, 0) : -1;
    }

    inline void layoutHistory(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int pad = 10, btnH = 30, hintH = 24, gap = 8;
        const int bottomY = rc.bottom - pad - btnH;
        if (listWnd) MoveWindow(listWnd, pad, pad + hintH,
            std::max(40, rc.right - pad * 2), std::max(40, bottomY - gap - (pad + hintH)), TRUE);
        auto copy = GetDlgItem(hwnd, ID_COPY), del = GetDlgItem(hwnd, ID_DELETE), clear = GetDlgItem(hwnd, ID_CLEAR);
        if (copy) MoveWindow(copy, pad, bottomY, 110, btnH, TRUE);
        if (del) MoveWindow(del, pad + 118, bottomY, 90, btnH, TRUE);
        if (clear) MoveWindow(clear, pad + 216, bottomY, 90, btnH, TRUE);
        auto hint = GetDlgItem(hwnd, 3199);
        if (hint) MoveWindow(hint, pad, pad, std::max(40, rc.right - pad * 2), hintH, TRUE);
    }

    inline LRESULT CALLBACK historyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_CREATE:
        {
            historyWnd = hwnd;
            if (!uiFont) uiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            auto hint = CreateWindowExW(0, L"STATIC",
                L"双击条目或点击“复制所选”即可恢复。本次运行保留最近 30 条；单项超过 8 MB 不记录。",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)3199, GetModuleHandleW(nullptr), nullptr);
            listWnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                0, 0, 0, 0, hwnd, (HMENU)ID_LIST, GetModuleHandleW(nullptr), nullptr);
            auto copy = CreateWindowExW(0, L"BUTTON", L"复制所选", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hwnd, (HMENU)ID_COPY, GetModuleHandleW(nullptr), nullptr);
            auto del = CreateWindowExW(0, L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hwnd, (HMENU)ID_DELETE, GetModuleHandleW(nullptr), nullptr);
            auto clear = CreateWindowExW(0, L"BUTTON", L"清空", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hwnd, (HMENU)ID_CLEAR, GetModuleHandleW(nullptr), nullptr);
            for (HWND c : { hint, listWnd, copy, del, clear }) if (c && uiFont) SendMessageW(c, WM_SETFONT, (WPARAM)uiFont, TRUE);
            refreshList();
            layoutHistory(hwnd);
            return 0;
        }
        case WM_SIZE:
            layoutHistory(hwnd); return 0;
        case WM_COMMAND:
        {
            const UINT id = LOWORD(wParam), code = HIWORD(wParam);
            if (id == ID_LIST && code == LBN_DBLCLK) { restoreIndex(selectedIndex()); return 0; }
            if (id == ID_COPY) { restoreIndex(selectedIndex()); return 0; }
            if (id == ID_DELETE) {
                int idx = selectedIndex();
                if (idx >= 0 && idx < (int)items.size()) {
                    totalBytes -= items[(size_t)idx].data.size();
                    items.erase(items.begin() + idx);
                    refreshList();
                    if (!items.empty()) SendMessageW(listWnd, LB_SETCURSEL, std::min(idx, (int)items.size() - 1), 0);
                }
                return 0;
            }
            if (id == ID_CLEAR) {
                items.clear(); totalBytes = 0; refreshList(); return 0;
            }
            break;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_DESTROY:
            if (historyWnd == hwnd) { historyWnd = nullptr; listWnd = nullptr; }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline LRESULT CALLBACK listenerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_CLIPBOARDUPDATE) { captureCurrent(); return 0; }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void init()
    {
        if (listenerWnd) return;
        const auto hInst = GetModuleHandleW(nullptr);
        WNDCLASSW lc{};
        lc.lpfnWndProc = listenerProc; lc.hInstance = hInst; lc.lpszClassName = L"WeShotClipboardListener";
        RegisterClassW(&lc);
        WNDCLASSW hc{};
        hc.lpfnWndProc = historyProc; hc.hInstance = hInst; hc.lpszClassName = L"WeShotClipboardHistory";
        hc.hCursor = LoadCursorW(nullptr, IDC_ARROW); hc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&hc);
        listenerWnd = CreateWindowExW(0, lc.lpszClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, hInst, nullptr);
        if (listenerWnd) {
            AddClipboardFormatListener(listenerWnd);
            captureCurrent();
        }
    }

    inline void show()
    {
        const auto hInst = GetModuleHandleW(nullptr);
        if (!historyWnd) {
            historyWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"WeShotClipboardHistory", L"WeShot 剪贴板历史",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 620, 460,
                nullptr, nullptr, hInst, nullptr);
        }
        if (!historyWnd) return;
        refreshList();
        ShowWindow(historyWnd, SW_SHOW);
        SetForegroundWindow(historyWnd);
        if (listWnd && !items.empty()) SendMessageW(listWnd, LB_SETCURSEL, 0, 0);
    }

    inline void dispose()
    {
        if (listenerWnd) {
            RemoveClipboardFormatListener(listenerWnd);
            DestroyWindow(listenerWnd);
            listenerWnd = nullptr;
        }
        if (historyWnd) { DestroyWindow(historyWnd); historyWnd = nullptr; listWnd = nullptr; }
        if (uiFont) { DeleteObject(uiFont); uiFont = nullptr; }
        items.clear(); totalBytes = 0; suppressNext = false;
    }
}
'@
Set-Content 'Src\\ClipboardHistory.h' $clipboardHeader -Encoding utf8

# Wire clipboard manager into application lifetime.
$path = 'Src\\App.cpp'
$src = Get-Content $path -Raw
Replace-Checked ([ref]$src) @'
#include "./Win/WinSetting.h"
'@ @'
#include "./Win/WinSetting.h"
#include "ClipboardHistory.h"
'@ 'App clipboard include'
Replace-Checked ([ref]$src) @'
    WinSetting::dispose();
    Lang::dispose();
'@ @'
    WinSetting::dispose();
    ClipboardHistory::dispose();
    Lang::dispose();
'@ 'clipboard dispose'
Replace-Checked ([ref]$src) @'
        bool flag = app->refuseSecondInstance();
        if (flag) return;
        Tray::init();
'@ @'
        bool flag = app->refuseSecondInstance();
        if (flag) return;
        ClipboardHistory::init();
        Tray::init();
'@ 'clipboard init'
Set-Content $path $src -Encoding utf8

# Add tray entry.
$path = 'Src\\Tray.cpp'
$src = Get-Content $path -Raw
Replace-Checked ([ref]$src) @'
#include "Setting.h"
'@ @'
#include "Setting.h"
#include "ClipboardHistory.h"
'@ 'Tray clipboard include'
Replace-Checked ([ref]$src) @'
	static std::unique_ptr<Tray> trayIns;
	static constexpr UINT settingMsg = 163;
	static constexpr UINT exitMsg = 164;
'@ @'
	static std::unique_ptr<Tray> trayIns;
	static constexpr UINT clipboardMsg = 162;
	static constexpr UINT settingMsg = 163;
	static constexpr UINT exitMsg = 164;
'@ 'clipboard tray command id'
Replace-Checked ([ref]$src) @'
	auto menu = CreatePopupMenu();
	AppendMenu(menu, MF_STRING, settingMsg, Lang::get(L"tray.setting").data());
	AppendMenu(menu, MF_STRING, exitMsg, Lang::get(L"tray.exit").data());
	auto menuId = Ling::App::get()->popupMenu(menu);
	if (menuId == settingMsg)
	{
		WinSetting::init();
	}
	else if (menuId == exitMsg)
'@ @'
	auto menu = CreatePopupMenu();
	AppendMenu(menu, MF_STRING, clipboardMsg, L"剪贴板历史");
	AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenu(menu, MF_STRING, settingMsg, Lang::get(L"tray.setting").data());
	AppendMenu(menu, MF_STRING, exitMsg, Lang::get(L"tray.exit").data());
	auto menuId = Ling::App::get()->popupMenu(menu);
	if (menuId == clipboardMsg)
	{
		ClipboardHistory::show();
	}
	else if (menuId == settingMsg)
	{
		WinSetting::init();
	}
	else if (menuId == exitMsg)
'@ 'clipboard tray menu'
Set-Content $path $src -Encoding utf8

# Verification.
$checks = @(
    @{ Path='Src\\Win\\WinCap.cpp'; Needle='hollowWin();' },
    @{ Path='Src\\Util.cpp'; Needle='Otsu threshold' },
    @{ Path='Src\\ClipboardHistory.h'; Needle='AddClipboardFormatListener' },
    @{ Path='Src\\Tray.cpp'; Needle='L"剪贴板历史"' },
    @{ Path='Src\\App.cpp'; Needle='ClipboardHistory::init();' }
)
foreach ($check in $checks) {
    $v = Get-Content $check.Path -Raw
    if (-not $v.Contains($check.Needle)) { throw "Verification failed: $($check.Needle)" }
}
Write-Host 'v0.8.15 recording hole + QR enhancement + clipboard history applied successfully.'
