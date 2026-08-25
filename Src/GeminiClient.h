#pragma once

#include <Windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <objidl.h>
#include <wrl.h>
#include <algorithm>
#include <string>
#include <vector>
#include <winrt/Windows.Data.Json.h>

namespace GeminiClient
{
    using Microsoft::WRL::ComPtr;
    using namespace winrt::Windows::Data::Json;

    struct TranslationBlock
    {
        int ymin{ 0 }, xmin{ 0 }, ymax{ 0 }, xmax{ 0 }; // normalized 0..1000
        std::wstring translation;
    };

    struct TranslationResult
    {
        bool ok{ false };
        std::wstring translatedText;
        std::vector<TranslationBlock> blocks;
        std::wstring error;
    };

    struct TestResult
    {
        bool ok{ false };
        std::wstring message;
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
        if (len <= 0) {
            len = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0);
            if (len <= 0) return {};
        }
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
        auto hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr)) return false;

        ComPtr<IWICBitmapEncoder> encoder;
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
        if (FAILED(hr)) return false;
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) return false;

        ComPtr<IWICBitmapFrameEncode> frame;
        hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
        if (FAILED(hr)) return false;
        hr = frame->Initialize(nullptr);
        if (FAILED(hr)) return false;
        hr = frame->SetSize((UINT)width, (UINT)height);
        if (FAILED(hr)) return false;
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&fmt);
        if (FAILED(hr) || !IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA)) return false;
        const UINT rowBytes = (UINT)width * 4;
        hr = frame->WritePixels((UINT)height, rowBytes, rowBytes * (UINT)height,
            const_cast<BYTE*>(pixels.data()));
        if (FAILED(hr)) return false;
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

    inline HttpResult postJson(const std::wstring& apiKey, const std::wstring& json)
    {
        HttpResult result;
        if (apiKey.empty()) {
            result.error = L"Gemini API Key 为空。";
            return result;
        }

        HINTERNET session = WinHttpOpen(L"WeShot/0.7", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            result.error = L"无法初始化网络连接。";
            return result;
        }
        WinHttpSetTimeouts(session, 10000, 10000, 30000, 120000);

        HINTERNET connect = WinHttpConnect(session, L"generativelanguage.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) {
            WinHttpCloseHandle(session);
            result.error = L"无法连接 Gemini API。";
            return result;
        }

        HINTERNET request = WinHttpOpenRequest(connect, L"POST", L"/v1beta/interactions",
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request) {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            result.error = L"无法创建 Gemini 请求。";
            return result;
        }

        std::wstring headers = L"Content-Type: application/json\r\n";
        headers += L"x-goog-api-key: " + apiKey + L"\r\n";
        headers += L"Api-Revision: 2026-05-20\r\n";
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
                    result.body.resize(oldSize);
                    break;
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
        if (wide.size() > 400) return wide.substr(0, 400) + L"...";
        return wide;
    }

    inline std::wstring extractOutputText(const std::string& response)
    {
        JsonObject root{ nullptr };
        auto wide = utf8ToWide(response);
        if (!JsonObject::TryParse(wide, root)) return {};

        auto direct = std::wstring{ root.GetNamedString(L"output_text", L"") };
        if (!direct.empty()) return direct;

        auto steps = root.GetNamedArray(L"steps", nullptr);
        if (!steps) return {};
        for (uint32_t i = 0; i < steps.Size(); ++i) {
            auto step = steps.GetObjectAt(i);
            if (std::wstring{ step.GetNamedString(L"type", L"") } != L"model_output") continue;
            auto content = step.GetNamedArray(L"content", nullptr);
            if (!content) continue;
            for (uint32_t j = 0; j < content.Size(); ++j) {
                auto item = content.GetObjectAt(j);
                if (std::wstring{ item.GetNamedString(L"type", L"") } != L"text") continue;
                auto text = std::wstring{ item.GetNamedString(L"text", L"") };
                if (!text.empty()) return text;
            }
        }
        return {};
    }

    inline JsonObject makeTranslationSchema()
    {
        JsonObject integerType;
        integerType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"integer"));

        JsonObject boxArray;
        boxArray.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        boxArray.SetNamedValue(L"items", integerType);
        boxArray.SetNamedValue(L"description", JsonValue::CreateStringValue(
            L"[ymin, xmin, ymax, xmax], normalized to 0-1000"));

        JsonObject translationType;
        translationType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"string"));

        JsonObject blockProps;
        blockProps.SetNamedValue(L"box_2d", boxArray);
        blockProps.SetNamedValue(L"translation", translationType);

        JsonArray blockRequired;
        blockRequired.Append(JsonValue::CreateStringValue(L"box_2d"));
        blockRequired.Append(JsonValue::CreateStringValue(L"translation"));

        JsonObject blockItem;
        blockItem.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        blockItem.SetNamedValue(L"properties", blockProps);
        blockItem.SetNamedValue(L"required", blockRequired);

        JsonObject blocksArray;
        blocksArray.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        blocksArray.SetNamedValue(L"items", blockItem);

        JsonObject translatedType;
        translatedType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"string"));

        JsonObject props;
        props.SetNamedValue(L"translated_text", translatedType);
        props.SetNamedValue(L"blocks", blocksArray);

        JsonArray required;
        required.Append(JsonValue::CreateStringValue(L"translated_text"));
        required.Append(JsonValue::CreateStringValue(L"blocks"));

        JsonObject schema;
        schema.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        schema.SetNamedValue(L"properties", props);
        schema.SetNamedValue(L"required", required);
        return schema;
    }

    inline TestResult testConnection(const std::wstring& apiKey, const std::wstring& model)
    {
        TestResult out;
        JsonObject root;
        root.SetNamedValue(L"model", JsonValue::CreateStringValue(model.empty() ? L"gemini-3.7-flash" : model));
        JsonArray input;
        JsonObject text;
        text.SetNamedValue(L"type", JsonValue::CreateStringValue(L"text"));
        text.SetNamedValue(L"text", JsonValue::CreateStringValue(L"Reply with exactly: OK"));
        input.Append(text);
        root.SetNamedValue(L"input", input);

        auto http = postJson(apiKey, root.Stringify().c_str());
        if (!http.error.empty()) {
            out.message = http.error;
            return out;
        }
        if (http.status < 200 || http.status >= 300) {
            out.message = std::format(L"连接失败（HTTP {}）：{}", http.status, getApiError(http.body));
            return out;
        }
        auto textOut = extractOutputText(http.body);
        if (textOut.empty()) {
            out.message = L"Gemini 已响应，但没有返回文本。";
            return out;
        }
        out.ok = true;
        out.message = L"连接成功";
        return out;
    }

    inline TranslationResult translateImage(const std::vector<BYTE>& pixels, int width, int height,
        const std::wstring& apiKey, const std::wstring& model,
        const std::wstring& targetLanguage = L"Simplified Chinese")
    {
        TranslationResult out;
        std::vector<BYTE> png;
        if (!encodePng(pixels, width, height, png)) {
            out.error = L"图片编码失败。";
            return out;
        }
        // Gemini inline media has a 20 MB total request limit. Leave headroom for base64 + prompt.
        if (png.size() > 14 * 1024 * 1024) {
            out.error = L"图片过大，当前测试版暂时无法一次发送。长截图分块会在下一版处理。";
            return out;
        }

        const auto b64 = base64Encode(png.data(), png.size());

        JsonObject root;
        root.SetNamedValue(L"model", JsonValue::CreateStringValue(model.empty() ? L"gemini-3.7-flash" : model));

        JsonArray input;
        JsonObject image;
        image.SetNamedValue(L"type", JsonValue::CreateStringValue(L"image"));
        image.SetNamedValue(L"mime_type", JsonValue::CreateStringValue(L"image/png"));
        image.SetNamedValue(L"data", JsonValue::CreateStringValue(utf8ToWide(b64)));
        input.Append(image);

        JsonObject prompt;
        prompt.SetNamedValue(L"type", JsonValue::CreateStringValue(L"text"));
        std::wstring promptText =
            L"Translate every readable text region in this screenshot into " + targetLanguage +
            L". Return translated_text in natural reading order. Also return one block for every readable text region. "
            L"Each block must contain only its translation and box_2d as [ymin, xmin, ymax, xmax] normalized to 0-1000. "
            L"Do not return source/original text. Preserve numbers, URLs, product names and proper nouns when appropriate. "
            L"Do not omit small UI labels or buttons. Keep translations concise enough to fit the original region.";
        prompt.SetNamedValue(L"text", JsonValue::CreateStringValue(promptText));
        input.Append(prompt);
        root.SetNamedValue(L"input", input);

        JsonObject responseFormat;
        responseFormat.SetNamedValue(L"type", JsonValue::CreateStringValue(L"text"));
        responseFormat.SetNamedValue(L"mime_type", JsonValue::CreateStringValue(L"application/json"));
        responseFormat.SetNamedValue(L"schema", makeTranslationSchema());
        root.SetNamedValue(L"response_format", responseFormat);

        auto http = postJson(apiKey, root.Stringify().c_str());
        if (!http.error.empty()) {
            out.error = http.error;
            return out;
        }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"Gemini 翻译失败（HTTP {}）：{}", http.status, getApiError(http.body));
            return out;
        }

        auto outputText = extractOutputText(http.body);
        if (outputText.empty()) {
            out.error = L"Gemini 没有返回翻译结果。";
            return out;
        }

        JsonObject payload{ nullptr };
        if (!JsonObject::TryParse(outputText, payload)) {
            out.error = L"Gemini 返回的翻译数据格式无法解析。";
            return out;
        }

        out.translatedText = std::wstring{ payload.GetNamedString(L"translated_text", L"") };
        auto blocks = payload.GetNamedArray(L"blocks", nullptr);
        if (blocks) {
            for (uint32_t i = 0; i < blocks.Size(); ++i) {
                auto item = blocks.GetObjectAt(i);
                auto box = item.GetNamedArray(L"box_2d", nullptr);
                auto translation = std::wstring{ item.GetNamedString(L"translation", L"") };
                if (!box || box.Size() < 4 || translation.empty()) continue;
                TranslationBlock block;
                block.ymin = std::clamp((int)std::lround(box.GetNumberAt(0)), 0, 1000);
                block.xmin = std::clamp((int)std::lround(box.GetNumberAt(1)), 0, 1000);
                block.ymax = std::clamp((int)std::lround(box.GetNumberAt(2)), 0, 1000);
                block.xmax = std::clamp((int)std::lround(box.GetNumberAt(3)), 0, 1000);
                if (block.ymax <= block.ymin || block.xmax <= block.xmin) continue;
                block.translation = std::move(translation);
                out.blocks.push_back(std::move(block));
            }
        }

        if (out.translatedText.empty() && !out.blocks.empty()) {
            for (auto& b : out.blocks) {
                if (!out.translatedText.empty()) out.translatedText += L"\r\n";
                out.translatedText += b.translation;
            }
        }
        if (out.translatedText.empty()) {
            out.error = L"Gemini 没有识别到可翻译的文字。";
            return out;
        }
        out.ok = true;
        return out;
    }
}
