#pragma once

// WeShot clipboard manager v0.9.1
// UI/interaction model is intentionally close to the public Apache-2.0
// ZiuChen/ClipboardManager project so we can evaluate that proven workflow in
// a native Win32 implementation before doing WeShot-specific refinements.

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace ClipboardHistory
{
    enum class ItemType : uint8_t { Text = 1, Image = 2, File = 3 };
    enum class Tab : uint8_t { All = 0, Text = 1, Image = 2, File = 3, Favorite = 4 };

    struct Item {
        UINT format{ 0 };
        ItemType type{ ItemType::Text };
        std::vector<BYTE> data;
        uint64_t created{ 0 };
        uint64_t updated{ 0 };
        uint64_t hash{ 0 };
        bool favorite{ false };
    };

    struct Theme {
        COLORREF primary;
        COLORREF primaryLighter;
        COLORREF text;
        COLORREF textLighter;
        COLORREF textBg;
        COLORREF textBgLighter;
        COLORREF navBg;
        COLORREF navHover;
        COLORREF bg;
    };

    inline std::vector<Item> items;
    inline std::vector<size_t> visibleItems;
    inline size_t totalBytes{ 0 };
    inline HWND listenerWnd{ nullptr };
    inline HWND historyWnd{ nullptr };
    inline HWND listWnd{ nullptr };
    inline HWND searchWnd{ nullptr };
    inline HWND clearWnd{ nullptr };
    inline HWND fullWnd{ nullptr };
    inline HWND lastForeground{ nullptr };
    inline WNDPROC oldListProc{ nullptr };
    inline HFONT uiFont{ nullptr };
    inline HFONT smallFont{ nullptr };
    inline HFONT boldFont{ nullptr };
    inline HFONT emojiFont{ nullptr };
    inline HBRUSH editBrush{ nullptr };
    inline bool suppressNext{ false };
    inline Tab activeTab{ Tab::All };
    inline uint64_t fullItemHash{ 0 };
    inline bool multiMode{ false };
    inline std::unordered_set<uint64_t> multiHashes;

    static constexpr UINT ID_LIST = 4101;
    static constexpr UINT ID_SEARCH = 4102;
    static constexpr UINT ID_CLEAR_FLOAT = 4103;
    static constexpr size_t MAX_ITEMS = 800;
    static constexpr int MAX_AGE_DAYS = 14;
    static constexpr size_t MAX_ITEM_BYTES = 16ull * 1024ull * 1024ull;
    static constexpr size_t MAX_TOTAL_BYTES = 256ull * 1024ull * 1024ull;
    static constexpr uint32_t STORE_MAGIC = 0x57434831u; // WCH1
    static constexpr uint32_t STORE_VERSION = 1;

    inline LRESULT CALLBACK historyProc(HWND, UINT, WPARAM, LPARAM);
    inline LRESULT CALLBACK listenerProc(HWND, UINT, WPARAM, LPARAM);
    inline LRESULT CALLBACK listProc(HWND, UINT, WPARAM, LPARAM);
    inline LRESULT CALLBACK fullProc(HWND, UINT, WPARAM, LPARAM);

    inline uint64_t nowTicks()
    {
        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER u{};
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return u.QuadPart;
    }

    inline bool isDarkMode()
    {
        DWORD value = 1, size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
            return value == 0;
        return false;
    }

    inline Theme theme()
    {
        if (isDarkMode()) {
            return { RGB(0x44,0x8b,0xd2), RGB(0x49,0x97,0xe1), RGB(0xe8,0xe6,0xe3),
                RGB(0xb5,0xb5,0xb5), RGB(0x65,0x65,0x65), RGB(0x4e,0x4e,0x4e),
                RGB(0x22,0x24,0x26), RGB(0x2b,0x2e,0x30), RGB(0x18,0x1a,0x1b) };
        }
        return { RGB(0x32,0x71,0xae), RGB(0x49,0x97,0xe1), RGB(0x33,0x33,0x33),
            RGB(0x8a,0x8a,0x8a), RGB(0xf2,0xf2,0xf2), RGB(0xee,0xea,0xf3),
            RGB(0xee,0xee,0xee), RGB(0xde,0xde,0xde), RGB(0xff,0xff,0xff) };
    }

    inline void fillRect(HDC dc, const RECT& rc, COLORREF color)
    {
        HBRUSH b = CreateSolidBrush(color);
        FillRect(dc, &rc, b);
        DeleteObject(b);
    }

    inline void fillRoundRect(HDC dc, const RECT& rc, COLORREF color, int radius = 8)
    {
        HBRUSH b = CreateSolidBrush(color);
        auto oldB = SelectObject(dc, b);
        auto oldP = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
        SelectObject(dc, oldP);
        SelectObject(dc, oldB);
        DeleteObject(b);
    }

    inline void drawText(HDC dc, const std::wstring& text, RECT rc, COLORREF color,
        HFONT font, UINT flags)
    {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        auto old = font ? SelectObject(dc, font) : nullptr;
        DrawTextW(dc, text.c_str(), (int)text.size(), &rc, flags | DT_NOPREFIX);
        if (old) SelectObject(dc, old);
    }

    inline std::filesystem::path storagePath()
    {
        wchar_t buf[32768]{};
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)std::size(buf));
        std::filesystem::path root = (n > 0 && n < std::size(buf))
            ? std::filesystem::path(buf)
            : std::filesystem::temp_directory_path();
        root /= L"WeShot";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        return root / L"clipboard_history.bin";
    }

    inline uint64_t hashBytes(UINT format, const std::vector<BYTE>& bytes)
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](BYTE b) { h ^= b; h *= 1099511628211ull; };
        for (int i = 0; i < 4; ++i) mix((BYTE)((format >> (i * 8)) & 0xff));
        for (BYTE b : bytes) mix(b);
        return h;
    }

    inline std::wstring textFromData(const std::vector<BYTE>& bytes)
    {
        if (bytes.size() < sizeof(wchar_t)) return L"";
        auto p = reinterpret_cast<const wchar_t*>(bytes.data());
        const size_t cap = bytes.size() / sizeof(wchar_t);
        size_t n = 0;
        while (n < cap && p[n]) ++n;
        return std::wstring(p, p + n);
    }

    inline std::vector<std::wstring> filePaths(const std::vector<BYTE>& bytes)
    {
        std::vector<std::wstring> out;
        if (bytes.size() < sizeof(DROPFILES)) return out;
        auto df = reinterpret_cast<const DROPFILES*>(bytes.data());
        if (df->pFiles >= bytes.size()) return out;
        if (df->fWide) {
            const wchar_t* p = reinterpret_cast<const wchar_t*>(bytes.data() + df->pFiles);
            const size_t cap = (bytes.size() - df->pFiles) / sizeof(wchar_t);
            size_t pos = 0;
            while (pos < cap && p[pos]) {
                size_t n = 0;
                while (pos + n < cap && p[pos + n]) ++n;
                out.emplace_back(p + pos, p + pos + n);
                pos += n + 1;
            }
        }
        else {
            const char* p = reinterpret_cast<const char*>(bytes.data() + df->pFiles);
            const size_t cap = bytes.size() - df->pFiles;
            size_t pos = 0;
            while (pos < cap && p[pos]) {
                size_t n = 0;
                while (pos + n < cap && p[pos + n]) ++n;
                int wlen = MultiByteToWideChar(CP_ACP, 0, p + pos, (int)n, nullptr, 0);
                std::wstring s((size_t)std::max(0, wlen), L'\0');
                if (wlen > 0) MultiByteToWideChar(CP_ACP, 0, p + pos, (int)n, s.data(), wlen);
                out.push_back(std::move(s));
                pos += n + 1;
            }
        }
        return out;
    }

    inline std::wstring filePreview(const std::vector<BYTE>& bytes, size_t limit = 6)
    {
        auto paths = filePaths(bytes);
        if (paths.empty()) return L"文件/文件夹";
        std::wstring out;
        const size_t n = std::min(limit, paths.size());
        for (size_t i = 0; i < n; ++i) {
            std::filesystem::path p(paths[i]);
            std::wstring name = p.filename().wstring();
            if (name.empty()) name = paths[i];
            if (!out.empty()) out += L"\r\n";
            out += L"📄 " + name;
        }
        if (paths.size() > n) out += std::format(L"\r\n… 还有 {} 个项目", paths.size() - n);
        return out;
    }

    inline std::wstring relativeTime(uint64_t ticks)
    {
        const uint64_t now = nowTicks();
        uint64_t sec = now > ticks ? (now - ticks) / 10000000ull : 0;
        if (sec < 60) return L"刚刚";
        if (sec < 3600) return std::format(L"{} 分钟前", sec / 60);
        if (sec < 86400) return std::format(L"{} 小时前", sec / 3600);
        if (sec < 86400ull * 14) return std::format(L"{} 天前", sec / 86400ull);

        FILETIME ft{};
        ULARGE_INTEGER u{}; u.QuadPart = ticks;
        ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
        FILETIME localFt{}; SYSTEMTIME st{};
        if (FileTimeToLocalFileTime(&ft, &localFt) && FileTimeToSystemTime(&localFt, &st))
            return std::format(L"{:02}/{:02} {:02}:{:02}", st.wMonth, st.wDay, st.wHour, st.wMinute);
        return L"较早";
    }

    inline ItemType typeForFormat(UINT format)
    {
        if (format == CF_HDROP) return ItemType::File;
        if (format == CF_DIB || format == CF_DIBV5) return ItemType::Image;
        return ItemType::Text;
    }

    inline bool itemExpired(const Item& item)
    {
        if (item.favorite) return false;
        const uint64_t age = (uint64_t)MAX_AGE_DAYS * 24ull * 60ull * 60ull * 10000000ull;
        return nowTicks() > item.updated && nowTicks() - item.updated > age;
    }

    inline void trim()
    {
        items.erase(std::remove_if(items.begin(), items.end(), [](const Item& i) {
            return itemExpired(i);
            }), items.end());

        size_t normal = 0;
        for (const auto& i : items) if (!i.favorite) ++normal;
        for (size_t pos = items.size(); normal > MAX_ITEMS && pos > 0; ) {
            --pos;
            if (!items[pos].favorite) {
                items.erase(items.begin() + (ptrdiff_t)pos);
                --normal;
            }
        }

        totalBytes = 0;
        for (const auto& i : items) totalBytes += i.data.size();
        for (size_t pos = items.size(); totalBytes > MAX_TOTAL_BYTES && pos > 0; ) {
            --pos;
            if (!items[pos].favorite) {
                totalBytes -= items[pos].data.size();
                items.erase(items.begin() + (ptrdiff_t)pos);
            }
        }
    }

    inline void saveStore()
    {
        trim();
        auto path = storagePath();
        auto temp = path; temp += L".tmp";
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        const uint32_t count = (uint32_t)items.size();
        f.write((const char*)&STORE_MAGIC, sizeof(STORE_MAGIC));
        f.write((const char*)&STORE_VERSION, sizeof(STORE_VERSION));
        f.write((const char*)&count, sizeof(count));
        for (const auto& i : items) {
            uint8_t type = (uint8_t)i.type, fav = i.favorite ? 1 : 0;
            uint32_t fmt = (uint32_t)i.format;
            uint32_t size = (uint32_t)i.data.size();
            f.write((const char*)&type, sizeof(type));
            f.write((const char*)&fav, sizeof(fav));
            f.write((const char*)&fmt, sizeof(fmt));
            f.write((const char*)&i.created, sizeof(i.created));
            f.write((const char*)&i.updated, sizeof(i.updated));
            f.write((const char*)&i.hash, sizeof(i.hash));
            f.write((const char*)&size, sizeof(size));
            if (size) f.write((const char*)i.data.data(), size);
        }
        f.close();
        if (!f) return;
        std::error_code ec;
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temp, path, ec);
    }

    inline void loadStore()
    {
        items.clear(); totalBytes = 0;
        std::ifstream f(storagePath(), std::ios::binary);
        if (!f) return;
        uint32_t magic = 0, version = 0, count = 0;
        f.read((char*)&magic, sizeof(magic));
        f.read((char*)&version, sizeof(version));
        f.read((char*)&count, sizeof(count));
        if (!f || magic != STORE_MAGIC || version != STORE_VERSION || count > 5000) return;
        for (uint32_t n = 0; n < count; ++n) {
            uint8_t type = 0, fav = 0;
            uint32_t fmt = 0, size = 0;
            Item i;
            f.read((char*)&type, sizeof(type));
            f.read((char*)&fav, sizeof(fav));
            f.read((char*)&fmt, sizeof(fmt));
            f.read((char*)&i.created, sizeof(i.created));
            f.read((char*)&i.updated, sizeof(i.updated));
            f.read((char*)&i.hash, sizeof(i.hash));
            f.read((char*)&size, sizeof(size));
            if (!f || size > MAX_ITEM_BYTES) break;
            i.type = (ItemType)type; i.favorite = fav != 0; i.format = fmt;
            i.data.resize(size);
            if (size) f.read((char*)i.data.data(), size);
            if (!f) break;
            if (!itemExpired(i)) items.push_back(std::move(i));
        }
        trim();
    }

    inline bool containsInsensitive(std::wstring hay, std::wstring needle)
    {
        std::transform(hay.begin(), hay.end(), hay.begin(), towlower);
        std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
        return hay.find(needle) != std::wstring::npos;
    }

    inline std::wstring searchText()
    {
        if (!searchWnd) return L"";
        int n = GetWindowTextLengthW(searchWnd);
        std::wstring s((size_t)n, L'\0');
        if (n) GetWindowTextW(searchWnd, s.data(), n + 1);
        return s;
    }

    inline bool searchMatches(const Item& item, const std::wstring& query)
    {
        if (query.empty()) return true;
        if (item.type == ItemType::Image) return false; // mirrors the public uTools plugin
        std::wstring hay = item.type == ItemType::Text ? textFromData(item.data) : filePreview(item.data, 100);
        size_t pos = 0;
        while (pos < query.size()) {
            while (pos < query.size() && iswspace(query[pos])) ++pos;
            size_t end = pos;
            while (end < query.size() && !iswspace(query[end])) ++end;
            if (end > pos && !containsInsensitive(hay, query.substr(pos, end - pos))) return false;
            pos = end;
        }
        return true;
    }

    inline bool tabMatches(const Item& item)
    {
        switch (activeTab) {
        case Tab::Text: return item.type == ItemType::Text;
        case Tab::Image: return item.type == ItemType::Image;
        case Tab::File: return item.type == ItemType::File;
        case Tab::Favorite: return item.favorite;
        default: return true;
        }
    }

    inline void refreshList()
    {
        visibleItems.clear();
        std::wstring q = searchText();
        for (size_t i = 0; i < items.size(); ++i)
            if (tabMatches(items[i]) && searchMatches(items[i], q)) visibleItems.push_back(i);

        if (!listWnd) return;
        SendMessageW(listWnd, WM_SETREDRAW, FALSE, 0);
        SendMessageW(listWnd, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < visibleItems.size(); ++i)
            SendMessageW(listWnd, LB_ADDSTRING, 0, (LPARAM)i);
        SendMessageW(listWnd, WM_SETREDRAW, TRUE, 0);
        if (!visibleItems.empty()) SendMessageW(listWnd, LB_SETCURSEL, 0, 0);
        InvalidateRect(listWnd, nullptr, TRUE);
        if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE);
    }

    inline bool restoreItem(const Item& item)
    {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, item.data.size());
        if (!h) return false;
        void* p = GlobalLock(h);
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

    inline void pasteToPrevious()
    {
        HWND target = lastForeground;
        if (historyWnd) ShowWindow(historyWnd, SW_HIDE);
        if (target && IsWindow(target)) {
            SetForegroundWindow(target);
            Sleep(55);
        }
        INPUT in[4]{};
        in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
        in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'V';
        in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'V'; in[2].ki.dwFlags = KEYEVENTF_KEYUP;
        in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, in, sizeof(INPUT));
    }

    inline int currentListIndex()
    {
        return listWnd ? (int)SendMessageW(listWnd, LB_GETCURSEL, 0, 0) : -1;
    }

    inline Item* itemAtListIndex(int idx)
    {
        if (idx < 0 || idx >= (int)visibleItems.size()) return nullptr;
        size_t real = visibleItems[(size_t)idx];
        return real < items.size() ? &items[real] : nullptr;
    }

    inline void useListItem(int idx, bool paste)
    {
        Item* item = itemAtListIndex(idx);
        if (!item) return;
        const uint64_t h = item->hash;
        if (!restoreItem(*item)) return;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].hash == h) {
                items[i].updated = nowTicks();
                Item moved = std::move(items[i]);
                items.erase(items.begin() + (ptrdiff_t)i);
                items.insert(items.begin(), std::move(moved));
                break;
            }
        }
        saveStore();
        if (paste) pasteToPrevious();
        else refreshList();
    }

    inline void deleteListItem(int idx)
    {
        if (idx < 0 || idx >= (int)visibleItems.size()) return;
        size_t real = visibleItems[(size_t)idx];
        if (real >= items.size()) return;
        totalBytes -= std::min(totalBytes, items[real].data.size());
        multiHashes.erase(items[real].hash);
        items.erase(items.begin() + (ptrdiff_t)real);
        saveStore(); refreshList();
    }

    inline void toggleFavorite(int idx)
    {
        Item* item = itemAtListIndex(idx);
        if (!item) return;
        item->favorite = !item->favorite;
        saveStore(); refreshList();
    }

    inline size_t dibBitsOffset(const BITMAPINFOHEADER* bi)
    {
        if (!bi) return 0;
        size_t off = bi->biSize;
        if (bi->biBitCount <= 8) {
            DWORD colors = bi->biClrUsed ? bi->biClrUsed : (1u << bi->biBitCount);
            off += (size_t)colors * sizeof(RGBQUAD);
        }
        else if (bi->biCompression == BI_BITFIELDS && bi->biSize == sizeof(BITMAPINFOHEADER)) {
            off += 3 * sizeof(DWORD);
        }
        return off;
    }

    inline void drawDibFit(HDC dc, const Item& item, RECT rc)
    {
        if (item.data.size() < sizeof(BITMAPINFOHEADER)) return;
        auto bi = reinterpret_cast<const BITMAPINFOHEADER*>(item.data.data());
        if (bi->biWidth == 0 || bi->biHeight == 0) return;
        size_t off = dibBitsOffset(bi);
        if (off >= item.data.size()) return;
        int sw = std::abs(bi->biWidth), sh = std::abs(bi->biHeight);
        int aw = std::max(1, rc.right - rc.left), ah = std::max(1, rc.bottom - rc.top);
        double scale = std::min((double)aw / sw, (double)ah / sh);
        int dw = std::max(1, (int)(sw * scale)), dh = std::max(1, (int)(sh * scale));
        int x = rc.left + (aw - dw) / 2, y = rc.top + (ah - dh) / 2;
        StretchDIBits(dc, x, y, dw, dh, 0, 0, sw, sh,
            item.data.data() + off, reinterpret_cast<const BITMAPINFO*>(bi), DIB_RGB_COLORS, SRCCOPY);
    }

    inline std::wstring itemPreview(const Item& item, bool& oversized)
    {
        oversized = false;
        if (item.type == ItemType::File) return filePreview(item.data);
        if (item.type == ItemType::Image) return L"";
        std::wstring s = textFromData(item.data);
        size_t lines = 1;
        for (wchar_t c : s) if (c == L'\n') ++lines;
        oversized = lines > 6 || s.size() > 255;
        if (s.size() > 420) s.resize(420), s += L"…";
        return s;
    }

    inline void actionRects(HWND hwnd, const RECT& itemRc, RECT out[4])
    {
        RECT cr{}; GetClientRect(hwnd, &cr);
        const int bw = 34, gap = 5, total = bw * 4 + gap * 3;
        int x = cr.right - total - 16;
        int y = itemRc.top + std::max(4, ((itemRc.bottom - itemRc.top) - bw) / 2);
        for (int i = 0; i < 4; ++i) {
            out[i] = { x, y, x + bw, y + bw };
            x += bw + gap;
        }
    }

    inline void drawActionButton(HDC dc, RECT rc, const std::wstring& glyph, const Theme& t)
    {
        fillRoundRect(dc, rc, t.textBg, 8);
        drawText(dc, glyph, rc, t.primary, emojiFont ? emojiFont : uiFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void drawListItem(const DRAWITEMSTRUCT* dis)
    {
        if (!dis || dis->itemID == (UINT)-1 || dis->itemID >= visibleItems.size()) return;
        const Theme t = theme();
        const Item& item = items[visibleItems[dis->itemID]];
        RECT rc = dis->rcItem;
        const bool active = (dis->itemState & ODS_SELECTED) != 0;
        const bool multiSelected = multiHashes.contains(item.hash);
        fillRect(dis->hDC, rc, (active || multiSelected) ? t.textBgLighter : t.bg);
        if (active) {
            RECT strip{ rc.left, rc.top + 1, rc.left + 6, rc.bottom };
            fillRect(dis->hDC, strip, t.primary);
        }
        RECT topLine{ rc.left + 2, rc.top, rc.right - 2, rc.top + 1 };
        fillRect(dis->hDC, topLine, t.textBgLighter);

        const int left = rc.left + (active ? 12 : 7);
        RECT timeArea{ left, rc.top + 10, left + 100, rc.bottom - 10 };
        RECT badge{ timeArea.left + 8, timeArea.top + (timeArea.bottom - timeArea.top - 28) / 2,
            timeArea.right - 8, timeArea.top + (timeArea.bottom - timeArea.top - 28) / 2 + 28 };
        fillRoundRect(dis->hDC, badge, t.textBg, 8);
        drawText(dis->hDC, relativeTime(item.updated), badge, t.text, smallFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT divider{ timeArea.right + 2, rc.top + 12, timeArea.right + 4, rc.bottom - 12 };
        fillRect(dis->hDC, divider, t.textBgLighter);

        const int actionReserve = active && !multiMode ? 190 : 62;
        RECT content{ timeArea.right + 14, rc.top + 9, rc.right - actionReserve, rc.bottom - 9 };
        if (item.type == ItemType::Image) {
            RECT imageRc = content;
            imageRc.right = std::min(imageRc.right, imageRc.left + 460);
            drawDibFit(dis->hDC, item, imageRc);
        }
        else {
            bool oversized = false;
            auto preview = itemPreview(item, oversized);
            drawText(dis->hDC, preview, content, oversized ? t.primary : t.text, uiFont,
                DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
        }

        if (active && !multiMode) {
            RECT acts[4]{}; actionRects(dis->hwndItem, rc, acts);
            drawActionButton(dis->hDC, acts[0], L"📄", t);
            drawActionButton(dis->hDC, acts[1], L"💬", t);
            drawActionButton(dis->hDC, acts[2], item.favorite ? L"★" : L"☆", t);
            drawActionButton(dis->hDC, acts[3], L"✕", t);
        }
        else {
            RECT count{ rc.right - 55, rc.top, rc.right - 8, rc.bottom };
            std::wstring s = std::to_wstring(dis->itemID + 1);
            if (multiSelected) s = L"✓ " + s;
            drawText(dis->hDC, s, count, multiSelected ? t.primary : t.textLighter,
                smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void setTab(Tab tab)
    {
        activeTab = tab;
        multiMode = false; multiHashes.clear();
        if (searchWnd) ShowWindow(searchWnd, SW_SHOW);
        refreshList();
    }

    inline RECT tabRect(int index)
    {
        const int x = 10 + index * 88;
        return { x, 10, x + 83, 56 };
    }

    inline void drawTopBar(HDC dc, RECT client)
    {
        const Theme t = theme();
        RECT nav{ 0,0,client.right,66 };
        fillRect(dc, nav, t.navBg);
        const wchar_t* labels[5] = { L"☰  全部", L"▤  文字", L"▧  图片", L"▱  文件", L"☆  收藏" };
        for (int i = 0; i < 5; ++i) {
            RECT r = tabRect(i);
            bool active = (int)activeTab == i;
            if (active) fillRoundRect(dc, r, t.bg, 8);
            drawText(dc, labels[i], r, active ? t.primary : t.text, uiFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        if (multiMode) {
            RECT count{ client.right - 330, 13, client.right - 275, 53 };
            fillRoundRect(dc, count, t.primary, 8);
            drawText(dc, std::to_wstring(multiHashes.size()), count, t.bg, boldFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT copy{ client.right - 268, 13, client.right - 198, 53 };
            RECT paste{ client.right - 190, 13, client.right - 120, 53 };
            RECT exit{ client.right - 112, 13, client.right - 18, 53 };
            for (auto r : { copy,paste,exit }) fillRoundRect(dc, r, t.navHover, 8);
            drawText(dc, L"复制", copy, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"粘贴", paste, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"退出多选", exit, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void ensureFonts(HWND hwnd)
    {
        if (uiFont) return;
        UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
        auto px = [dpi](int pt) { return -MulDiv(pt, dpi, 72); };
        uiFont = CreateFontW(px(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        smallFont = CreateFontW(px(9), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        boldFont = CreateFontW(px(10), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        emojiFont = CreateFontW(px(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Emoji");
    }

    inline void layoutHistory(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (listWnd) MoveWindow(listWnd, 0, 66, w, std::max(1, h - 66), TRUE);
        if (searchWnd) {
            int sw = std::clamp(w - 470, 230, 330);
            MoveWindow(searchWnd, w - sw - 18, 15, sw, 36, TRUE);
        }
        if (clearWnd) MoveWindow(clearWnd, std::max(0, w - 66), std::max(66, h - 66), 50, 50, TRUE);
        if (fullWnd && IsWindowVisible(fullWnd)) MoveWindow(fullWnd, 0, 0, w, h, TRUE);
    }

    inline Item* findByHash(uint64_t hash)
    {
        for (auto& i : items) if (i.hash == hash) return &i;
        return nullptr;
    }

    inline void showFull(int idx)
    {
        Item* item = itemAtListIndex(idx);
        if (!item || !historyWnd) return;
        fullItemHash = item->hash;
        if (!fullWnd) {
            fullWnd = CreateWindowExW(0, L"WeShotClipboardFullView", L"", WS_CHILD,
                0,0,0,0, historyWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        }
        if (!fullWnd) return;
        RECT rc{}; GetClientRect(historyWnd, &rc);
        MoveWindow(fullWnd, 0, 0, rc.right, rc.bottom, TRUE);
        ShowWindow(fullWnd, SW_SHOW);
        BringWindowToTop(fullWnd);
        SetFocus(fullWnd);
        InvalidateRect(fullWnd, nullptr, TRUE);
    }

    inline void hideFull()
    {
        if (fullWnd) ShowWindow(fullWnd, SW_HIDE);
        if (listWnd) SetFocus(listWnd);
    }

    inline bool pointIn(const RECT& r, POINT p)
    {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    }

    inline bool restoreByHash(uint64_t hash, bool paste)
    {
        Item* item = findByHash(hash);
        if (!item || !restoreItem(*item)) return false;
        item->updated = nowTicks(); saveStore();
        if (paste) pasteToPrevious();
        return true;
    }

    inline void toggleMultiCurrent()
    {
        int idx = currentListIndex();
        Item* item = itemAtListIndex(idx);
        if (!item) return;
        multiMode = true;
        if (multiHashes.contains(item->hash)) multiHashes.erase(item->hash);
        else multiHashes.insert(item->hash);
        if (searchWnd) ShowWindow(searchWnd, SW_HIDE);
        if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE);
        if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
    }

    inline bool writeUnicodeClipboardText(const std::wstring& text)
    {
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!h) return false;
        void* p = GlobalLock(h);
        if (!p) { GlobalFree(h); return false; }
        memcpy(p, text.c_str(), bytes); GlobalUnlock(h);
        if (!OpenClipboard(historyWnd)) { GlobalFree(h); return false; }
        EmptyClipboard(); suppressNext = true;
        if (!SetClipboardData(CF_UNICODETEXT, h)) {
            suppressNext = false; GlobalFree(h); CloseClipboard(); return false;
        }
        CloseClipboard(); return true;
    }

    inline void copyMulti(bool paste)
    {
        if (multiHashes.empty()) return;
        std::vector<const Item*> selected;
        for (auto it = items.rbegin(); it != items.rend(); ++it)
            if (multiHashes.contains(it->hash)) selected.push_back(&*it);
        if (selected.empty()) return;
        if (selected.size() == 1) {
            if (restoreItem(*selected[0]) && paste) pasteToPrevious();
            return;
        }
        bool allText = std::all_of(selected.begin(), selected.end(), [](const Item* i) { return i->type == ItemType::Text; });
        if (!allText) {
            MessageBoxW(historyWnd,
                L"当前预览版已经完整复刻多选界面与选择逻辑。多图片/文件混合后自动生成临时文件的 uTools 行为会在下一轮接上；这次先避免为了演示效果写出不稳定的临时文件逻辑。",
                L"WeShot 剪贴板", MB_OK | MB_ICONINFORMATION);
            return;
        }
        std::wstring merged;
        for (auto* i : selected) {
            if (!merged.empty()) merged += L"\r\n";
            merged += textFromData(i->data);
        }
        if (writeUnicodeClipboardText(merged) && paste) pasteToPrevious();
    }

    inline void captureCurrent()
    {
        if (suppressNext) { suppressNext = false; return; }
        if (!OpenClipboard(listenerWnd)) return;

        // Match the public uTools ClipboardManager priority: file -> text -> image.
        UINT format = 0;
        if (IsClipboardFormatAvailable(CF_HDROP)) format = CF_HDROP;
        else if (IsClipboardFormatAvailable(CF_UNICODETEXT)) format = CF_UNICODETEXT;
        else if (IsClipboardFormatAvailable(CF_DIBV5)) format = CF_DIBV5;
        else if (IsClipboardFormatAvailable(CF_DIB)) format = CF_DIB;

        std::vector<BYTE> bytes;
        if (format) {
            HANDLE h = GetClipboardData(format);
            if (h) {
                SIZE_T sz = GlobalSize(h);
                if (sz > 0 && sz <= MAX_ITEM_BYTES) {
                    void* p = GlobalLock(h);
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

        uint64_t h = hashBytes(format, bytes), now = nowTicks();
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].hash == h && items[i].format == format && items[i].data == bytes) {
                Item moved = std::move(items[i]);
                moved.updated = now;
                items.erase(items.begin() + (ptrdiff_t)i);
                items.insert(items.begin(), std::move(moved));
                saveStore(); refreshList(); return;
            }
        }

        Item item;
        item.format = format; item.type = typeForFormat(format); item.data = std::move(bytes);
        item.created = item.updated = now; item.hash = h;
        items.insert(items.begin(), std::move(item));
        trim(); saveStore(); refreshList();
    }

    inline LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_MOUSEMOVE:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (!HIWORD(hit) && idx >= 0 && idx < (int)visibleItems.size()) {
                if ((int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0) != idx) {
                    SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (HIWORD(hit) || idx < 0 || idx >= (int)visibleItems.size()) break;
            SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
            if (multiMode) { toggleMultiCurrent(); return 0; }
            RECT itemRc{}; SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc);
            RECT acts[4]{}; actionRects(hwnd, itemRc, acts);
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pointIn(acts[0], p)) { useListItem(idx, false); return 0; }
            if (pointIn(acts[1], p)) { showFull(idx); return 0; }
            if (pointIn(acts[2], p)) { toggleFavorite(idx); return 0; }
            if (pointIn(acts[3], p)) { deleteListItem(idx); return 0; }
            useListItem(idx, true); return 0;
        }
        case WM_RBUTTONUP:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (!HIWORD(hit)) useListItem(idx, false);
            return 0;
        }
        case WM_KEYDOWN:
        {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (wParam == VK_ESCAPE) { if (historyWnd) ShowWindow(historyWnd, SW_HIDE); return 0; }
            if (wParam == VK_RETURN) { useListItem(currentListIndex(), true); return 0; }
            if (wParam == VK_DELETE) { deleteListItem(currentListIndex()); return 0; }
            if (wParam == VK_SPACE) { toggleMultiCurrent(); return 0; }
            if (wParam == VK_TAB) { setTab((Tab)(((int)activeTab + 1) % 5)); return 0; }
            if (ctrl && (wParam == 'F' || wParam == 'L')) { if (searchWnd) SetFocus(searchWnd); return 0; }
            if (ctrl && wParam == 'C') { useListItem(currentListIndex(), false); return 0; }
            if (ctrl && (wParam == 'J' || wParam == 'K')) {
                int idx = currentListIndex();
                idx += wParam == 'J' ? 1 : -1;
                idx = std::clamp(idx, 0, std::max(0, (int)visibleItems.size() - 1));
                SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                SendMessageW(hwnd, LB_SETTOPINDEX, std::max(0, idx - 3), 0);
                return 0;
            }
            if ((ctrl || alt) && wParam >= '1' && wParam <= '9') {
                int idx = (int)(wParam - '1');
                if (idx < (int)visibleItems.size()) useListItem(idx, true);
                return 0;
            }
            if (wParam == VK_SHIFT) {
                multiMode = true;
                if (searchWnd) ShowWindow(searchWnd, SW_HIDE);
                if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE);
                return 0;
            }
            break;
        }
        }
        return oldListProc ? CallWindowProcW(oldListProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline LRESULT CALLBACK fullProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) { hideFull(); return 0; }
            if (wParam == VK_RETURN) { restoreByHash(fullItemHash, true); return 0; }
            break;
        case WM_LBUTTONDOWN:
        {
            RECT rc{}; GetClientRect(hwnd, &rc);
            int panelW = (int)(rc.right * .80);
            RECT panel{ 0,0,panelW,rc.bottom };
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!pointIn(panel, p)) { hideFull(); return 0; }
            RECT copy{ panelW - 205,15,panelW - 160,55 };
            RECT fav{ panelW - 150,15,panelW - 105,55 };
            RECT del{ panelW - 95,15,panelW - 50,55 };
            RECT close{ panelW - 42,15,panelW - 8,55 };
            if (pointIn(copy, p)) { restoreByHash(fullItemHash, false); return 0; }
            if (pointIn(fav, p)) {
                if (auto* item = findByHash(fullItemHash)) { item->favorite = !item->favorite; saveStore(); refreshList(); }
                InvalidateRect(hwnd, nullptr, TRUE); return 0;
            }
            if (pointIn(del, p)) {
                for (size_t i = 0; i < items.size(); ++i) if (items[i].hash == fullItemHash) {
                    items.erase(items.begin() + (ptrdiff_t)i); break;
                }
                saveStore(); refreshList(); hideFull(); return 0;
            }
            if (pointIn(close, p)) { hideFull(); return 0; }
            break;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            Theme t = theme();
            fillRect(dc, rc, RGB(0x55,0x55,0x55));
            int panelW = (int)(rc.right * .80);
            RECT panel{ 0,0,panelW,rc.bottom };
            fillRect(dc, panel, t.bg);
            auto* item = findByHash(fullItemHash);
            if (item) {
                RECT copy{ panelW - 205,15,panelW - 160,55 };
                RECT fav{ panelW - 150,15,panelW - 105,55 };
                RECT del{ panelW - 95,15,panelW - 50,55 };
                RECT close{ panelW - 42,15,panelW - 8,55 };
                for (auto r : { copy,fav,del,close }) fillRoundRect(dc, r, t.textBg, 8);
                drawText(dc, L"📄", copy, t.primary, emojiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                drawText(dc, item->favorite ? L"★" : L"☆", fav, t.primary, emojiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                drawText(dc, L"✕", del, t.primary, emojiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                drawText(dc, L"×", close, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                RECT content{ 20,70,panelW - 20,rc.bottom - 20 };
                fillRoundRect(dc, content, t.textBg, 8);
                RECT inner{ content.left + 20, content.top + 20, content.right - 20, content.bottom - 20 };
                if (item->type == ItemType::Image) drawDibFit(dc, *item, inner);
                else {
                    std::wstring s = item->type == ItemType::Text ? textFromData(item->data) : filePreview(item->data, 1000);
                    drawText(dc, s, inner, t.text, uiFont, DT_LEFT | DT_TOP | DT_WORDBREAK);
                }
            }
            EndPaint(hwnd, &ps); return 0;
        }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void clearAll()
    {
        if (MessageBoxW(historyWnd, L"即将清空剪贴板记录（包括收藏内容）。", L"WeShot 剪贴板",
            MB_OKCANCEL | MB_ICONWARNING) != IDOK) return;
        items.clear(); visibleItems.clear(); multiHashes.clear(); multiMode = false; totalBytes = 0;
        saveStore(); refreshList();
    }

    inline LRESULT CALLBACK historyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_CREATE:
        {
            historyWnd = hwnd; ensureFonts(hwnd);
            Theme t = theme();
            editBrush = CreateSolidBrush(t.bg);
            listWnd = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_LIST), GetModuleHandleW(nullptr), nullptr);
            searchWnd = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_SEARCH), GetModuleHandleW(nullptr), nullptr);
            clearWnd = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_CLEAR_FLOAT), GetModuleHandleW(nullptr), nullptr);
            if (listWnd) {
                SendMessageW(listWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                oldListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(listWnd, GWLP_WNDPROC, (LONG_PTR)listProc));
            }
            if (searchWnd) {
                SendMessageW(searchWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                SendMessageW(searchWnd, EM_SETCUEBANNER, TRUE, (LPARAM)L"🔍 检索剪贴板历史...");
            }
            refreshList(); layoutHistory(hwnd); return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto p = reinterpret_cast<MINMAXINFO*>(lParam);
            p->ptMinTrackSize.x = 760; p->ptMinTrackSize.y = 480; return 0;
        }
        case WM_SIZE: layoutHistory(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_CTLCOLOREDIT:
        {
            HDC dc = (HDC)wParam; Theme t = theme();
            SetTextColor(dc, t.text); SetBkColor(dc, t.bg);
            return (LRESULT)editBrush;
        }
        case WM_MEASUREITEM:
        {
            auto mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mi && mi->CtlID == ID_LIST) {
                if (mi->itemID < visibleItems.size() && items[visibleItems[mi->itemID]].type == ItemType::Image)
                    mi->itemHeight = 128;
                else mi->itemHeight = 94;
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
        {
            auto dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!dis) break;
            if (dis->CtlID == ID_LIST) { drawListItem(dis); return TRUE; }
            if (dis->CtlID == ID_CLEAR_FLOAT) {
                Theme t = theme();
                fillRect(dis->hDC, dis->rcItem, t.bg);
                RECT r = dis->rcItem; InflateRect(&r, -2, -2);
                fillRoundRect(dis->hDC, r, (dis->itemState & ODS_SELECTED) ? t.primary : t.textBgLighter, 46);
                drawText(dis->hDC, L"🧭", r, (dis->itemState & ODS_SELECTED) ? t.bg : t.text,
                    emojiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
        {
            UINT id = LOWORD(wParam), code = HIWORD(wParam);
            if (id == ID_SEARCH && code == EN_CHANGE) { refreshList(); return 0; }
            if (id == ID_CLEAR_FLOAT && code == BN_CLICKED) { clearAll(); return 0; }
            break;
        }
        case WM_LBUTTONDOWN:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (p.y < 66) {
                for (int i = 0; i < 5; ++i) if (pointIn(tabRect(i), p)) { setTab((Tab)i); return 0; }
                if (multiMode) {
                    RECT rc{}; GetClientRect(hwnd, &rc);
                    RECT copy{ rc.right - 268,13,rc.right - 198,53 };
                    RECT paste{ rc.right - 190,13,rc.right - 120,53 };
                    RECT exit{ rc.right - 112,13,rc.right - 18,53 };
                    if (pointIn(copy, p)) { copyMulti(false); return 0; }
                    if (pointIn(paste, p)) { copyMulti(true); return 0; }
                    if (pointIn(exit, p)) {
                        multiMode = false; multiHashes.clear(); if (searchWnd) ShowWindow(searchWnd, SW_SHOW);
                        InvalidateRect(hwnd, nullptr, TRUE); InvalidateRect(listWnd, nullptr, TRUE); return 0;
                    }
                }
            }
            break;
        }
        case WM_SETTINGCHANGE:
            if (editBrush) { DeleteObject(editBrush); editBrush = CreateSolidBrush(theme().bg); }
            InvalidateRect(hwnd, nullptr, TRUE); if (listWnd) InvalidateRect(listWnd, nullptr, TRUE); return 0;
        case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_DESTROY:
            historyWnd = nullptr; listWnd = nullptr; searchWnd = nullptr; clearWnd = nullptr; fullWnd = nullptr; oldListProc = nullptr;
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            fillRect(dc, rc, theme().bg); drawTopBar(dc, rc);
            EndPaint(hwnd, &ps); return 0;
        }
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
        loadStore();
        const auto hInst = GetModuleHandleW(nullptr);
        WNDCLASSW lc{}; lc.lpfnWndProc = listenerProc; lc.hInstance = hInst; lc.lpszClassName = L"WeShotClipboardListener";
        RegisterClassW(&lc);
        WNDCLASSW hc{}; hc.lpfnWndProc = historyProc; hc.hInstance = hInst; hc.lpszClassName = L"WeShotClipboardHistory";
        hc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&hc);
        WNDCLASSW fc{}; fc.lpfnWndProc = fullProc; fc.hInstance = hInst; fc.lpszClassName = L"WeShotClipboardFullView";
        fc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&fc);
        listenerWnd = CreateWindowExW(0, lc.lpszClassName, L"", 0, 0,0,0,0, HWND_MESSAGE, nullptr, hInst, nullptr);
        if (listenerWnd) { AddClipboardFormatListener(listenerWnd); captureCurrent(); }
    }

    inline void show()
    {
        const auto hInst = GetModuleHandleW(nullptr);
        HWND fg = GetForegroundWindow();
        if (fg && fg != historyWnd && fg != listWnd && fg != searchWnd && fg != fullWnd) lastForeground = fg;
        if (!historyWnd) {
            historyWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"WeShotClipboardHistory", L"WeShot 剪贴板",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 880, 620,
                nullptr, nullptr, hInst, nullptr);
        }
        if (!historyWnd) return;
        refreshList(); ShowWindow(historyWnd, SW_SHOW); SetForegroundWindow(historyWnd);
        if (listWnd) SetFocus(listWnd);
    }

    inline void toggle()
    {
        if (historyWnd && IsWindow(historyWnd) && IsWindowVisible(historyWnd)) {
            ShowWindow(historyWnd, SW_HIDE); return;
        }
        show();
    }

    inline void dispose()
    {
        saveStore();
        if (listenerWnd) {
            RemoveClipboardFormatListener(listenerWnd); DestroyWindow(listenerWnd); listenerWnd = nullptr;
        }
        if (historyWnd) { DestroyWindow(historyWnd); historyWnd = nullptr; }
        if (uiFont) { DeleteObject(uiFont); uiFont = nullptr; }
        if (smallFont) { DeleteObject(smallFont); smallFont = nullptr; }
        if (boldFont) { DeleteObject(boldFont); boldFont = nullptr; }
        if (emojiFont) { DeleteObject(emojiFont); emojiFont = nullptr; }
        if (editBrush) { DeleteObject(editBrush); editBrush = nullptr; }
        items.clear(); visibleItems.clear(); multiHashes.clear(); totalBytes = 0; suppressNext = false;
    }
}
