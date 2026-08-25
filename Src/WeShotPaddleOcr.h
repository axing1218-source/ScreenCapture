#pragma once

#include <Windows.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <format>
#include <cfloat>
#include <winrt/Windows.Data.Json.h>
#include "Util.h"

// High-accuracy local OCR backend for WeShot.
//
// PaddleOCR-json is launched as a hidden child process and communicates through anonymous
// stdin/stdout pipes. No image leaves the machine. The executable + models live beside WeShot
// under ocr/PaddleOCR-json. If it is missing or fails to initialize, the caller can transparently
// fall back to Windows OCR.
namespace WeShotPaddleOcr
{
    struct Block
    {
        std::wstring text;
        float x{ 0 }, y{ 0 }, w{ 0 }, h{ 0 };
        double score{ 0.0 };
    };

    struct Result
    {
        bool available{ false };
        bool success{ false };
        std::wstring text;
        std::vector<Block> blocks;
        std::wstring error;
    };

    inline std::wstring utf8ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return {};
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
        return out;
    }

    inline std::string wideToUtf8(const std::wstring& s)
    {
        if (s.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    class Engine
    {
    public:
        ~Engine()
        {
            closeHandles();
            if (job) {
                CloseHandle(job);
                job = nullptr;
            }
        }

        Result recognize(const std::vector<BYTE>& pixels, int width, int height)
        {
            std::scoped_lock lock(mutex);
            Result out;

            auto exe = findExe();
            if (exe.empty()) {
                out.available = false;
                out.error = L"PaddleOCR-json 未安装";
                return out;
            }
            out.available = true;

            if (!ensureStarted(exe)) {
                out.error = lastError.empty() ? L"PaddleOCR-json 初始化失败" : lastError;
                return out;
            }

            auto temp = makeTempPngPath();
            if (temp.empty() || !Util::saveToFile(temp.wstring(), width, height, const_cast<BYTE*>(pixels.data()))) {
                out.error = L"无法准备 OCR 临时图片";
                return out;
            }

            try {
                using namespace winrt::Windows::Data::Json;
                JsonObject req;
                req.SetNamedValue(L"image_path", JsonValue::CreateStringValue(temp.wstring()));
                auto line = wideToUtf8(req.Stringify().c_str());
                if (!writeLine(line)) {
                    DeleteFileW(temp.c_str());
                    out.error = L"无法向 PaddleOCR-json 发送图片";
                    restartNeeded = true;
                    return out;
                }

                std::string reply;
                if (!readLine(reply, 30000)) {
                    DeleteFileW(temp.c_str());
                    out.error = L"PaddleOCR-json 响应超时";
                    restartNeeded = true;
                    return out;
                }
                DeleteFileW(temp.c_str());

                auto obj = JsonObject::Parse(utf8ToWide(reply));
                const int code = (int)obj.GetNamedNumber(L"code", -1);
                if (code == 101) {
                    out.error = L"没有识别到文字。";
                    return out;
                }
                if (code != 100) {
                    try { out.error = obj.GetNamedString(L"data", L"PaddleOCR-json 识别失败").c_str(); }
                    catch (...) { out.error = L"PaddleOCR-json 识别失败"; }
                    return out;
                }

                auto arr = obj.GetNamedArray(L"data");
                bool first = true;
                for (uint32_t i = 0; i < arr.Size(); ++i) {
                    auto item = arr.GetObjectAt(i);
                    std::wstring text = item.GetNamedString(L"text", L"").c_str();
                    if (text.empty()) continue;
                    const double score = item.GetNamedNumber(L"score", 0.0);

                    float minX = FLT_MAX, minY = FLT_MAX;
                    float maxX = -FLT_MAX, maxY = -FLT_MAX;
                    try {
                        auto box = item.GetNamedArray(L"box");
                        for (uint32_t p = 0; p < box.Size(); ++p) {
                            auto pt = box.GetArrayAt(p);
                            if (pt.Size() < 2) continue;
                            const float x = (float)pt.GetNumberAt(0);
                            const float y = (float)pt.GetNumberAt(1);
                            minX = (std::min)(minX, x); maxX = (std::max)(maxX, x);
                            minY = (std::min)(minY, y); maxY = (std::max)(maxY, y);
                        }
                    }
                    catch (...) {}

                    Block b;
                    b.text = text;
                    b.score = score;
                    if (maxX >= minX && maxY >= minY) {
                        b.x = minX; b.y = minY; b.w = maxX - minX; b.h = maxY - minY;
                    }
                    out.blocks.push_back(std::move(b));

                    if (!first) out.text += L"\r\n";
                    first = false;
                    out.text += text;
                }

                if (out.text.empty()) {
                    out.error = L"没有识别到文字。";
                    return out;
                }
                out.success = true;
                return out;
            }
            catch (const winrt::hresult_error& e) {
                DeleteFileW(temp.c_str());
                out.error = std::wstring(L"PaddleOCR-json 返回数据解析失败：") + e.message().c_str();
                return out;
            }
            catch (...) {
                DeleteFileW(temp.c_str());
                out.error = L"PaddleOCR-json 返回数据解析失败";
                return out;
            }
        }

    private:
        std::filesystem::path findExe()
        {
            wchar_t module[MAX_PATH]{};
            if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) return {};
            auto base = std::filesystem::path(module).parent_path();
            const std::filesystem::path candidates[] = {
                base / L"ocr" / L"PaddleOCR-json" / L"PaddleOCR-json.exe",
                base / L"PaddleOCR-json" / L"PaddleOCR-json.exe",
                base / L"PaddleOCR-json.exe"
            };
            for (const auto& p : candidates) if (std::filesystem::exists(p)) return p;
            return {};
        }

        std::filesystem::path makeTempPngPath()
        {
            wchar_t tempDir[MAX_PATH]{};
            if (!GetTempPathW(MAX_PATH, tempDir)) return {};
            auto name = std::format(L"WeShotOCR_{}_{}.png", GetCurrentProcessId(), GetTickCount64());
            return std::filesystem::path(tempDir) / name;
        }

        void closeHandles()
        {
            if (stdinWrite) { CloseHandle(stdinWrite); stdinWrite = nullptr; }
            if (stdoutRead) { CloseHandle(stdoutRead); stdoutRead = nullptr; }
            if (process) { CloseHandle(process); process = nullptr; }
            if (thread) { CloseHandle(thread); thread = nullptr; }
            rx.clear();
            started = false;
        }

        bool ensureStarted(const std::filesystem::path& exe)
        {
            if (started && !restartNeeded && process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return true;
            closeHandles();
            restartNeeded = false;
            lastError.clear();

            SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
            HANDLE childStdoutRead = nullptr, childStdoutWrite = nullptr;
            HANDLE childStdinRead = nullptr, childStdinWrite = nullptr;
            if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0)) {
                lastError = L"创建 OCR 输出管道失败";
                return false;
            }
            if (!SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
                CloseHandle(childStdoutRead); CloseHandle(childStdoutWrite);
                lastError = L"配置 OCR 输出管道失败";
                return false;
            }
            if (!CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0)) {
                CloseHandle(childStdoutRead); CloseHandle(childStdoutWrite);
                lastError = L"创建 OCR 输入管道失败";
                return false;
            }
            if (!SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
                CloseHandle(childStdoutRead); CloseHandle(childStdoutWrite);
                CloseHandle(childStdinRead); CloseHandle(childStdinWrite);
                lastError = L"配置 OCR 输入管道失败";
                return false;
            }

            HANDLE hNull = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            si.hStdInput = childStdinRead;
            si.hStdOutput = childStdoutWrite;
            si.hStdError = hNull != INVALID_HANDLE_VALUE ? hNull : childStdoutWrite;

            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"\"" + exe.wstring() + L"\" --limit_side_len 4320";
            std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
            cmdBuf.push_back(L'\0');
            auto cwd = exe.parent_path().wstring();

            const BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi);

            CloseHandle(childStdinRead);
            CloseHandle(childStdoutWrite);
            if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);

            if (!ok) {
                CloseHandle(childStdoutRead); CloseHandle(childStdinWrite);
                lastError = std::format(L"启动 PaddleOCR-json 失败（{}）", GetLastError());
                return false;
            }

            process = pi.hProcess;
            thread = pi.hThread;
            stdinWrite = childStdinWrite;
            stdoutRead = childStdoutRead;

            if (!job) {
                job = CreateJobObjectW(nullptr, nullptr);
                if (job) {
                    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
                    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
                }
            }
            if (job) AssignProcessToJobObject(job, process);

            const ULONGLONG deadline = GetTickCount64() + 20000;
            std::string line;
            while (GetTickCount64() < deadline) {
                const DWORD left = (DWORD)(deadline - GetTickCount64());
                const DWORD waitMs = left < (DWORD)500 ? left : (DWORD)500;
                if (!readLine(line, waitMs)) {
                    if (process && WaitForSingleObject(process, 0) != WAIT_TIMEOUT) break;
                    continue;
                }
                if (line.find("OCR init completed.") != std::string::npos) {
                    started = true;
                    return true;
                }
            }

            lastError = L"PaddleOCR-json 初始化超时或异常退出";
            restartNeeded = true;
            return false;
        }

        bool writeLine(const std::string& s)
        {
            if (!stdinWrite) return false;
            std::string data = s;
            data.push_back('\n');
            size_t done = 0;
            while (done < data.size()) {
                DWORD wrote = 0;
                if (!WriteFile(stdinWrite, data.data() + done, (DWORD)(data.size() - done), &wrote, nullptr) || wrote == 0) return false;
                done += wrote;
            }
            return true;
        }

        bool readLine(std::string& line, DWORD timeoutMs)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMs;
            while (GetTickCount64() <= deadline) {
                auto nl = rx.find('\n');
                if (nl != std::string::npos) {
                    line = rx.substr(0, nl);
                    rx.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    return true;
                }

                if (!stdoutRead) return false;
                DWORD avail = 0;
                if (!PeekNamedPipe(stdoutRead, nullptr, 0, nullptr, &avail, nullptr)) return false;
                if (avail > 0) {
                    char buf[4096];
                    const DWORD want = (std::min<DWORD>)(avail, (DWORD)sizeof(buf));
                    DWORD got = 0;
                    if (!ReadFile(stdoutRead, buf, want, &got, nullptr) || got == 0) return false;
                    rx.append(buf, buf + got);
                    continue;
                }
                if (process && WaitForSingleObject(process, 0) != WAIT_TIMEOUT) return false;
                Sleep(8);
            }
            return false;
        }

    private:
        std::mutex mutex;
        HANDLE process{ nullptr };
        HANDLE thread{ nullptr };
        HANDLE stdinWrite{ nullptr };
        HANDLE stdoutRead{ nullptr };
        HANDLE job{ nullptr };
        bool started{ false };
        bool restartNeeded{ false };
        std::wstring lastError;
        std::string rx;
    };

    inline Engine engine;

    inline Result recognize(const std::vector<BYTE>& pixels, int width, int height)
    {
        return engine.recognize(pixels, width, height);
    }
}
