#pragma once

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>
#include <robuffer.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include "GeminiClient.h"

namespace GeminiClientHybridGeometry
{
    struct WindowsWord
    {
        int ymin{ 0 }, xmin{ 0 }, ymax{ 0 }, xmax{ 0 };
        std::wstring text;
    };

    inline std::wstring trimCopy(const std::wstring& value)
    {
        size_t first = 0, last = value.size();
        while (first < last && iswspace(value[first])) ++first;
        while (last > first && iswspace(value[last - 1])) --last;
        return value.substr(first, last - first);
    }

    inline wchar_t foldChar(wchar_t ch)
    {
        if (ch == 0x2018 || ch == 0x2019 || ch == 0xFF07) return L'\'';
        if (ch == 0x201C || ch == 0x201D) return L'"';
        if (ch >= L'A' && ch <= L'Z') return ch - L'A' + L'a';
        return (wchar_t)towlower(ch);
    }

    // Match OCR words by their semantic glyphs. Whitespace and punctuation are allowed
    // to differ because Gemini and Windows OCR often attach punctuation differently.
    inline std::wstring compactForMatch(const std::wstring& value)
    {
        std::wstring out;
        out.reserve(value.size());
        for (wchar_t ch : value) {
            ch = foldChar(ch);
            if (iswalnum(ch) || ch >= 0x2E80) out.push_back(ch);
        }
        return out;
    }

    inline int editDistance(const std::wstring& a, const std::wstring& b)
    {
        if (a.empty()) return (int)b.size();
        if (b.empty()) return (int)a.size();
        std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
        for (size_t i = 1; i <= a.size(); ++i) {
            cur[0] = (int)i;
            for (size_t j = 1; j <= b.size(); ++j) {
                const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
                cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
            }
            prev.swap(cur);
        }
        return prev[b.size()];
    }

    inline bool equalsInsensitiveAt(const std::wstring& haystack, size_t at, const std::wstring& needle)
    {
        if (at + needle.size() > haystack.size()) return false;
        for (size_t i = 0; i < needle.size(); ++i)
            if (foldChar(haystack[at + i]) != foldChar(needle[i])) return false;
        return true;
    }

    inline bool findExactNearby(const std::wstring& display, const std::wstring& raw,
        size_t cursor, size_t& start, size_t& end)
    {
        if (raw.empty() || cursor >= display.size()) return false;
        const size_t limit = std::min(display.size(), cursor + std::max<size_t>(128, raw.size() * 10));
        for (size_t i = cursor; i + raw.size() <= limit; ++i) {
            if (equalsInsensitiveAt(display, i, raw)) {
                start = i;
                end = i + raw.size();
                return true;
            }
        }
        return false;
    }

    inline bool findFuzzyNearby(const std::wstring& display, const std::wstring& raw,
        size_t cursor, size_t& start, size_t& end)
    {
        const auto target = compactForMatch(raw);
        if (target.empty() || cursor >= display.size()) return false;
        const size_t limit = std::min(display.size(), cursor + std::max<size_t>(144, raw.size() * 12));
        const int targetLen = (int)target.size();
        const int minCompact = std::max(1, targetLen - (targetLen >= 8 ? 3 : 2));
        const int maxCompact = targetLen + (targetLen >= 8 ? 3 : 2);

        float bestScore = -1.f;
        size_t bestStart = 0, bestEnd = 0;
        for (size_t s = cursor; s < limit; ++s) {
            if (display[s] == L'\r' || display[s] == L'\n') continue;
            if (iswspace(display[s])) continue;
            const size_t maxEnd = std::min(limit, s + std::max<size_t>(8, raw.size() + 8));
            for (size_t e = s + 1; e <= maxEnd; ++e) {
                bool hasBreak = false;
                for (size_t k = s; k < e; ++k) {
                    if (display[k] == L'\r' || display[k] == L'\n') { hasBreak = true; break; }
                }
                if (hasBreak) break;
                const auto candidate = compactForMatch(display.substr(s, e - s));
                const int clen = (int)candidate.size();
                if (clen < minCompact) continue;
                if (clen > maxCompact) break;
                const int dist = editDistance(target, candidate);
                const int denom = std::max(targetLen, clen);
                float similarity = denom ? 1.f - (float)dist / denom : 0.f;
                similarity -= std::min(.18f, (float)(s - cursor) * .0015f);
                if (similarity > bestScore) {
                    bestScore = similarity;
                    bestStart = s;
                    bestEnd = e;
                }
            }
        }

        const float threshold = targetLen <= 2 ? .999f : (targetLen <= 4 ? .66f : .58f);
        if (bestScore < threshold || bestEnd <= bestStart) return false;
        start = bestStart;
        end = bestEnd;
        return true;
    }

    inline std::vector<WindowsWord> recognizeWindowsWords(
        const std::vector<BYTE>& pixels, int width, int height)
    {
        std::vector<WindowsWord> out;
        if (pixels.empty() || width <= 0 || height <= 0) return out;
        bool apartmentReady = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentReady = true;
            using namespace winrt::Windows::Storage::Streams;
            using namespace winrt::Windows::Graphics::Imaging;
            using namespace winrt::Windows::Media::Ocr;

            const uint32_t byteCount = (uint32_t)pixels.size();
            Buffer buffer(byteCount);
            buffer.Length(byteCount);
            auto byteAccess = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
            BYTE* dst{ nullptr };
            winrt::check_hresult(byteAccess->Buffer(&dst));
            memcpy(dst, pixels.data(), pixels.size());

            SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Ignore);
            bitmap.CopyFromBuffer(buffer);
            auto engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (engine) {
                auto result = engine.RecognizeAsync(bitmap).get();
                for (auto const& line : result.Lines()) {
                    for (auto const& word : line.Words()) {
                        auto text = trimCopy(std::wstring{ word.Text().c_str() });
                        auto r = word.BoundingRect();
                        if (text.empty() || r.Width <= .5f || r.Height <= .5f) continue;
                        WindowsWord w;
                        w.xmin = std::clamp((int)std::lround(r.X * 1000.0 / width), 0, 1000);
                        w.ymin = std::clamp((int)std::lround(r.Y * 1000.0 / height), 0, 1000);
                        w.xmax = std::clamp((int)std::lround((r.X + r.Width) * 1000.0 / width), 0, 1000);
                        w.ymax = std::clamp((int)std::lround((r.Y + r.Height) * 1000.0 / height), 0, 1000);
                        if (w.xmax <= w.xmin || w.ymax <= w.ymin) continue;
                        w.text = std::move(text);
                        out.push_back(std::move(w));
                    }
                }
            }
            winrt::uninit_apartment();
            apartmentReady = false;
        }
        catch (...) {
            if (apartmentReady) { try { winrt::uninit_apartment(); } catch (...) {} }
            out.clear();
        }
        return out;
    }

    inline std::vector<GeminiClient::OcrBlock> alignWords(
        const std::wstring& geminiText, const std::vector<WindowsWord>& words)
    {
        std::vector<GeminiClient::OcrBlock> out;
        if (geminiText.empty() || words.empty()) return out;
        size_t cursor = 0;
        out.reserve(words.size());

        for (const auto& word : words) {
            size_t start = 0, end = 0;
            auto raw = trimCopy(word.text);
            if (raw.empty()) continue;
            bool found = findExactNearby(geminiText, raw, cursor, start, end);
            if (!found) found = findFuzzyNearby(geminiText, raw, cursor, start, end);
            if (!found || end <= start || end > geminiText.size()) continue;

            GeminiClient::OcrBlock b;
            b.ymin = word.ymin; b.xmin = word.xmin;
            b.ymax = word.ymax; b.xmax = word.xmax;
            // Store the exact Gemini substring. The linked-selection bridge can then use
            // deterministic exact matching while the rectangle still comes from Windows OCR.
            b.source = geminiText.substr(start, end - start);
            out.push_back(std::move(b));
            cursor = end;
        }
        return out;
    }

    inline GeminiClient::OcrResult recognizeImageHybrid(
        const std::vector<BYTE>& pixels, int width, int height,
        const std::wstring& apiKey, const std::wstring& model)
    {
        auto result = GeminiClient::recognizeImage(pixels, width, height, apiKey, model);
        if (!result.ok || result.text.empty()) return result;

        // Gemini owns transcription accuracy. Windows OCR is used ONLY as a local geometry
        // detector. Never replace Gemini's text with Windows OCR output.
        auto words = recognizeWindowsWords(pixels, width, height);
        auto aligned = alignWords(result.text, words);

        // Do not fall back to Gemini boxes here. If Windows geometry is unavailable, image
        // text selection is intentionally disabled instead of showing misleading highlights.
        result.blocks = std::move(aligned);
        return result;
    }
}

// This unqualified forwarding name is intentionally provided for the feature-branch
// preprocessor substitution around StarCapOcrV2.h.
inline GeminiClient::OcrResult recognizeImageHybrid(
    const std::vector<BYTE>& pixels, int width, int height,
    const std::wstring& apiKey, const std::wstring& model)
{
    return GeminiClientHybridGeometry::recognizeImageHybrid(pixels, width, height, apiKey, model);
}
