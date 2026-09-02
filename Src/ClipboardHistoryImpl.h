#pragma once

// WeShot clipboard manager v0.9.6
// UI/interaction model is intentionally close to the public Apache-2.0
// ZiuChen/ClipboardManager project so we can evaluate that proven workflow in
// a native Win32 implementation before doing WeShot-specific refinements.

#include <windows.h>
#include <windowsx.h>
#include <cstdlib>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>
#include "Win/WinSetting.h"

namespace ClipboardHistory
{
    enum class ItemType : uint8_t { Text = 1, Image = 2, File = 3 };
    enum class Tab : uint8_t { All = 0, Text = 1, Image = 2, File = 3, Favorite = 4 };
    enum class ThemeMode : uint8_t { System = 0, Light = 1, Dark = 2 };

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
    inline WNDPROC oldSearchProc{ nullptr };
    inline bool sidePinned{ false };
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
    inline ThemeMode themeMode{ ThemeMode::System };
    inline int multiAnchor{ -1 };
    inline int hoverIndex{ -1 };
    inline int hoverAction{ -1 };
    inline double fullImageZoom{ 1.0 };

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
    inline LRESULT CALLBACK searchProc(HWND, UINT, WPARAM, LPARAM);
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

    inline std::filesystem::path storagePath();

    inline std::filesystem::path themeModePath()
    {
        auto p = storagePath();
        p.replace_filename(L"clipboard_theme.txt");
        return p;
    }

    inline void loadThemeMode()
    {
        std::ifstream f(themeModePath());
        int v = 0;
        if (f >> v) {
            if (v >= 0 && v <= 2) themeMode = (ThemeMode)v;
        }
    }

    inline void saveThemeMode()
    {
        std::ofstream f(themeModePath(), std::ios::trunc);
        if (f) f << (int)themeMode;
    }

    inline bool useDarkTheme()
    {
        if (themeMode == ThemeMode::Dark) return true;
        if (themeMode == ThemeMode::Light) return false;
        return isDarkMode();
    }

    inline const wchar_t* themeModeLabel()
    {
        if (themeMode == ThemeMode::Dark) return L"深色";
        if (themeMode == ThemeMode::Light) return L"浅色";
        return L"自动";
    }

    inline Theme theme()
    {
        if (useDarkTheme()) {
            return { RGB(0x90,0xca,0xf9), RGB(0x58,0x70,0xff), RGB(0xf8,0xf8,0xf8),
                RGB(0xb5,0xb5,0xb5), RGB(0x50,0x50,0x50), RGB(0x4a,0x4a,0x4a),
                RGB(0x30,0x31,0x33), RGB(0x4a,0x4a,0x4a), RGB(0x21,0x21,0x21) };
        }
        return { RGB(0x58,0x70,0xff), RGB(0x90,0xca,0xf9), RGB(0x21,0x21,0x21),
            RGB(0x70,0x70,0x70), RGB(0xf4,0xf4,0xf4), RGB(0xd0,0xd0,0xd0),
            RGB(0xf4,0xf4,0xf4), RGB(0xdb,0xdb,0xdb), RGB(0xff,0xff,0xff) };
    }

    inline COLORREF selectionBg()
    {
        return useDarkTheme() ? RGB(0x26,0x2f,0x37) : RGB(0xe3,0xf2,0xfd);
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

    inline void strokeRoundRect(HDC dc, const RECT& rc, COLORREF color, int width = 1, int radius = 8)
    {
        HPEN p = CreatePen(PS_SOLID, width, color);
        auto oldP = SelectObject(dc, p);
        auto oldB = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(p);
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
        const std::filesystem::path base = (n > 0 && n < std::size(buf))
            ? std::filesystem::path(buf)
            : std::filesystem::temp_directory_path();

        auto root = base / L"StarCap";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);

        const auto legacyName = std::wstring(L"We") + L"Shot";
        const auto legacyRoot = base / legacyName;
        if (std::filesystem::is_directory(legacyRoot)) {
            ec.clear();
            std::filesystem::copy(legacyRoot, root,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
        }
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
        std::wstring s((size_t)n + 1, L'\0');
        if (n) GetWindowTextW(searchWnd, s.data(), n + 1);
        s.resize((size_t)n);
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

    inline std::wstring readableBytes(size_t bytes)
    {
        if (bytes >= 1024ull * 1024ull)
            return std::format(L"{:.1f} MB", (double)bytes / (1024.0 * 1024.0));
        if (bytes >= 1024ull)
            return std::format(L"{:.0f} KB", (double)bytes / 1024.0);
        return std::format(L"{} B", bytes);
    }

    inline std::wstring imageMeta(const Item& item)
    {
        if (item.data.size() < sizeof(BITMAPINFOHEADER)) return readableBytes(item.data.size());
        auto bi = reinterpret_cast<const BITMAPINFOHEADER*>(item.data.data());
        return std::format(L"{} × {}   ·   {}", std::abs(bi->biWidth), std::abs(bi->biHeight), readableBytes(item.data.size()));
    }

    inline void drawDibZoom(HDC dc, const Item& item, RECT rc, double zoom)
    {
        if (item.data.size() < sizeof(BITMAPINFOHEADER)) return;
        auto bi = reinterpret_cast<const BITMAPINFOHEADER*>(item.data.data());
        if (bi->biWidth == 0 || bi->biHeight == 0) return;
        size_t off = dibBitsOffset(bi);
        if (off >= item.data.size()) return;
        int sw = std::abs(bi->biWidth), sh = std::abs(bi->biHeight);
        int aw = std::max(1, (int)(rc.right - rc.left)), ah = std::max(1, (int)(rc.bottom - rc.top));
        double fit = std::min((double)aw / sw, (double)ah / sh);
        double scale = fit * std::clamp(zoom, 1.0, 4.0);
        int dw = std::max(1, (int)std::lround(sw * scale));
        int dh = std::max(1, (int)std::lround(sh * scale));
        int x = rc.left + (aw - dw) / 2, y = rc.top + (ah - dh) / 2;
        int saved = SaveDC(dc);
        IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, x, y, dw, dh, 0, 0, sw, sh,
            item.data.data() + off, reinterpret_cast<const BITMAPINFO*>(bi), DIB_RGB_COLORS, SRCCOPY);
        RestoreDC(dc, saved);
    }

    inline void drawDibFit(HDC dc, const Item& item, RECT rc)
    {
        drawDibZoom(dc, item, rc, 1.0);
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
        int y = (int)itemRc.top + std::max(4, ((int)(itemRc.bottom - itemRc.top) - bw) / 2);
        for (int i = 0; i < 4; ++i) {
            out[i] = { x, y, x + bw, y + bw };
            x += bw + gap;
        }
    }

    inline void drawActionButton(HDC dc, RECT rc, const std::wstring& glyph, const Theme& t, bool hovered)
    {
        fillRoundRect(dc, rc, hovered ? t.primary : t.textBg, 9);
        if (!hovered) strokeRoundRect(dc, rc, t.textBgLighter, 1, 9);
        drawText(dc, glyph, rc, hovered ? t.bg : t.primary, emojiFont ? emojiFont : uiFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void drawHoverHint(HDC dc, const RECT& anchor, const wchar_t* text, const Theme& t)
    {
        int width = 72;
        RECT tip{ anchor.left - (width - (anchor.right - anchor.left)) / 2,
            anchor.top - 28, anchor.left - (width - (anchor.right - anchor.left)) / 2 + width, anchor.top - 5 };
        fillRoundRect(dc, tip, t.text, 7);
        drawText(dc, text, tip, t.bg, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline RECT expandRect(const RECT& itemRc)
    {
        int center = (itemRc.left + itemRc.right) / 2;
        return { center - 58, itemRc.bottom - 30, center + 58, itemRc.bottom - 5 };
    }

    inline void drawListItem(const DRAWITEMSTRUCT* dis)
    {
        if (!dis || dis->itemID == (UINT)-1 || dis->itemID >= visibleItems.size()) return;
        const Theme t = theme();
        const Item& item = items[visibleItems[dis->itemID]];
        RECT rc = dis->rcItem;
        const bool active = (dis->itemState & ODS_SELECTED) != 0;
        const bool multiSelected = multiHashes.contains(item.hash);
        const bool hovered = ((int)dis->itemID == hoverIndex);

        fillRect(dis->hDC, rc, t.bg);
        RECT card = rc; InflateRect(&card, -2, 0);
        if (hovered || active || multiSelected) fillRect(dis->hDC, card, selectionBg());

        if (active || multiSelected) {
            strokeRoundRect(dis->hDC, card, t.primary, 2, 3);
        }
        else {
            RECT line{ card.left + 24, card.bottom - 1, card.right - 12, card.bottom };
            fillRect(dis->hDC, line, t.textBgLighter);
        }

        RECT body{ card.left + 36, card.top + 8, card.right - 30, card.bottom - 31 };
        bool oversized = false;
        if (item.type == ItemType::Image) {
            RECT imageRc = body;
            imageRc.left += 28; imageRc.right -= 28;
            drawDibFit(dis->hDC, item, imageRc);
        }
        else {
            auto preview = itemPreview(item, oversized);
            drawText(dis->hDC, preview, body, t.text, uiFont,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        }

        RECT timeRc{ card.left + 36, card.bottom - 29, card.left + 180, card.bottom - 4 };
        drawText(dis->hDC, relativeTime(item.updated), timeRc, t.textLighter, smallFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        bool expandable = item.type == ItemType::Image || item.type == ItemType::File || oversized;
        if (expandable) {
            RECT er = expandRect(card);
            drawText(dis->hDC, L"⌄  展开", er, t.textLighter, smallFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        std::wstring meta;
        if (item.type == ItemType::Image) meta = imageMeta(item);
        else if (item.type == ItemType::File) meta = std::format(L"{} 个文件", filePaths(item.data).size());
        else meta = std::format(L"{} 字符", textFromData(item.data).size());

        RECT metaRc{ card.right - 250, card.bottom - 29, card.right - 60, card.bottom - 4 };
        drawText(dis->hDC, meta, metaRc, t.textLighter, smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT indexRc{ card.right - 54, card.bottom - 29, card.right - 14, card.bottom - 4 };
        std::wstring index = std::to_wstring(dis->itemID + 1);
        if (multiSelected) index = L"✓ " + index;
        drawText(dis->hDC, index, indexRc, multiSelected ? t.primary : t.textLighter, smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        if (item.favorite) {
            RECT favRc{ card.right - 52, card.top + 7, card.right - 16, card.top + 32 };
            drawText(dis->hDC, L"★", favRc, t.primary, uiFont,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void setTab(Tab tab)
    {
        activeTab = tab;
        multiMode = false; multiHashes.clear();
        if (searchWnd) ShowWindow(searchWnd, SW_SHOW);
        refreshList();
    }

    inline int sideRailWidth() { return 72; }

    inline RECT sideRailButtonRect(const RECT& client, int index)
    {
        const int railLeft = client.right - sideRailWidth();
        const int cx = railLeft + sideRailWidth() / 2;
        if (index < 3) {
            const int cy = 41 + index * 78;
            return { cx - 26, cy - 26, cx + 26, cy + 26 };
        }
        const int cy = client.bottom - 173 + (index - 3) * 78;
        return { cx - 24, cy - 24, cx + 24, cy + 24 };
    }

    inline void drawRailGlyph(HDC dc, int index, const RECT& r, COLORREF color)
    {
        HPEN pen = CreatePen(PS_SOLID, 2, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        const int cx = (r.left + r.right) / 2;
        const int cy = (r.top + r.bottom) / 2;

        if (index == 0) {
            HPEN dot = CreatePen(PS_DOT, 1, color);
            SelectObject(dc, dot);
            Rectangle(dc, cx - 13, cy - 12, cx + 13, cy + 12);
            SelectObject(dc, pen);
            RoundRect(dc, cx - 6, cy - 8, cx + 6, cy + 8, 3, 3);
            MoveToEx(dc, cx - 2, cy + 5, nullptr); LineTo(dc, cx + 2, cy + 5);
            DeleteObject(dot);
        }
        else if (index == 1) {
            POINT p[3] = { {cx - 10, cy - 10}, {cx + 12, cy}, {cx - 10, cy + 10} };
            HBRUSH b = CreateSolidBrush(color);
            SelectObject(dc, b); Polygon(dc, p, 3);
            SelectObject(dc, oldBrush); DeleteObject(b);
        }
        else if (index == 2) {
            Rectangle(dc, cx - 6, cy - 10, cx + 6, cy - 4);
            MoveToEx(dc, cx - 4, cy - 4, nullptr); LineTo(dc, cx - 4, cy + 3);
            MoveToEx(dc, cx + 4, cy - 4, nullptr); LineTo(dc, cx + 4, cy + 3);
            MoveToEx(dc, cx - 9, cy + 3, nullptr); LineTo(dc, cx + 9, cy + 3);
            MoveToEx(dc, cx, cy + 3, nullptr); LineTo(dc, cx, cy + 12);
        }
        else if (index == 3) {
            Rectangle(dc, cx - 10, cy - 8, cx + 7, cy + 9);
            MoveToEx(dc, cx - 1, cy + 1, nullptr); LineTo(dc, cx + 11, cy - 11);
            MoveToEx(dc, cx + 4, cy - 11, nullptr); LineTo(dc, cx + 11, cy - 11);
            LineTo(dc, cx + 11, cy - 4);
        }
        else if (index == 4) {
            Ellipse(dc, cx - 7, cy - 7, cx + 7, cy + 7);
            Ellipse(dc, cx - 2, cy - 2, cx + 2, cy + 2);
            const POINT a[8] = {
                {cx,cy-12},{cx+8,cy-8},{cx+12,cy},{cx+8,cy+8},
                {cx,cy+12},{cx-8,cy+8},{cx-12,cy},{cx-8,cy-8}
            };
            const POINT b[8] = {
                {cx,cy-8},{cx+6,cy-6},{cx+8,cy},{cx+6,cy+6},
                {cx,cy+8},{cx-6,cy+6},{cx-8,cy},{cx-6,cy-6}
            };
            for (int i = 0; i < 8; ++i) {
                MoveToEx(dc, b[i].x, b[i].y, nullptr); LineTo(dc, a[i].x, a[i].y);
            }
        }
        else if (index == 5) {
            MoveToEx(dc, cx + 7, cy - 12, nullptr); LineTo(dc, cx - 3, cy + 3);
            MoveToEx(dc, cx - 8, cy + 2, nullptr); LineTo(dc, cx + 2, cy + 9);
            MoveToEx(dc, cx - 10, cy + 5, nullptr); LineTo(dc, cx, cy + 12);
            MoveToEx(dc, cx - 4, cy + 1, nullptr); LineTo(dc, cx + 5, cy + 8);
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    inline void drawSideRail(HDC dc, const RECT& client)
    {
        const bool dark = useDarkTheme();
        const int railLeft = client.right - sideRailWidth();
        RECT rail{ railLeft, 0, client.right, client.bottom };
        fillRect(dc, rail, dark ? RGB(0x30,0x31,0x33) : RGB(0xf4,0xf4,0xf4));

        const COLORREF circle = dark ? RGB(0x90,0xca,0xf9) : RGB(0x58,0x70,0xff);
        const COLORREF circleGlyph = dark ? RGB(0x16,0x2b,0x3a) : RGB(0xff,0xff,0xff);
        const COLORREF lowerGlyph = dark ? RGB(0xf2,0xf2,0xf2) : RGB(0x70,0x70,0x70);

        for (int i = 0; i < 3; ++i) {
            RECT r = sideRailButtonRect(client, i);
            HBRUSH b = CreateSolidBrush(circle);
            HGDIOBJ oldB = SelectObject(dc, b);
            HGDIOBJ oldP = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, oldP); SelectObject(dc, oldB); DeleteObject(b);
            drawRailGlyph(dc, i, r, circleGlyph);
        }
        for (int i = 3; i < 6; ++i) {
            drawRailGlyph(dc, i, sideRailButtonRect(client, i), lowerGlyph);
        }
    }
    inline RECT tabRect(int index)
    {
        RECT cr{};
        if (historyWnd) GetClientRect(historyWnd, &cr);
        int w = std::max(520, (int)(cr.right - cr.left) - sideRailWidth());
        int tabW = std::clamp((w - 10) / 5, 92, 170);
        int x = 5 + index * tabW;
        return { x, 58, x + tabW, 106 };
    }

    inline RECT themeButtonRect(const RECT& client)
    {
        return { std::max(10, (int)client.right - 132), 12, std::max(120, (int)client.right - 12), 49 };
    }

    inline void applyThemeNow()
    {
        Theme t = theme();
        if (editBrush) DeleteObject(editBrush);
        editBrush = CreateSolidBrush(t.navBg);
        if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE);
        if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
        if (searchWnd) RedrawWindow(searchWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        if (fullWnd && IsWindowVisible(fullWnd)) InvalidateRect(fullWnd, nullptr, TRUE);
    }

    inline void cycleThemeMode()
    {
        themeMode = themeMode == ThemeMode::System ? ThemeMode::Light
            : (themeMode == ThemeMode::Light ? ThemeMode::Dark : ThemeMode::System);
        saveThemeMode();
        applyThemeNow();
    }

    inline void drawTopBar(HDC dc, RECT client)
    {
        const Theme t = theme();
        RECT top{ 0,0,client.right,58 };
        fillRect(dc, top, t.navBg);
        RECT tabs{ 0,58,client.right,106 };
        fillRect(dc, tabs, t.navBg);

        RECT themeRc = themeButtonRect(client);
        fillRoundRect(dc, themeRc, t.navHover, 6);
        std::wstring themeText = std::format(L"◐  {}", themeModeLabel());
        drawText(dc, themeText, themeRc, t.primary, uiFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        size_t favCount = 0;
        for (const auto& it : items) if (it.favorite) ++favCount;
        std::wstring labels[5] = { L"▣  全部", L"T  文本", L"▧  图像", L"▱  文件",
            favCount ? std::format(L"★  收藏 ({})", favCount) : L"★  收藏" };
        for (int i = 0; i < 5; ++i) {
            RECT r = tabRect(i);
            bool selected = (int)activeTab == i;
            if (selected) {
                fillRoundRect(dc, r, t.bg, 7);
                RECT squareBottom{ r.left, r.bottom - 8, r.right, r.bottom };
                fillRect(dc, squareBottom, t.bg);
            }
            drawText(dc, labels[i], r, selected ? t.text : t.textLighter,
                selected ? boldFont : uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        RECT separator{ 0,105,client.right,106 };
        fillRect(dc, separator, t.textBgLighter);

        if (multiMode) {
            RECT multiBg{ 16,11,std::max(130, (int)client.right - 146),50 };
            fillRoundRect(dc, multiBg, t.bg, 5);
            RECT count{ multiBg.left + 10,15,multiBg.left + 100,47 };
            drawText(dc, std::format(L"已选 {}", multiHashes.size()), count, t.primary, boldFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT copy{ multiBg.right - 220,14,multiBg.right - 154,48 };
            RECT paste{ multiBg.right - 148,14,multiBg.right - 82,48 };
            RECT exit{ multiBg.right - 76,14,multiBg.right - 10,48 };
            for (auto r : { copy,paste,exit }) fillRoundRect(dc, r, t.navHover, 5);
            drawText(dc, L"复制", copy, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"粘贴", paste, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"退出", exit, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
        const int w = (int)(rc.right - rc.left), h = (int)(rc.bottom - rc.top);
        const int contentW = std::max(1, w - sideRailWidth());
        if (listWnd) MoveWindow(listWnd, 0, 106, contentW, std::max(1, h - 106), TRUE);
        if (searchWnd) {
            int x = 24;
            int right = std::max(x + 90, contentW - 148);
            MoveWindow(searchWnd, x, 12, std::max(90, right - x - 8), 34, TRUE);
        }
        if (clearWnd) ShowWindow(clearWnd, SW_HIDE);
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
        fullImageZoom = 1.0;
        const auto hInst = GetModuleHandleW(nullptr);
        if (!fullWnd || !IsWindow(fullWnd)) {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            int ww = std::min(1000, std::max(680, (int)(work.right - work.left) - 120));
            int wh = std::min(760, std::max(520, (int)(work.bottom - work.top) - 120));
            int x = work.left + ((work.right - work.left) - ww) / 2;
            int y = work.top + ((work.bottom - work.top) - wh) / 2;
            fullWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"StarCapClipboardFullView",
                item->type == ItemType::Image ? L"StarCap 图片预览" : L"StarCap 内容预览",
                WS_OVERLAPPEDWINDOW, x, y, ww, wh, historyWnd, nullptr, hInst, nullptr);
        }
        if (!fullWnd) return;
        ShowWindow(fullWnd, SW_SHOW);
        SetWindowPos(fullWnd, HWND_TOP, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(fullWnd);
        SetFocus(fullWnd);
        InvalidateRect(fullWnd, nullptr, TRUE);
    }

    inline void hideFull()
    {
        if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
        if (historyWnd && IsWindow(historyWnd)) SetForegroundWindow(historyWnd);
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
                L"StarCap 剪贴板", MB_OK | MB_ICONINFORMATION);
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

    inline void deleteMultiItems()
    {
        if (multiHashes.empty()) return;
        items.erase(std::remove_if(items.begin(), items.end(), [](const Item& it) {
            return multiHashes.contains(it.hash);
        }), items.end());
        multiHashes.clear(); multiMode = false; multiAnchor = -1;
        totalBytes = 0; for (const auto& it : items) totalBytes += it.data.size();
        saveStore(); refreshList();
    }

    inline void showItemContextMenu(int idx, POINT screenPt)
    {
        Item* item = itemAtListIndex(idx);
        if (!item) return;
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        enum { CMD_PASTE = 5201, CMD_COPY, CMD_PREVIEW, CMD_FAVORITE, CMD_DELETE };
        AppendMenuW(menu, MF_STRING, CMD_PASTE, L"执行粘贴");
        AppendMenuW(menu, MF_STRING, CMD_COPY, L"复制");
        AppendMenuW(menu, MF_STRING, CMD_PREVIEW, item->type == ItemType::Image ? L"预览图片" : L"展开内容");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_FAVORITE, item->favorite ? L"取消收藏" : L"收藏");
        AppendMenuW(menu, MF_STRING, CMD_DELETE, L"删除记录");
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
            screenPt.x, screenPt.y, 0, historyWnd, nullptr);
        DestroyMenu(menu);
        if (cmd == CMD_PASTE) useListItem(idx, true);
        else if (cmd == CMD_COPY) useListItem(idx, false);
        else if (cmd == CMD_PREVIEW) showFull(idx);
        else if (cmd == CMD_FAVORITE) toggleFavorite(idx);
        else if (cmd == CMD_DELETE) deleteListItem(idx);
    }

    inline void selectRangeTo(int idx)
    {
        if (idx < 0 || idx >= (int)visibleItems.size()) return;
        if (multiAnchor < 0) multiAnchor = currentListIndex() >= 0 ? currentListIndex() : idx;
        multiMode = true; multiHashes.clear();
        int a = std::min(multiAnchor, idx), b = std::max(multiAnchor, idx);
        for (int i = a; i <= b; ++i) {
            if (auto* it = itemAtListIndex(i)) multiHashes.insert(it->hash);
        }
        SendMessageW(listWnd, LB_SETCURSEL, idx, 0);
        if (searchWnd) ShowWindow(searchWnd, SW_HIDE);
        if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE);
        InvalidateRect(listWnd, nullptr, TRUE);
    }

    inline LRESULT CALLBACK searchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
            if (historyWnd) ShowWindow(historyWnd, SW_HIDE);
            return 0;
        }
        if (msg == WM_PAINT) {
            LRESULT result = oldSearchProc ? CallWindowProcW(oldSearchProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
            if (GetWindowTextLengthW(hwnd) == 0) {
                HDC dc = GetDC(hwnd);
                if (dc) {
                    RECT r{}; GetClientRect(hwnd, &r); r.left += 7; r.right -= 7;
                    drawText(dc, L"搜索...", r, theme().textLighter, uiFont,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    ReleaseDC(hwnd, dc);
                }
            }
            return result;
        }
        if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
            LRESULT result = oldSearchProc ? CallWindowProcW(oldSearchProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, nullptr, TRUE);
            return result;
        }
        return oldSearchProc ? CallWindowProcW(oldSearchProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    inline LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int nextHover = HIWORD(hit) ? -1 : LOWORD(hit);
            if (nextHover < 0 || nextHover >= (int)visibleItems.size()) nextHover = -1;
            if (nextHover != hoverIndex) {
                hoverIndex = nextHover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE:
            if (hoverIndex != -1) {
                hoverIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (HIWORD(hit) || idx < 0 || idx >= (int)visibleItems.size()) return 0;
            RECT itemRc{}; SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc);
            RECT er = expandRect(itemRc);
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (shift) { selectRangeTo(idx); return 0; }
            SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
            multiAnchor = idx;
            if (pointIn(er, p)) { showFull(idx); return 0; }
            // Current official uTools behavior: a single click only selects.
            if (multiMode) { multiMode = false; multiHashes.clear(); if (searchWnd) ShowWindow(searchWnd, SW_SHOW); }
            InvalidateRect(hwnd, &itemRc, FALSE);
            if (historyWnd) InvalidateRect(historyWnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDBLCLK:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (!HIWORD(hit) && idx >= 0 && idx < (int)visibleItems.size()) {
                SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                useListItem(idx, true);
            }
            return 0;
        }
        case WM_RBUTTONUP:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (!HIWORD(hit) && idx >= 0 && idx < (int)visibleItems.size()) {
                SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &p);
                showItemContextMenu(idx, p);
            }
            return 0;
        }
        case WM_CHAR:
        {
            if (wParam >= 0x20 && wParam != 0x7f && searchWnd && !multiMode) {
                SetFocus(searchWnd);
                SendMessageW(searchWnd, WM_CHAR, wParam, lParam);
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
        {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int idx = currentListIndex();
            if (wParam == VK_ESCAPE) {
                if (multiMode) {
                    multiMode = false; multiHashes.clear(); if (searchWnd) ShowWindow(searchWnd, SW_SHOW);
                    if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE); InvalidateRect(hwnd, nullptr, TRUE);
                }
                else if (historyWnd) ShowWindow(historyWnd, SW_HIDE);
                return 0;
            }
            if (wParam == VK_RETURN) { if (multiMode) copyMulti(true); else useListItem(idx, true); return 0; }
            if (wParam == VK_DELETE) { if (multiMode) deleteMultiItems(); else deleteListItem(idx); return 0; }
            if (wParam == VK_SPACE) { toggleMultiCurrent(); if (multiAnchor < 0) multiAnchor = idx; return 0; }
            if (wParam == VK_TAB) {
                int next = idx + (shift ? -1 : 1);
                next = std::clamp(next, 0, std::max(0, (int)visibleItems.size() - 1));
                SendMessageW(hwnd, LB_SETCURSEL, next, 0);
                SendMessageW(hwnd, LB_SETTOPINDEX, std::max(0, next - 3), 0);
                multiAnchor = next; return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                int t = (int)activeTab + (wParam == VK_RIGHT ? 1 : -1);
                if (t < 0) t = 4; if (t > 4) t = 0;
                setTab((Tab)t); return 0;
            }
            if (wParam == VK_OEM_2 && !ctrl) { if (searchWnd) SetFocus(searchWnd); return 0; }
            if (ctrl && (wParam == 'F' || wParam == 'L')) { if (searchWnd) SetFocus(searchWnd); return 0; }
            if (ctrl && wParam == 'C') { if (multiMode) copyMulti(false); else useListItem(idx, false); return 0; }
            if (ctrl && (wParam == 'J' || wParam == 'K')) {
                int next = idx + (wParam == 'J' ? 1 : -1);
                next = std::clamp(next, 0, std::max(0, (int)visibleItems.size() - 1));
                SendMessageW(hwnd, LB_SETCURSEL, next, 0);
                SendMessageW(hwnd, LB_SETTOPINDEX, std::max(0, next - 3), 0);
                multiAnchor = next; return 0;
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
        case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_MOUSEWHEEL:
        {
            auto* item = findByHash(fullItemHash);
            if (item && item->type == ItemType::Image) {
                int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                fullImageZoom = std::clamp(fullImageZoom + steps * 0.25, 1.0, 5.0);
                InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            break;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) { hideFull(); return 0; }
            if (wParam == VK_RETURN) { restoreByHash(fullItemHash, true); return 0; }
            if (wParam == VK_OEM_PLUS || wParam == VK_ADD) { fullImageZoom = std::clamp(fullImageZoom + .25, 1.0, 5.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) { fullImageZoom = std::clamp(fullImageZoom - .25, 1.0, 5.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            if (wParam == '0') { fullImageZoom = 1.0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            break;
        case WM_LBUTTONDOWN:
        {
            RECT rc{}; GetClientRect(hwnd, &rc);
            RECT zoomOut{ rc.right - 330,12,rc.right - 286,50 };
            RECT zoomReset{ rc.right - 278,12,rc.right - 206,50 };
            RECT zoomIn{ rc.right - 198,12,rc.right - 154,50 };
            RECT copy{ rc.right - 146,12,rc.right - 102,50 };
            RECT close{ rc.right - 94,12,rc.right - 18,50 };
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            auto* item = findByHash(fullItemHash);
            if (item && item->type == ItemType::Image) {
                if (pointIn(zoomOut, p)) { fullImageZoom = std::clamp(fullImageZoom - .25, 1.0, 5.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
                if (pointIn(zoomReset, p)) { fullImageZoom = 1.0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
                if (pointIn(zoomIn, p)) { fullImageZoom = std::clamp(fullImageZoom + .25, 1.0, 5.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            }
            if (pointIn(copy, p)) { restoreByHash(fullItemHash, false); return 0; }
            if (pointIn(close, p)) { hideFull(); return 0; }
            break;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            Theme t = theme();
            fillRect(dc, rc, t.bg);
            auto* item = findByHash(fullItemHash);
            if (item) {
                RECT heading{ 20,10,rc.right - 350,52 };
                std::wstring title = item->type == ItemType::Image ? L"图片预览" : L"完整内容";
                if (item->type == ItemType::Image) title += L"  ·  滚轮 / +/- 缩放";
                drawText(dc, title, heading, t.text, boldFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                RECT zoomOut{ rc.right - 330,12,rc.right - 286,50 };
                RECT zoomReset{ rc.right - 278,12,rc.right - 206,50 };
                RECT zoomIn{ rc.right - 198,12,rc.right - 154,50 };
                RECT copy{ rc.right - 146,12,rc.right - 102,50 };
                RECT close{ rc.right - 94,12,rc.right - 18,50 };
                if (item->type == ItemType::Image) {
                    for (auto r : { zoomOut,zoomReset,zoomIn }) fillRoundRect(dc, r, t.navHover, 8);
                    drawText(dc, L"−", zoomOut, t.primary, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    std::wstring z = fullImageZoom <= 1.001 ? L"适应" : std::format(L"{}%", (int)std::lround(fullImageZoom * 100.0));
                    drawText(dc, z, zoomReset, t.primary, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    drawText(dc, L"+", zoomIn, t.primary, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                fillRoundRect(dc, copy, t.navHover, 8); fillRoundRect(dc, close, t.textBg, 8);
                drawText(dc, L"复制", copy, t.primary, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                drawText(dc, L"关闭", close, t.text, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                RECT content{ 18,62,rc.right - 18,rc.bottom - 18 };
                fillRoundRect(dc, content, t.textBg, 8);
                strokeRoundRect(dc, content, t.textBgLighter, 1, 8);
                RECT inner = content; InflateRect(&inner, -18, -18);
                if (item->type == ItemType::Image) drawDibZoom(dc, *item, inner, fullImageZoom);
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
        if (MessageBoxW(historyWnd, L"即将清空剪贴板记录（包括收藏内容）。", L"StarCap 剪贴板",
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
            Theme t = theme(); editBrush = CreateSolidBrush(t.navBg);
            listWnd = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_LIST), GetModuleHandleW(nullptr), nullptr);
            searchWnd = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_SEARCH), GetModuleHandleW(nullptr), nullptr);
            clearWnd = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_CLEAR_FLOAT), GetModuleHandleW(nullptr), nullptr);
            if (listWnd) {
                SendMessageW(listWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                oldListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(listWnd, GWLP_WNDPROC, (LONG_PTR)listProc));
            }
            if (searchWnd) {
                SendMessageW(searchWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                oldSearchProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(searchWnd, GWLP_WNDPROC, (LONG_PTR)searchProc));
                SendMessageW(searchWnd, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索...");
                SendMessageW(searchWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(7, 7));
            }
            refreshList(); layoutHistory(hwnd); return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto p = reinterpret_cast<MINMAXINFO*>(lParam);
            p->ptMinTrackSize.x = 520; p->ptMinTrackSize.y = 420; return 0;
        }
        case WM_SIZE: layoutHistory(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_CTLCOLOREDIT:
        {
            HDC dc = (HDC)wParam; Theme t = theme();
            SetTextColor(dc, t.text); SetBkColor(dc, t.navBg);
            return (LRESULT)editBrush;
        }
        case WM_MEASUREITEM:
        {
            auto mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mi && mi->CtlID == ID_LIST) {
                if (mi->itemID < visibleItems.size()) {
                    const auto& it = items[visibleItems[mi->itemID]];
                    if (it.type == ItemType::Image) mi->itemHeight = 210;
                    else if (it.type == ItemType::File) mi->itemHeight = filePaths(it.data).size() > 3 ? 160 : 112;
                    else { bool over = false; itemPreview(it, over); mi->itemHeight = over ? 150 : 96; }
                }
                else mi->itemHeight = 96;
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
        {
            auto dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlID == ID_LIST) { drawListItem(dis); return TRUE; }
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
            RECT client{}; GetClientRect(hwnd, &client);
            if (p.x >= client.right - sideRailWidth()) {
                for (int i = 0; i < 6; ++i) {
                    RECT br = sideRailButtonRect(client, i);
                    if (!pointIn(br, p)) continue;
                    if (i == 0) {
                        multiMode = !multiMode;
                        if (!multiMode) multiHashes.clear();
                        if (searchWnd) ShowWindow(searchWnd, multiMode ? SW_HIDE : SW_SHOW);
                        InvalidateRect(hwnd, nullptr, TRUE);
                        if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
                    }
                    else if (i == 1) useListItem(currentListIndex(), true);
                    else if (i == 2) {
                        sidePinned = !sidePinned;
                        SetWindowPos(hwnd, sidePinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                            0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    }
                    else if (i == 3) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                    else if (i == 4) WinSetting::init();
                    else if (i == 5) clearAll();
                    return 0;
                }
                return 0;
            }
            RECT rc{}; GetClientRect(hwnd, &rc);
            if (p.y < 58 && pointIn(themeButtonRect(rc), p)) { cycleThemeMode(); return 0; }
            if (p.y >= 58 && p.y < 106) {
                for (int i = 0; i < 5; ++i) if (pointIn(tabRect(i), p)) { setTab((Tab)i); return 0; }
            }
            if (multiMode && p.y < 58) {
                RECT multiBg{ 166,11,std::max(520, (int)rc.right - 146),50 };
                RECT copy{ multiBg.right - 220,14,multiBg.right - 154,48 };
                RECT paste{ multiBg.right - 148,14,multiBg.right - 82,48 };
                RECT exit{ multiBg.right - 76,14,multiBg.right - 10,48 };
                if (pointIn(copy, p)) { copyMulti(false); return 0; }
                if (pointIn(paste, p)) { copyMulti(true); return 0; }
                if (pointIn(exit, p)) {
                    multiMode = false; multiHashes.clear(); multiAnchor = -1;
                    if (searchWnd) ShowWindow(searchWnd, SW_SHOW);
                    InvalidateRect(hwnd, nullptr, TRUE); if (listWnd) InvalidateRect(listWnd, nullptr, TRUE); return 0;
                }
            }
            break;
        }
        case WM_SETTINGCHANGE:
            if (themeMode == ThemeMode::System) applyThemeNow();
            return 0;
        case WM_CLOSE:
            if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
            ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_DESTROY:
            historyWnd = nullptr; listWnd = nullptr; searchWnd = nullptr; clearWnd = nullptr; fullWnd = nullptr; oldListProc = nullptr; oldSearchProc = nullptr;
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            fillRect(dc, rc, theme().bg);
            RECT contentRc = rc;
            contentRc.right = std::max(contentRc.left, contentRc.right - sideRailWidth());
            drawTopBar(dc, contentRc);
            drawSideRail(dc, rc);
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
        loadThemeMode();
        const auto hInst = GetModuleHandleW(nullptr);
        WNDCLASSW lc{}; lc.lpfnWndProc = listenerProc; lc.hInstance = hInst; lc.lpszClassName = L"StarCapClipboardListener";
        RegisterClassW(&lc);
        WNDCLASSW hc{}; hc.lpfnWndProc = historyProc; hc.hInstance = hInst; hc.lpszClassName = L"StarCapClipboardHistory";
        hc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&hc);
        WNDCLASSW fc{}; fc.lpfnWndProc = fullProc; fc.hInstance = hInst; fc.lpszClassName = L"StarCapClipboardFullView";
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
            historyWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"StarCapClipboardHistory", L"StarCap 剪贴板",
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
            if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
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










