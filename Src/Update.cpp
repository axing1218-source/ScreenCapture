#include "pch.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <array>
#include <format>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.Threading.h>
#include "Update.h"
#include "Setting.h"
#include "Lang.h"
#include "Util.h"

namespace {
    using namespace winrt::Windows::Foundation;
    using namespace winrt::Windows::Data::Json;
    using namespace winrt::Windows::Web::Http;
    using namespace winrt::Windows::Web::Http::Filters;
    using namespace winrt::Windows::Storage::Streams;
    using winrt::Windows::System::Threading::ThreadPoolTimer;

    // The GitHub Release is the single source of truth for StarCap updates.
    // "latest" ignores drafts and prereleases, so unpublished builds never
    // trigger an update prompt for users.
    constexpr std::wstring_view versionUrl{ L"https://github.com/axing1218-source/StarCap/releases/latest/download/version.json" };
    constexpr std::wstring_view exeUrl{ L"https://github.com/axing1218-source/StarCap/releases/latest/download/StarCap.exe" };
    constexpr std::wstring_view releaseUrl{ L"https://github.com/axing1218-source/StarCap/releases/latest" };
    constexpr std::wstring_view newExeName{ L"StarCap.update.exe" };

    bool checked{ false };
    UINT_PTR checkTimer{ 0 };
    UINT_PTR promptTimer{ 0 };
    std::filesystem::path newExePath;
    std::wstring newVer;

    long long today()
    {
        auto day = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        return day.time_since_epoch().count();
    }

    BOOL CALLBACK onEnumWin(HWND hwnd, LPARAM param)
    {
        if (!IsWindowVisible(hwnd)) return TRUE;
        *reinterpret_cast<int*>(param) += 1;
        return FALSE;
    }

    bool isIdle()
    {
        int count{ 0 };
        EnumThreadWindows(GetCurrentThreadId(), onEnumWin, reinterpret_cast<LPARAM>(&count));
        return count == 0;
    }

    std::filesystem::path selfPath()
    {
        std::vector<wchar_t> buf(MAX_PATH);
        auto len = GetModuleFileName(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return {};
        return std::filesystem::path{ std::wstring{ buf.data(), len } };
    }

    bool canWrite(const std::filesystem::path& dir)
    {
        auto probe = (dir / L"starcap.update.probe").wstring();
        auto file = CreateFile(probe.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        CloseHandle(file);
        return true;
    }

    void removeNewExe()
    {
        if (newExePath.empty()) return;
        std::error_code ec;
        std::filesystem::remove(newExePath, ec);
        newExePath.clear();
    }

    std::wstring quotePath(const std::filesystem::path& path)
    {
        std::wstring str = path.wstring();
        size_t pos{ 0 };
        while ((pos = str.find(L'\'', pos)) != std::wstring::npos) {
            str.insert(pos, 1, L'\'');
            pos += 2;
        }
        return L"'" + str + L"'";
    }

    bool startScript(const std::filesystem::path& exePath)
    {
        auto scriptPath = Setting::get()->getDataPath() / L"update.ps1";
        std::wstring pid = std::to_wstring(GetCurrentProcessId());
        std::wstring script;
        script += L"$ErrorActionPreference='SilentlyContinue'\r\n";
        script += L"Wait-Process -Id " + pid + L" -Timeout 60\r\n";
        script += L"Start-Sleep -Milliseconds 500\r\n";
        script += L"Copy-Item -LiteralPath " + quotePath(newExePath) + L" -Destination " + quotePath(exePath) + L" -Force\r\n";
        script += L"if ($?) { Start-Process -FilePath " + quotePath(exePath) + L" -ArgumentList '--enter=tray' }\r\n";
        script += L"Remove-Item -LiteralPath " + quotePath(newExePath) + L" -Force\r\n";
        script += L"Remove-Item -LiteralPath " + quotePath(scriptPath) + L" -Force\r\n";
        {
            std::ofstream file{ scriptPath, std::ios::binary | std::ios::trunc };
            if (!file) return false;
            file << "\xEF\xBB\xBF" << Ling::Util::convertToStr(script);
            if (!file.good()) return false;
        }
        std::wstring cmd{ L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" };
        cmd += scriptPath.wstring() + L"\"";
        STARTUPINFO si{ .cb{ sizeof(STARTUPINFO) } };
        PROCESS_INFORMATION pi{};
        auto flag = CreateProcess(nullptr, cmd.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (!flag) return false;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    void promptRestart()
    {
        auto title = Lang::get(L"about.sysTip");
        auto text = std::format(L"{} {}\n\n{}", Lang::get(L"update.found"), newVer, Lang::get(L"update.tip"));
        auto btnId = MessageBox(nullptr, text.data(), title.data(),
            MB_OKCANCEL | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        if (btnId != IDOK) {
            removeNewExe();
            return;
        }
        auto exePath = selfPath();
        if (exePath.empty() || !startScript(exePath)) {
            removeNewExe();
            return;
        }
        Ling::App::get()->quit(0);
    }

    void promptNoPermission(const std::wstring& verStr)
    {
        if (!isIdle()) return;
        auto title = Lang::get(L"about.sysTip");
        auto text = std::format(L"{} {}\n\n{}", Lang::get(L"update.found"), verStr, Lang::get(L"update.noPermission"));
        auto btnId = MessageBox(nullptr, text.data(), title.data(),
            MB_OKCANCEL | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        if (btnId != IDOK) return;
        ShellExecute(nullptr, L"open", releaseUrl.data(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void promptLater();

    void CALLBACK onPromptTimer(HWND, UINT, UINT_PTR id, DWORD)
    {
        KillTimer(nullptr, id);
        if (promptTimer == id) promptTimer = 0;
        if (newExePath.empty()) return;
        if (!isIdle()) {
            promptLater();
            return;
        }
        promptRestart();
    }

    void promptLater()
    {
        if (newExePath.empty() || promptTimer) return;
        promptTimer = SetTimer(nullptr, 0, 5000, onPromptTimer);
    }

    winrt::fire_and_forget doCheck()
    {
        co_await winrt::resume_background();
        try {
            auto exePath = selfPath();
            if (exePath.empty()) co_return;

            HttpBaseProtocolFilter filter;
            filter.CacheControl().ReadBehavior(HttpCacheReadBehavior::MostRecent);
            filter.CacheControl().WriteBehavior(HttpCacheWriteBehavior::NoCache);
            HttpClient client{ filter };

            auto op = client.GetStringAsync(Uri{ versionUrl });
            auto guard = ThreadPoolTimer::CreateTimer([op](const ThreadPoolTimer&) { op.Cancel(); },
                std::chrono::seconds(10));
            std::wstring body{ co_await op };
            guard.Cancel();

            JsonObject obj{ nullptr };
            if (!JsonObject::TryParse(body, obj)) co_return;
            auto arr = obj.GetNamedArray(L"version", nullptr);
            if (!arr || arr.Size() < 3) co_return;

            std::array<int, 3> remote{ 0, 0, 0 };
            for (uint32_t i = 0; i < 3; i++) {
                remote[i] = static_cast<int>(arr.GetNumberAt(i));
            }
            if (remote <= Ling::Util::getVerNum()) co_return;

            auto verStr = std::format(L"{}.{}.{}", remote[0], remote[1], remote[2]);
            if (!canWrite(exePath.parent_path())) {
                Ling::App::get()->dq.TryEnqueue([verStr]() { promptNoPermission(verStr); });
                co_return;
            }

            auto buffer = co_await client.GetBufferAsync(Uri{ exeUrl });
            std::vector<BYTE> bytes(buffer.Length());
            DataReader::FromBuffer(buffer).ReadBytes(bytes);
            if (bytes.size() < 2 || bytes[0] != 'M' || bytes[1] != 'Z') co_return;

            auto dataPath = Setting::get()->getDataPath();
            std::error_code ec;
            std::filesystem::create_directories(dataPath, ec);
            auto target = dataPath / newExeName;
            {
                std::ofstream file{ target, std::ios::binary | std::ios::trunc };
                if (!file) co_return;
                file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }

            // Never replace the running executable unless the downloaded file's
            // embedded version exactly matches the release metadata.
            if (Ling::Util::getVerNum(target.wstring()) != remote) {
                std::filesystem::remove(target, ec);
                co_return;
            }

            Ling::App::get()->dq.TryEnqueue([target, verStr]() {
                newExePath = target;
                newVer = verStr;
                promptLater();
            });
        }
        catch (...) {
            // Update checks are best-effort. Missing releases, network errors,
            // timeouts, redirects or invalid assets stay silent and retry later.
        }
    }

    void CALLBACK onCheckTimer(HWND, UINT, UINT_PTR id, DWORD)
    {
        KillTimer(nullptr, id);
        if (checkTimer == id) checkTimer = 0;
        if (!isIdle()) return;
        checked = true;
        Setting::get()->setUpdateCheckDay(today());
        doCheck();
    }
}

void Update::checkLater()
{
    if (!newExePath.empty()) {
        promptLater();
        return;
    }
    if (checked || checkTimer) return;
    auto lingApp = Ling::App::get();
    if (!lingApp) return;
    if (lingApp->args[L"--auto-quit"] == L"true") return;
    if (Setting::get()->getUpdateCheckDay() >= today()) return;
    checkTimer = SetTimer(nullptr, 0, 15000, onCheckTimer);
}
