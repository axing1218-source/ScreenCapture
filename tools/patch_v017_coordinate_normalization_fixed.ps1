$ErrorActionPreference = 'Stop'

function Replace-Checked([ref]$textRef, [string]$old, [string]$new, [string]$label) {
    if (-not $textRef.Value.Contains($old)) { throw "Patch target not found: $label" }
    $textRef.Value = $textRef.Value.Replace($old, $new)
}

# v0.8.17 fixed PowerShell version.
# Normalize Gemini translation boxes at the parser boundary. Gemini sometimes
# returns source-pixel coordinates even though 0..1000 normalized coordinates
# were requested. Coordinate-space selection is based on text/box geometry.

$diagPath = 'Src\WeShotDiag.h'
$diag = @'
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
        return p + L"WeShot_Diagnostics.log";
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
'@
Set-Content $diagPath $diag -Encoding utf8

$path = 'Src\GeminiClient.h'
$src = Get-Content $path -Raw

$includeOld = '#include <winrt/Windows.Data.Json.h>'
$includeNew = @'
#include <winrt/Windows.Data.Json.h>
#include "WeShotDiag.h"
'@
if (-not $src.Contains('#include "WeShotDiag.h"')) {
    Replace-Checked ([ref]$src) $includeOld $includeNew 'diagnostic include'
}

$anchor = @'
    inline bool readBox(JsonArray box, int& ymin, int& xmin, int& ymax, int& xmax)
    {
        if (!box || box.Size() < 4) return false;
        ymin = std::clamp((int)std::lround(box.GetNumberAt(0)), 0, 1000);
        xmin = std::clamp((int)std::lround(box.GetNumberAt(1)), 0, 1000);
        ymax = std::clamp((int)std::lround(box.GetNumberAt(2)), 0, 1000);
        xmax = std::clamp((int)std::lround(box.GetNumberAt(3)), 0, 1000);
        return ymax > ymin && xmax > xmin;
    }
'@
$insert = @'
    inline bool readBox(JsonArray box, int& ymin, int& xmin, int& ymax, int& xmax)
    {
        if (!box || box.Size() < 4) return false;
        ymin = std::clamp((int)std::lround(box.GetNumberAt(0)), 0, 1000);
        xmin = std::clamp((int)std::lround(box.GetNumberAt(1)), 0, 1000);
        ymax = std::clamp((int)std::lround(box.GetNumberAt(2)), 0, 1000);
        xmax = std::clamp((int)std::lround(box.GetNumberAt(3)), 0, 1000);
        return ymax > ymin && xmax > xmin;
    }

    inline float sourceGlyphUnits(const std::wstring& text)
    {
        float units = 0.f;
        for (wchar_t ch : text) {
            if (ch == L'\r' || ch == L'\n') continue;
            if (ch == L'\t') { units += 1.2f; continue; }
            if (iswspace(ch)) { units += .32f; continue; }
            if (ch >= 0x2E80) { units += 1.f; continue; }
            if (iswalnum(ch)) { units += .55f; continue; }
            units += .38f;
        }
        return std::max(.75f, units);
    }

    inline float sourceMaxLineUnits(const std::wstring& text, int sourceLines)
    {
        float maxUnits = 0.f, current = 0.f;
        bool hadBreak = false;
        for (wchar_t ch : text) {
            if (ch == L'\r') continue;
            if (ch == L'\n') {
                maxUnits = std::max(maxUnits, current);
                current = 0.f;
                hadBreak = true;
                continue;
            }
            std::wstring one(1, ch);
            current += sourceGlyphUnits(one);
        }
        maxUnits = std::max(maxUnits, current);
        const int lines = std::max(1, sourceLines);
        if (!hadBreak && lines > 1) maxUnits = std::max(.75f, sourceGlyphUnits(text) / (float)lines * 1.08f);
        return std::max(.75f, maxUnits);
    }

    inline bool readTranslationBox(JsonArray box, int imageW, int imageH,
        const std::wstring& source, int sourceLines,
        int& ymin, int& xmin, int& ymax, int& xmax)
    {
        if (!box || box.Size() < 4 || imageW <= 0 || imageH <= 0) return false;
        const int ry1 = (int)std::lround(box.GetNumberAt(0));
        const int rx1 = (int)std::lround(box.GetNumberAt(1));
        const int ry2 = (int)std::lround(box.GetNumberAt(2));
        const int rx2 = (int)std::lround(box.GetNumberAt(3));
        if (ry2 <= ry1 || rx2 <= rx1) return false;

        const float rawW = (float)(rx2 - rx1);
        const float rawH = (float)(ry2 - ry1);
        const int lines = std::max(1, sourceLines);
        const float lineUnits = sourceMaxLineUnits(source, lines);
        auto consistency = [&](float regionW, float regionH) {
            const float fw = std::max(.001f, regionW / lineUnits);
            const float fh = std::max(.001f, regionH / (1.18f * (float)lines));
            return std::fabs(std::log(fw / fh));
        };

        const float scoreNorm = consistency(imageW * rawW / 1000.f, imageH * rawH / 1000.f);
        const bool pixelPossible = rx1 >= 0 && ry1 >= 0 &&
            rx2 <= imageW + std::max(3, imageW / 20) &&
            ry2 <= imageH + std::max(3, imageH / 20);
        const float scorePixel = pixelPossible ? consistency(rawW, rawH) : 999.f;
        const bool usePixels = pixelPossible && scorePixel + .28f < scoreNorm;

        auto normX = [&](int v) {
            return usePixels ? (int)std::lround((double)v * 1000.0 / std::max(1, imageW)) : v;
        };
        auto normY = [&](int v) {
            return usePixels ? (int)std::lround((double)v * 1000.0 / std::max(1, imageH)) : v;
        };
        ymin = std::clamp(normY(ry1), 0, 1000);
        xmin = std::clamp(normX(rx1), 0, 1000);
        ymax = std::clamp(normY(ry2), 0, 1000);
        xmax = std::clamp(normX(rx2), 0, 1000);

        WeShotDiag::append(std::format(
            L"translate-box image={}x{} raw=[{},{},{},{}] coord={} scoreN={:.3f} scoreP={:.3f} norm=[{},{},{},{}] lines={}",
            imageW, imageH, ry1, rx1, ry2, rx2, usePixels ? L"pixels" : L"norm1000",
            scoreNorm, scorePixel, ymin, xmin, ymax, xmax, lines));
        return ymax > ymin && xmax > xmin;
    }
'@
Replace-Checked ([ref]$src) $anchor $insert 'flexible translation coordinate reader'

$oldParse = @'
                TranslationBlock b;
                if (tr.empty() || !readBox(box, b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.source = std::move(source);
                b.translation = std::move(tr);
                b.role = role.empty() ? L"body" : std::move(role);
                b.sourceLines = std::clamp(sourceLines, 1, 100);
'@
$newParse = @'
                TranslationBlock b;
                const int safeLines = std::clamp(sourceLines, 1, 100);
                if (tr.empty() || !readTranslationBox(box, width, height, source, safeLines,
                    b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.source = std::move(source);
                b.translation = std::move(tr);
                b.role = role.empty() ? L"body" : std::move(role);
                b.sourceLines = safeLines;
'@
Replace-Checked ([ref]$src) $oldParse $newParse 'translateImage flexible coordinate parsing'

$promptOld = 'box_2d=[ymin,xmin,ymax,xmax] covering the whole source region, source copied exactly from that region, translation,'
$promptNew = 'box_2d=[ymin,xmin,ymax,xmax] covering the whole source region, with EVERY coordinate normalized to 0-1000 (full image is [0,0,1000,1000]; NEVER use source pixel coordinates), source copied exactly from that region, translation,'
if ($src.Contains($promptOld)) { $src = $src.Replace($promptOld, $promptNew) }

Set-Content $path $src -Encoding utf8

$verify = Get-Content $path -Raw
foreach ($needle in @('readTranslationBox', 'translate-box image=', 'coord=', 'WeShotDiag::append')) {
    if (-not $verify.Contains($needle)) { throw "v0.8.17 verification failed: $needle" }
}
Write-Host 'v0.8.17 parser-level coordinate normalization + source diagnostics applied.'
