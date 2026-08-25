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
        HINTERNET session = WinHttpOpen(L"WeShot/0.8", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
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
        JsonObject blockProps;
        blockProps.SetNamedValue(L"box_2d", makeBoxSchema());
        blockProps.SetNamedValue(L"translation", strType);
        JsonArray blockReq;
        blockReq.Append(JsonValue::CreateStringValue(L"box_2d"));
        blockReq.Append(JsonValue::CreateStringValue(L"translation"));
        JsonObject block;
        block.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        block.SetNamedValue(L"properties", blockProps);
        block.SetNamedValue(L"required", blockReq);
        JsonObject blocks;
        blocks.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        blocks.SetNamedValue(L"items", block);
        JsonObject props;
        props.SetNamedValue(L"translated_text", strType);
        props.SetNamedValue(L"blocks", blocks);
        JsonArray req;
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
            L"Translate every readable text region in this screenshot into " + targetLanguage +
            L". Return translated_text in natural reading order and blocks. Each block contains only translation and "
            L"box_2d=[ymin,xmin,ymax,xmax] normalized 0-1000. Do not generate or edit an image. Do not add commentary. "
            L"Keep UI translations concise enough to fit their original regions.",
            &png, makeTranslationSchema(), 8192);
        auto http = postGenerate(apiKey, modelId, req.Stringify().c_str());
        if (!http.error.empty()) { out.error = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"Gemini 翻译失败（HTTP {}）：{}", http.status, getApiError(http.body)); return out;
        }
        auto text = extractGenerateText(http.body);
        JsonObject payload{ nullptr };
        if (text.empty() || !JsonObject::TryParse(text, payload)) { out.error = L"Gemini 翻译返回格式无法解析。"; return out; }
        out.translatedText = std::wstring{ payload.GetNamedString(L"translated_text", L"") };
        auto blocks = payload.GetNamedArray(L"blocks", nullptr);
        if (blocks) {
            for (uint32_t i = 0; i < blocks.Size(); ++i) {
                auto item = blocks.GetObjectAt(i);
                auto box = item.GetNamedArray(L"box_2d", nullptr);
                auto tr = std::wstring{ item.GetNamedString(L"translation", L"") };
                TranslationBlock b;
                if (tr.empty() || !readBox(box, b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.translation = std::move(tr); out.blocks.push_back(std::move(b));
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
                b.translation = std::move(tr); out.blocks.push_back(std::move(b));
            }
        }
        if (out.translatedText.empty() && !out.blocks.empty()) {
            for (auto& b : out.blocks) { if (!out.translatedText.empty()) out.translatedText += L"\r\n"; out.translatedText += b.translation; }
        }
        if (out.translatedText.empty()) { out.error = L"Gemini 没有返回译文。"; return out; }
        out.ok = true; return out;
    }
}
