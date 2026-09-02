#pragma once

#include <Windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <objidl.h>
#include <wrl.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include "WeShotDiag.h"

namespace GeminiClient
{
    using Microsoft::WRL::ComPtr;
    using namespace winrt::Windows::Data::Json;

    struct OcrBlock
    {
        int ymin{ 0 }, xmin{ 0 }, ymax{ 0 }, xmax{ 0 };
        std::wstring source;
    };

    struct OcrResult
    {
        bool ok{ false };
        std::wstring text;
        std::vector<OcrBlock> blocks;
        std::wstring error;
    };

    struct TranslationBlock
    {
        int ymin{ 0 }, xmin{ 0 }, ymax{ 0 }, xmax{ 0 };
        std::wstring source;
        std::wstring translation;
        std::wstring role{ L"body" };
        int sourceLines{ 1 };
        float sourceLineHeight{ 0.f };
    };

    struct TranslationResult
    {
        bool ok{ false };
        std::wstring sourceText;
        std::wstring translatedText;
        std::vector<TranslationBlock> blocks;
        std::wstring error;
    };

    struct TestResult
    {
        bool ok{ false };
        std::wstring message;
    };

    class ComScope
    {
    public:
        ComScope() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
        ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
    private:
        HRESULT hr;
    };

    inline std::string wideToUtf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string out((size_t)len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), out.data(), len, nullptr, nullptr);
        return out;
    }

    inline std::wstring utf8ToWide(const std::string& value)
    {
        if (value.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
        if (len <= 0) len = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0);
        if (len <= 0) return {};
        std::wstring out((size_t)len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), out.data(), len);
        return out;
    }

    inline std::string base64Encode(const BYTE* data, size_t len)
    {
        static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        if (!data || len == 0) return {};
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3) {
            const unsigned a = data[i];
            const unsigned b = i + 1 < len ? data[i + 1] : 0;
            const unsigned c = i + 2 < len ? data[i + 2] : 0;
            const unsigned v = (a << 16) | (b << 8) | c;
            out.push_back(table[(v >> 18) & 63]);
            out.push_back(table[(v >> 12) & 63]);
            out.push_back(i + 1 < len ? table[(v >> 6) & 63] : '=');
            out.push_back(i + 2 < len ? table[v & 63] : '=');
        }
        return out;
    }

    inline bool encodePng(const std::vector<BYTE>& pixels, int width, int height, std::vector<BYTE>& png)
    {
        if (width <= 0 || height <= 0 || pixels.size() < (size_t)width * height * 4) return false;
        ComPtr<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()))) return false;
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf())))) return false;
        ComPtr<IWICBitmapEncoder> encoder;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()))) return false;
        if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr))) return false;
        if (FAILED(frame->Initialize(nullptr))) return false;
        if (FAILED(frame->SetSize((UINT)width, (UINT)height))) return false;
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&fmt)) || !IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA)) return false;
        const UINT rowBytes = (UINT)width * 4;
        if (FAILED(frame->WritePixels((UINT)height, rowBytes, rowBytes * (UINT)height,
            const_cast<BYTE*>(pixels.data())))) return false;
        if (FAILED(frame->Commit()) || FAILED(encoder->Commit())) return false;
        STATSTG stat{};
        if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return false;
        const size_t size = (size_t)stat.cbSize.QuadPart;
        if (!size) return false;
        HGLOBAL mem{};
        if (FAILED(GetHGlobalFromStream(stream.Get(), &mem)) || !mem) return false;
        auto ptr = static_cast<const BYTE*>(GlobalLock(mem));
        if (!ptr) return false;
        png.assign(ptr, ptr + size);
        GlobalUnlock(mem);
        return true;
    }

    struct HttpResult
    {
        DWORD status{ 0 };
        std::string body;
        std::wstring error;
    };

    inline HttpResult postGenerate(const std::wstring& apiKey, const std::wstring& model, const std::wstring& json)
    {
        HttpResult result;
        if (apiKey.empty()) { result.error = L"Gemini API Key 为空。"; return result; }
        const auto modelId = model.empty() ? std::wstring(L"gemini-3.7-flash") : model;
        HINTERNET session = WinHttpOpen(L"StarCap/0.9.7", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { result.error = L"无法初始化网络连接。"; return result; }
        // 截图工具不应让一次请求挂一分钟。连接失败尽快反馈；正常 Flash 请求通常远低于此上限。
        WinHttpSetTimeouts(session, 8000, 8000, 15000, 25000);
        HINTERNET connect = WinHttpConnect(session, L"generativelanguage.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) { WinHttpCloseHandle(session); result.error = L"无法连接 Gemini API。"; return result; }
        std::wstring path = L"/v1beta/models/" + modelId + L":generateContent";
        HINTERNET request = WinHttpOpenRequest(connect, L"POST", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request) {
            WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
            result.error = L"无法创建 Gemini 请求。"; return result;
        }
        std::wstring headers = L"Content-Type: application/json\r\nx-goog-api-key: " + apiKey + L"\r\n";
        WinHttpAddRequestHeaders(request, headers.c_str(), (DWORD)-1L,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        auto body = wideToUtf8(json);
        BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
        if (sent) sent = WinHttpReceiveResponse(request, nullptr);
        if (!sent) {
            result.error = std::format(L"Gemini 网络请求失败（Windows 错误 {}）。", GetLastError());
        }
        else {
            DWORD statusSize = sizeof(result.status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                const size_t oldSize = result.body.size();
                result.body.resize(oldSize + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, result.body.data() + oldSize, available, &read)) {
                    result.body.resize(oldSize); break;
                }
                result.body.resize(oldSize + read);
            }
        }
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    inline std::wstring getApiError(const std::string& response)
    {
        JsonObject root{ nullptr };
        const auto wide = utf8ToWide(response);
        if (JsonObject::TryParse(wide, root)) {
            auto err = root.GetNamedObject(L"error", nullptr);
            if (err) {
                auto msg = std::wstring{ err.GetNamedString(L"message", L"") };
                if (!msg.empty()) return msg;
            }
        }
        return wide.size() > 500 ? wide.substr(0, 500) + L"..." : wide;
    }

    inline std::wstring extractGenerateText(const std::string& response)
    {
        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(utf8ToWide(response), root)) return {};
        auto candidates = root.GetNamedArray(L"candidates", nullptr);
        if (!candidates || candidates.Size() == 0) return {};
        auto content = candidates.GetObjectAt(0).GetNamedObject(L"content", nullptr);
        if (!content) return {};
        auto parts = content.GetNamedArray(L"parts", nullptr);
        if (!parts) return {};
        for (uint32_t i = 0; i < parts.Size(); ++i) {
            auto part = parts.GetObjectAt(i);
            auto text = std::wstring{ part.GetNamedString(L"text", L"") };
            if (!text.empty()) return text;
        }
        return {};
    }

    inline JsonObject makeBoxSchema()
    {
        JsonObject intType;
        intType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"integer"));
        JsonObject box;
        box.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        box.SetNamedValue(L"items", intType);
        box.SetNamedValue(L"minItems", JsonValue::CreateNumberValue(4));
        box.SetNamedValue(L"maxItems", JsonValue::CreateNumberValue(4));
        return box;
    }

    inline JsonObject makeOcrSchema()
    {
        JsonObject strType; strType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"string"));
        JsonObject blockProps;
        blockProps.SetNamedValue(L"box_2d", makeBoxSchema());
        blockProps.SetNamedValue(L"text", strType);
        JsonArray blockReq;
        blockReq.Append(JsonValue::CreateStringValue(L"box_2d"));
        blockReq.Append(JsonValue::CreateStringValue(L"text"));
        JsonObject block;
        block.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        block.SetNamedValue(L"properties", blockProps);
        block.SetNamedValue(L"required", blockReq);
        JsonObject blocks;
        blocks.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        blocks.SetNamedValue(L"items", block);
        JsonObject props;
        props.SetNamedValue(L"full_text", strType);
        props.SetNamedValue(L"blocks", blocks);
        JsonArray req;
        req.Append(JsonValue::CreateStringValue(L"full_text"));
        req.Append(JsonValue::CreateStringValue(L"blocks"));
        JsonObject schema;
        schema.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        schema.SetNamedValue(L"properties", props);
        schema.SetNamedValue(L"required", req);
        return schema;
    }

    inline JsonObject makeTranslationSchema()
    {
        JsonObject strType; strType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"string"));
        JsonObject intType; intType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"integer"));
        JsonObject blockProps;
        blockProps.SetNamedValue(L"box_2d", makeBoxSchema());
        blockProps.SetNamedValue(L"source", strType);
        blockProps.SetNamedValue(L"translation", strType);
        blockProps.SetNamedValue(L"role", strType);
        blockProps.SetNamedValue(L"source_lines", intType);
        JsonArray blockReq;
        blockReq.Append(JsonValue::CreateStringValue(L"box_2d"));
        blockReq.Append(JsonValue::CreateStringValue(L"source"));
        blockReq.Append(JsonValue::CreateStringValue(L"translation"));
        blockReq.Append(JsonValue::CreateStringValue(L"role"));
        blockReq.Append(JsonValue::CreateStringValue(L"source_lines"));
        JsonObject block;
        block.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        block.SetNamedValue(L"properties", blockProps);
        block.SetNamedValue(L"required", blockReq);
        JsonObject blocks;
        blocks.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        blocks.SetNamedValue(L"items", block);
        JsonObject props;
        props.SetNamedValue(L"source_text", strType);
        props.SetNamedValue(L"translated_text", strType);
        props.SetNamedValue(L"blocks", blocks);
        JsonArray req;
        req.Append(JsonValue::CreateStringValue(L"source_text"));
        req.Append(JsonValue::CreateStringValue(L"translated_text"));
        req.Append(JsonValue::CreateStringValue(L"blocks"));
        JsonObject schema;
        schema.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        schema.SetNamedValue(L"properties", props);
        schema.SetNamedValue(L"required", req);
        return schema;
    }

    inline JsonObject makeTextTranslationSchema()
    {
        JsonObject strType; strType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"string"));
        JsonObject intType; intType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"integer"));
        JsonObject itemProps;
        itemProps.SetNamedValue(L"id", intType);
        itemProps.SetNamedValue(L"translation", strType);
        JsonArray itemReq;
        itemReq.Append(JsonValue::CreateStringValue(L"id"));
        itemReq.Append(JsonValue::CreateStringValue(L"translation"));
        JsonObject item;
        item.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        item.SetNamedValue(L"properties", itemProps);
        item.SetNamedValue(L"required", itemReq);
        JsonObject items;
        items.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        items.SetNamedValue(L"items", item);
        JsonObject props;
        props.SetNamedValue(L"translated_text", strType);
        props.SetNamedValue(L"items", items);
        JsonArray req;
        req.Append(JsonValue::CreateStringValue(L"translated_text"));
        req.Append(JsonValue::CreateStringValue(L"items"));
        JsonObject schema;
        schema.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        schema.SetNamedValue(L"properties", props);
        schema.SetNamedValue(L"required", req);
        return schema;
    }

    inline void addFastThinking(JsonObject& generationConfig, const std::wstring& model)
    {
        JsonObject thinking;
        if (model.find(L"2.5") != std::wstring::npos) {
            thinking.SetNamedValue(L"thinkingBudget", JsonValue::CreateNumberValue(0));
        }
        else {
            // Flash 截图 OCR/翻译属于简单视觉任务，minimal 优先低延迟；非 Flash 用 low 更稳妥。
            thinking.SetNamedValue(L"thinkingLevel", JsonValue::CreateStringValue(
                model.find(L"flash") != std::wstring::npos ? L"minimal" : L"low"));
        }
        generationConfig.SetNamedValue(L"thinkingConfig", thinking);
    }

    inline JsonObject makeBaseRequest(const std::wstring& model, const std::wstring& prompt,
        const std::vector<BYTE>* png, JsonObject schema, int maxOutputTokens = 8192)
    {
        JsonArray parts;
        if (png && !png->empty()) {
            JsonObject blob;
            blob.SetNamedValue(L"mimeType", JsonValue::CreateStringValue(L"image/png"));
            blob.SetNamedValue(L"data", JsonValue::CreateStringValue(utf8ToWide(base64Encode(png->data(), png->size()))));
            JsonObject imagePart;
            imagePart.SetNamedValue(L"inlineData", blob);
            parts.Append(imagePart);
        }
        JsonObject textPart;
        textPart.SetNamedValue(L"text", JsonValue::CreateStringValue(prompt));
        parts.Append(textPart);
        JsonObject content;
        content.SetNamedValue(L"role", JsonValue::CreateStringValue(L"user"));
        content.SetNamedValue(L"parts", parts);
        JsonArray contents;
        contents.Append(content);
        JsonObject generation;
        generation.SetNamedValue(L"responseMimeType", JsonValue::CreateStringValue(L"application/json"));
        generation.SetNamedValue(L"responseSchema", schema);
        generation.SetNamedValue(L"maxOutputTokens", JsonValue::CreateNumberValue(maxOutputTokens));
        addFastThinking(generation, model);
        JsonObject root;
        root.SetNamedValue(L"contents", contents);
        root.SetNamedValue(L"generationConfig", generation);
        return root;
    }

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

        // Gemini can occasionally return a region whose coordinate SCALE is valid,
        // but whose geometry is impossible for the source text (for example a whole
        // sentence in a box only 1-2 physical pixels tall).  Repair that at the parser
        // boundary using source-content geometry, not screenshot-size buckets.
        //
        // The source font can be estimated independently from width and height:
        //   fontW ~= regionWidth / sourceLineGlyphUnits
        //   fontH ~= regionHeight / (lineHeightFactor * sourceLines)
        // A large disagreement means one axis of the returned box is under-sized.
        // We only EXPAND the implausibly small axis; we never shrink a valid box.
        const int beforeYmin = ymin, beforeXmin = xmin, beforeYmax = ymax, beforeXmax = xmax;
        const float regionWpx = imageW * std::max(1, xmax - xmin) / 1000.f;
        const float regionHpx = imageH * std::max(1, ymax - ymin) / 1000.f;
        const float lineGlyphUnits = sourceMaxLineUnits(source, lines);
        const float fontFromW = regionWpx / std::max(.75f, lineGlyphUnits);
        const float fontFromH = regionHpx / (1.18f * (float)lines);

        auto expandNormalizedSpan = [](int& lo, int& hi, int desiredSpan) {
            desiredSpan = std::clamp(desiredSpan, 1, 1000);
            const float center = (lo + hi) * .5f;
            int nlo = (int)std::lround(center - desiredSpan * .5f);
            int nhi = nlo + desiredSpan;
            if (nlo < 0) { nhi -= nlo; nlo = 0; }
            if (nhi > 1000) { nlo -= (nhi - 1000); nhi = 1000; }
            nlo = std::clamp(nlo, 0, 999);
            nhi = std::clamp(nhi, nlo + 1, 1000);
            lo = nlo; hi = nhi;
        };

        // v0.8.19: continuous geometry reconstruction.
        // Do not wait for an arbitrary mismatch threshold before repairing a box.
        // Width and height are two independent noisy measurements of the SAME source
        // font scale.  Combine them with a smooth high-order power mean.  This follows
        // the more informative/larger estimate when one axis collapses, but changes
        // normal boxes only slightly when both estimates already agree.
        const float safeFW = std::max(.01f, fontFromW);
        const float safeFH = std::max(.01f, fontFromH);
        constexpr float p = 6.f;
        float stableFont = std::pow((std::pow(safeFW, p) + std::pow(safeFH, p)) * .5f, 1.f / p);

        // A reconstructed font can never require more space than the whole image.
        // This is a geometric feasibility bound, not a screenshot-size mode rule.
        const float maxByWidth = imageW / std::max(.75f, lineGlyphUnits);
        const float maxByHeight = imageH / (1.18f * (float)lines);
        stableFont = std::clamp(stableFont, .01f, std::max(.01f, std::min(maxByWidth, maxByHeight)));

        const float desiredWpx = std::min((float)imageW, stableFont * lineGlyphUnits);
        const float desiredHpx = std::min((float)imageH, stableFont * 1.18f * (float)lines);
        const int desiredNormW = (int)std::lround(desiredWpx * 1000.f / std::max(1, imageW));
        const int desiredNormH = (int)std::lround(desiredHpx * 1000.f / std::max(1, imageH));

        bool expandedX = false, expandedY = false;
        if (desiredNormW > xmax - xmin) {
            expandNormalizedSpan(xmin, xmax, desiredNormW);
            expandedX = true;
        }
        if (desiredNormH > ymax - ymin) {
            expandNormalizedSpan(ymin, ymax, desiredNormH);
            expandedY = true;
        }
        const std::wstring geometryFix = expandedX && expandedY ? L"rebuildXY" :
            (expandedX ? L"rebuildX" : (expandedY ? L"rebuildY" : L"stable"));

        WeShotDiag::append(std::format(
            L"translate-box image={}x{} raw=[{},{},{},{}] coord={} scoreN={:.3f} scoreP={:.3f} norm0=[{},{},{},{}] geom={} fontW={:.2f} fontH={:.2f} stableFont={:.2f} desiredPx={:.1f}x{:.1f} norm=[{},{},{},{}] lines={}",
            imageW, imageH, ry1, rx1, ry2, rx2, usePixels ? L"pixels" : L"norm1000",
            scoreNorm, scorePixel, beforeYmin, beforeXmin, beforeYmax, beforeXmax,
            geometryFix, fontFromW, fontFromH, stableFont, desiredWpx, desiredHpx,
            ymin, xmin, ymax, xmax, lines));
        return ymax > ymin && xmax > xmin;
    }

    inline TestResult testConnection(const std::wstring& apiKey, const std::wstring& model)
    {
        TestResult out;
        const auto modelId = model.empty() ? std::wstring(L"gemini-3.7-flash") : model;
        JsonArray parts;
        JsonObject part; part.SetNamedValue(L"text", JsonValue::CreateStringValue(L"Reply with exactly OK"));
        parts.Append(part);
        JsonObject content; content.SetNamedValue(L"parts", parts);
        JsonArray contents; contents.Append(content);
        JsonObject generation; addFastThinking(generation, modelId);
        generation.SetNamedValue(L"maxOutputTokens", JsonValue::CreateNumberValue(16));
        JsonObject root; root.SetNamedValue(L"contents", contents); root.SetNamedValue(L"generationConfig", generation);
        auto http = postGenerate(apiKey, modelId, root.Stringify().c_str());
        if (!http.error.empty()) { out.message = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.message = std::format(L"连接失败（HTTP {}）：{}", http.status, getApiError(http.body)); return out;
        }
        if (extractGenerateText(http.body).empty()) { out.message = L"Gemini 已响应，但没有返回文本。"; return out; }
        out.ok = true; out.message = L"连接成功"; return out;
    }

    inline OcrResult recognizeImage(const std::vector<BYTE>& pixels, int width, int height,
        const std::wstring& apiKey, const std::wstring& model)
    {
        OcrResult out;
        ComScope com;
        std::vector<BYTE> png;
        if (!encodePng(pixels, width, height, png)) { out.error = L"图片编码失败。"; return out; }
        if (png.size() > 14 * 1024 * 1024) { out.error = L"图片过大，长截图分块识别将在后续版本处理。"; return out; }
        const auto modelId = model.empty() ? std::wstring(L"gemini-3.7-flash") : model;
        auto req = makeBaseRequest(modelId,
            L"OCR this screenshot. Extract every readable text exactly as shown. Return full_text in natural reading order and blocks. "
            L"Each block must contain the exact source text and box_2d=[ymin,xmin,ymax,xmax] normalized 0-1000. "
            L"Do not translate, summarize, correct spelling, or add commentary. Include small UI labels when readable.",
            &png, makeOcrSchema(), 8192);
        auto http = postGenerate(apiKey, modelId, req.Stringify().c_str());
        if (!http.error.empty()) { out.error = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"Gemini OCR 失败（HTTP {}）：{}", http.status, getApiError(http.body)); return out;
        }
        auto text = extractGenerateText(http.body);
        JsonObject payload{ nullptr };
        if (text.empty() || !JsonObject::TryParse(text, payload)) { out.error = L"Gemini OCR 返回格式无法解析。"; return out; }
        out.text = std::wstring{ payload.GetNamedString(L"full_text", L"") };
        auto blocks = payload.GetNamedArray(L"blocks", nullptr);
        if (blocks) {
            for (uint32_t i = 0; i < blocks.Size(); ++i) {
                auto item = blocks.GetObjectAt(i);
                auto box = item.GetNamedArray(L"box_2d", nullptr);
                auto source = std::wstring{ item.GetNamedString(L"text", L"") };
                OcrBlock b;
                if (source.empty() || !readBox(box, b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.source = std::move(source); out.blocks.push_back(std::move(b));
            }
        }
        if (out.text.empty() && !out.blocks.empty()) {
            for (auto& b : out.blocks) { if (!out.text.empty()) out.text += L"\r\n"; out.text += b.source; }
        }
        if (out.text.empty()) { out.error = L"Gemini 没有识别到文字。"; return out; }
        out.ok = true; return out;
    }

    inline TranslationResult translateImage(const std::vector<BYTE>& pixels, int width, int height,
        const std::wstring& apiKey, const std::wstring& model,
        const std::wstring& targetLanguage = L"Simplified Chinese")
    {
        TranslationResult out;
        ComScope com;
        std::vector<BYTE> png;
        if (!encodePng(pixels, width, height, png)) { out.error = L"图片编码失败。"; return out; }
        if (png.size() > 14 * 1024 * 1024) { out.error = L"图片过大，长截图分块翻译将在后续版本处理。"; return out; }
        const auto modelId = model.empty() ? std::wstring(L"gemini-3.7-flash") : model;
        auto req = makeBaseRequest(modelId,
            L"Read ALL readable text in this screenshot from top to bottom, including a tall/long screenshot. "
            L"Return source_text exactly as read in natural reading order, then translate all of it into " + targetLanguage +
            L" as translated_text. Preserve paragraph breaks in translated_text. "
            L"For blocks, group text by VISUAL REGION: one title, heading, paragraph, caption, or short UI label per block. "
            L"Merge adjacent wrapped lines that belong to the same paragraph; DO NOT make a separate block for every visual line. "
            L"A visible blank vertical gap starts a NEW block: never combine text across paragraph whitespace. "
            L"A consecutive list/menu of short rows is ONE list-like body block, and its translation must keep one item per visible line using newline separators. "
            L"Each block must contain box_2d=[ymin,xmin,ymax,xmax] covering the whole source region, with EVERY coordinate normalized to 0-1000 (full image is [0,0,1000,1000]; NEVER use source pixel coordinates), source copied exactly from that region, translation, "
            L"role as exactly one of title, heading, body, caption, label, and source_lines as the number of visible source lines in that region. "
            L"Preserve visible line breaks inside each block's source string whenever possible. "
            L"Keep title/body hierarchy and original reading order. Do not omit later/bottom sections of a long screenshot. "
            L"Do not summarize, add commentary, generate, or edit an image.",
            &png, makeTranslationSchema(), 16384);
        auto http = postGenerate(apiKey, modelId, req.Stringify().c_str());
        if (!http.error.empty()) { out.error = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"Gemini 翻译失败（HTTP {}）：{}", http.status, getApiError(http.body)); return out;
        }
        auto text = extractGenerateText(http.body);
        JsonObject payload{ nullptr };
        if (text.empty() || !JsonObject::TryParse(text, payload)) { out.error = L"Gemini 翻译返回格式无法解析。"; return out; }
        out.sourceText = std::wstring{ payload.GetNamedString(L"source_text", L"") };
        out.translatedText = std::wstring{ payload.GetNamedString(L"translated_text", L"") };
        auto blocks = payload.GetNamedArray(L"blocks", nullptr);
        if (blocks) {
            for (uint32_t i = 0; i < blocks.Size(); ++i) {
                auto item = blocks.GetObjectAt(i);
                auto box = item.GetNamedArray(L"box_2d", nullptr);
                auto source = std::wstring{ item.GetNamedString(L"source", L"") };
                auto tr = std::wstring{ item.GetNamedString(L"translation", L"") };
                auto role = std::wstring{ item.GetNamedString(L"role", L"body") };
                int sourceLines = (int)std::lround(item.GetNamedNumber(L"source_lines", 1));
                TranslationBlock b;
                const int safeLines = std::clamp(sourceLines, 1, 100);
                if (tr.empty() || !readTranslationBox(box, width, height, source, safeLines,
                    b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.source = std::move(source);
                b.translation = std::move(tr);
                b.role = role.empty() ? L"body" : std::move(role);
                b.sourceLines = safeLines;
                out.blocks.push_back(std::move(b));
            }
        }
        if (out.translatedText.empty() && !out.blocks.empty()) {
            for (auto& b : out.blocks) { if (!out.translatedText.empty()) out.translatedText += L"\r\n"; out.translatedText += b.translation; }
        }
        if (out.translatedText.empty()) { out.error = L"Gemini 没有识别到可翻译文字。"; return out; }
        out.ok = true; return out;
    }

    inline TranslationResult translateOcrBlocks(const std::vector<OcrBlock>& sourceBlocks,
        const std::wstring& apiKey, const std::wstring& model,
        const std::wstring& targetLanguage = L"Simplified Chinese")
    {
        TranslationResult out;
        if (sourceBlocks.empty()) { out.error = L"没有可翻译的 OCR 文字块。"; return out; }
        for (const auto& b : sourceBlocks) {
            if (!out.sourceText.empty()) out.sourceText += L"\r\n";
            out.sourceText += b.source;
        }
        JsonArray src;
        for (uint32_t i = 0; i < sourceBlocks.size(); ++i) {
            JsonObject item;
            item.SetNamedValue(L"id", JsonValue::CreateNumberValue(i));
            item.SetNamedValue(L"text", JsonValue::CreateStringValue(sourceBlocks[i].source));
            src.Append(item);
        }
        const auto modelId = model.empty() ? std::wstring(L"gemini-3.7-flash") : model;
        std::wstring prompt = L"Translate the following OCR blocks into " + targetLanguage +
            L". Return one item for every id, preserving id. Keep UI text concise. No commentary. Source blocks JSON: " +
            std::wstring{ src.Stringify().c_str() };
        auto req = makeBaseRequest(modelId, prompt, nullptr, makeTextTranslationSchema(), 8192);
        auto http = postGenerate(apiKey, modelId, req.Stringify().c_str());
        if (!http.error.empty()) { out.error = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"Gemini 翻译失败（HTTP {}）：{}", http.status, getApiError(http.body)); return out;
        }
        auto text = extractGenerateText(http.body);
        JsonObject payload{ nullptr };
        if (text.empty() || !JsonObject::TryParse(text, payload)) { out.error = L"Gemini 翻译返回格式无法解析。"; return out; }
        out.sourceText = std::wstring{ payload.GetNamedString(L"source_text", L"") };
        out.translatedText = std::wstring{ payload.GetNamedString(L"translated_text", L"") };
        auto items = payload.GetNamedArray(L"items", nullptr);
        if (items) {
            for (uint32_t i = 0; i < items.Size(); ++i) {
                auto item = items.GetObjectAt(i);
                int id = (int)std::lround(item.GetNamedNumber(L"id", -1));
                auto tr = std::wstring{ item.GetNamedString(L"translation", L"") };
                if (id < 0 || id >= (int)sourceBlocks.size() || tr.empty()) continue;
                TranslationBlock b;
                b.ymin = sourceBlocks[id].ymin; b.xmin = sourceBlocks[id].xmin;
                b.ymax = sourceBlocks[id].ymax; b.xmax = sourceBlocks[id].xmax;
                b.source = sourceBlocks[id].source;
                b.translation = std::move(tr);
                b.role = L"body";
                b.sourceLines = 1;
                for (wchar_t ch : sourceBlocks[id].source) if (ch == L'\n') ++b.sourceLines;
                out.blocks.push_back(std::move(b));
            }
        }
        if (out.translatedText.empty() && !out.blocks.empty()) {
            for (auto& b : out.blocks) { if (!out.translatedText.empty()) out.translatedText += L"\r\n"; out.translatedText += b.translation; }
        }
        if (out.translatedText.empty()) { out.error = L"Gemini 没有返回译文。"; return out; }
        out.ok = true; return out;
    }
}









