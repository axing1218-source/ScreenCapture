#pragma once
#include <Windows.h>
#include <string>
#include <format>

namespace WeShotDiag
{
    inline std::wstring logPath()
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p = exe;
        auto pos = p.find_last_of(L"\\/");
        if (pos != std::wstring::npos) p.resize(pos + 1); else p.clear();
        return p + L"StarCap_Diagnostics.log";
    }

    inline void append(const std::wstring& message)
    {
        SYSTEMTIME st{}; GetLocalTime(&st);
        auto line = std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} {}\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, message);
        int n = WideCharToMultiByte(CP_UTF8, 0, line.data(), (int)line.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return;
        std::string utf8((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.data(), (int)line.size(), utf8.data(), n, nullptr, nullptr);
        auto path = logPath();
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD written = 0; WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        CloseHandle(h);
    }
}

