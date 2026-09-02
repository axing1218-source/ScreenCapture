#pragma once

#include <Windows.h>
#include <winhttp.h>
#include <algorithm>
#include <format>
#include <string>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include "GeminiClient.h"

// Provider-neutral AI facade for StarCap.
//
// The current renderer was originally built around GeminiClient result structs.
// Keep those proven geometry/result types as aliases during the v0.9.8 provider
// migration, while all network/provider selection goes through AIClient.
namespace AIClient
{
    using namespace winrt::Windows::Data::Json;
    using OcrBlock = GeminiClient::OcrBlock;
    using OcrResult = GeminiClient::OcrResult;
    using TranslationBlock = GeminiClient::TranslationBlock;
    using TranslationResult = GeminiClient::TranslationResult;
    using TestResult = GeminiClient::TestResult;

    inline constexpr std::wstring_view ProviderGemini{ L"gemini" };
    inline constexpr std::wstring_view ProviderOpenAI{ L"openai" };
    inline constexpr std::wstring_view ProviderAnthropic{ L"anthropic" };
    inline constexpr std::wstring_view ProviderDeepSeek{ L"deepseek" };

    struct ProviderInfo
    {
        std::wstring id;
        std::wstring name;
    };

    struct ModelsResult
    {
        bool ok{ false };
        std::vector<std::wstring> models;
        std::wstring error;
    };

    struct HttpResult
    {
        DWORD status{ 0 };
        std::string body;
        std::wstring error;
    };

    inline const std::vector<ProviderInfo>& providers()
    {
        static const std::vector<ProviderInfo> values{
            { std::wstring(ProviderGemini), L"Google Gemini" },
            { std::wstring(ProviderOpenAI), L"OpenAI" },
            { std::wstring(ProviderAnthropic), L"Anthropic Claude" },
            { std::wstring(ProviderDeepSeek), L"DeepSeek" },
        };
        return values;
    }

    inline std::wstring normalizeProvider(std::wstring provider)
    {
        std::transform(provider.begin(), provider.end(), provider.begin(), towlower);
        for (const auto& item : providers()) if (item.id == provider) return provider;
        return std::wstring(ProviderGemini);
    }

    inline std::wstring providerName(const std::wstring& provider)
    {
        const auto id = normalizeProvider(provider);
        for (const auto& item : providers()) if (item.id == id) return item.name;
        return L"Google Gemini";
    }

    inline std::vector<std::wstring> builtInModels(const std::wstring& provider)
    {
        const auto id = normalizeProvider(provider);
        if (id == ProviderOpenAI) {
            return { L"gpt-5.6-luna", L"gpt-5.6-terra", L"gpt-5.6-sol" };
        }
        if (id == ProviderAnthropic) {
            return { L"claude-sonnet-5", L"claude-haiku-4-5-20251001", L"claude-opus-5" };
        }
        if (id == ProviderDeepSeek) {
            // Screenshot OCR/translation requires a vision-capable model. Text-only
            // DeepSeek models are intentionally not offered in this v0.9.8 UI yet.
            return { L"deepseek-v4-flash-vision-exp" };
        }
        return { L"gemini-3.7-flash", L"gemini-3.6-flash", L"gemini-3.5-flash", L"gemini-3.5-flash-lite" };
    }

    inline std::wstring defaultModel(const std::wstring& provider)
    {
        const auto models = builtInModels(provider);
        return models.empty() ? L"" : models.front();
    }

    inline bool isSupportedScreenshotModel(const std::wstring& provider, const std::wstring& model)
    {
        const auto id = normalizeProvider(provider);
        if (id == ProviderOpenAI) return model.rfind(L"gpt-5.6", 0) == 0;
        if (id == ProviderAnthropic) return model.rfind(L"claude-", 0) == 0;
        if (id == ProviderDeepSeek) return model == L"deepseek-v4-flash-vision-exp";
        return model.rfind(L"gemini-", 0) == 0 &&
            model.find(L"embedding") == std::wstring::npos &&
            model.find(L"image") == std::wstring::npos &&
            model.find(L"tts") == std::wstring::npos;
    }

    inline HttpResult httpRequest(const wchar_t* host, const std::wstring& path,
        const wchar_t* method, const std::wstring& headers, const std::string& body = {})
    {
        HttpResult result;
        HINTERNET session = WinHttpOpen(L"StarCap/0.9.7", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { result.error = L"无法初始化网络连接。"; return result; }
        WinHttpSetTimeouts(session, 8000, 8000, 15000, 45000);
        HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) {
            result.error = std::format(L"无法连接 AI 服务（Windows 错误 {}）。", GetLastError());
            WinHttpCloseHandle(session); return result;
        }
        HINTERNET request = WinHttpOpenRequest(connect, method, path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request) {
            result.error = L"无法创建 AI 网络请求。";
            WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return result;
        }
        if (!headers.empty()) {
            WinHttpAddRequestHeaders(request, headers.c_str(), (DWORD)-1L,
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }
        BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
            (DWORD)body.size(), 0);
        if (sent) sent = WinHttpReceiveResponse(request, nullptr);
        if (!sent) {
            result.error = std::format(L"AI 网络请求失败（Windows 错误 {}）。", GetLastError());
        }
        else {
            DWORD statusSize = sizeof(result.status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                const auto old = result.body.size();
                result.body.resize(old + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, result.body.data() + old, available, &read)) {
                    result.body.resize(old); break;
                }
                result.body.resize(old + read);
            }
        }
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    inline std::wstring apiError(const std::string& response)
    {
        JsonObject root{ nullptr };
        const auto wide = GeminiClient::utf8ToWide(response);
        if (JsonObject::TryParse(wide, root)) {
            auto err = root.GetNamedObject(L"error", nullptr);
            if (err) {
                auto msg = std::wstring{ err.GetNamedString(L"message", L"") };
                if (!msg.empty()) return msg;
            }
            auto msg = std::wstring{ root.GetNamedString(L"message", L"") };
            if (!msg.empty()) return msg;
        }
        return wide.size() > 600 ? wide.substr(0, 600) + L"..." : wide;
    }

    inline JsonObject makeBoxSchema()
    {
        JsonObject integer; integer.SetNamedValue(L"type", JsonValue::CreateStringValue(L"integer"));
        JsonObject box;
        box.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        box.SetNamedValue(L"items", integer);
        box.SetNamedValue(L"minItems", JsonValue::CreateNumberValue(4));
        box.SetNamedValue(L"maxItems", JsonValue::CreateNumberValue(4));
        return box;
    }

    inline JsonObject makeOcrSchema()
    {
        JsonObject str; str.SetNamedValue(L"type", JsonValue::CreateStringValue(L"string"));
        JsonObject blockProps;
        blockProps.SetNamedValue(L"box_2d", makeBoxSchema());
        blockProps.SetNamedValue(L"text", str);
        JsonArray blockRequired;
        blockRequired.Append(JsonValue::CreateStringValue(L"box_2d"));
        blockRequired.Append(JsonValue::CreateStringValue(L"text"));
        JsonObject block;
        block.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        block.SetNamedValue(L"properties", blockProps);
        block.SetNamedValue(L"required", blockRequired);
        block.SetNamedValue(L"additionalProperties", JsonValue::CreateBooleanValue(false));
        JsonObject blocks;
        blocks.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        blocks.SetNamedValue(L"items", block);
        JsonObject props;
        props.SetNamedValue(L"full_text", str);
        props.SetNamedValue(L"blocks", blocks);
        JsonArray required;
        required.Append(JsonValue::CreateStringValue(L"full_text"));
        required.Append(JsonValue::CreateStringValue(L"blocks"));
        JsonObject schema;
        schema.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        schema.SetNamedValue(L"properties", props);
        schema.SetNamedValue(L"required", required);
        schema.SetNamedValue(L"additionalProperties", JsonValue::CreateBooleanValue(false));
        return schema;
    }

    inline JsonObject makeTranslationSchema()
    {
        JsonObject str; str.SetNamedValue(L"type", JsonValue::CreateStringValue(L"string"));
        JsonObject integer; integer.SetNamedValue(L"type", JsonValue::CreateStringValue(L"integer"));
        JsonObject blockProps;
        blockProps.SetNamedValue(L"box_2d", makeBoxSchema());
        blockProps.SetNamedValue(L"source", str);
        blockProps.SetNamedValue(L"translation", str);
        blockProps.SetNamedValue(L"role", str);
        blockProps.SetNamedValue(L"source_lines", integer);
        JsonArray blockRequired;
        for (auto key : { L"box_2d", L"source", L"translation", L"role", L"source_lines" })
            blockRequired.Append(JsonValue::CreateStringValue(key));
        JsonObject block;
        block.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        block.SetNamedValue(L"properties", blockProps);
        block.SetNamedValue(L"required", blockRequired);
        block.SetNamedValue(L"additionalProperties", JsonValue::CreateBooleanValue(false));
        JsonObject blocks;
        blocks.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        blocks.SetNamedValue(L"items", block);
        JsonObject props;
        props.SetNamedValue(L"source_text", str);
        props.SetNamedValue(L"translated_text", str);
        props.SetNamedValue(L"blocks", blocks);
        JsonArray required;
        required.Append(JsonValue::CreateStringValue(L"source_text"));
        required.Append(JsonValue::CreateStringValue(L"translated_text"));
        required.Append(JsonValue::CreateStringValue(L"blocks"));
        JsonObject schema;
        schema.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        schema.SetNamedValue(L"properties", props);
        schema.SetNamedValue(L"required", required);
        schema.SetNamedValue(L"additionalProperties", JsonValue::CreateBooleanValue(false));
        return schema;
    }

    inline JsonObject makeTextTranslationSchema()
    {
        JsonObject str; str.SetNamedValue(L"type", JsonValue::CreateStringValue(L"string"));
        JsonObject integer; integer.SetNamedValue(L"type", JsonValue::CreateStringValue(L"integer"));
        JsonObject itemProps;
        itemProps.SetNamedValue(L"id", integer);
        itemProps.SetNamedValue(L"translation", str);
        JsonArray itemRequired;
        itemRequired.Append(JsonValue::CreateStringValue(L"id"));
        itemRequired.Append(JsonValue::CreateStringValue(L"translation"));
        JsonObject item;
        item.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        item.SetNamedValue(L"properties", itemProps);
        item.SetNamedValue(L"required", itemRequired);
        item.SetNamedValue(L"additionalProperties", JsonValue::CreateBooleanValue(false));
        JsonObject items;
        items.SetNamedValue(L"type", JsonValue::CreateStringValue(L"array"));
        items.SetNamedValue(L"items", item);
        JsonObject props;
        props.SetNamedValue(L"source_text", str);
        props.SetNamedValue(L"translated_text", str);
        props.SetNamedValue(L"items", items);
        JsonArray required;
        required.Append(JsonValue::CreateStringValue(L"source_text"));
        required.Append(JsonValue::CreateStringValue(L"translated_text"));
        required.Append(JsonValue::CreateStringValue(L"items"));
        JsonObject schema;
        schema.SetNamedValue(L"type", JsonValue::CreateStringValue(L"object"));
        schema.SetNamedValue(L"properties", props);
        schema.SetNamedValue(L"required", required);
        schema.SetNamedValue(L"additionalProperties", JsonValue::CreateBooleanValue(false));
        return schema;
    }

    inline std::wstring extractResponsesText(const std::string& response)
    {
        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(GeminiClient::utf8ToWide(response), root)) return {};
        auto output = root.GetNamedArray(L"output", nullptr);
        if (!output) return {};
        for (uint32_t i = 0; i < output.Size(); ++i) {
            auto item = output.GetObjectAt(i);
            auto content = item.GetNamedArray(L"content", nullptr);
            if (!content) continue;
            for (uint32_t j = 0; j < content.Size(); ++j) {
                auto part = content.GetObjectAt(j);
                auto text = std::wstring{ part.GetNamedString(L"text", L"") };
                if (!text.empty()) return text;
            }
        }
        return {};
    }

    inline std::wstring extractAnthropicText(const std::string& response)
    {
        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(GeminiClient::utf8ToWide(response), root)) return {};
        auto content = root.GetNamedArray(L"content", nullptr);
        if (!content) return {};
        for (uint32_t i = 0; i < content.Size(); ++i) {
            auto part = content.GetObjectAt(i);
            auto text = std::wstring{ part.GetNamedString(L"text", L"") };
            if (!text.empty()) return text;
        }
        return {};
    }

    inline JsonObject makeResponsesRequest(const std::wstring& provider, const std::wstring& model,
        const std::wstring& prompt, const std::vector<BYTE>* png, JsonObject schema,
        const std::wstring& schemaName, int maxTokens)
    {
        JsonArray content;
        JsonObject textPart;
        textPart.SetNamedValue(L"type", JsonValue::CreateStringValue(L"input_text"));
        textPart.SetNamedValue(L"text", JsonValue::CreateStringValue(prompt));
        content.Append(textPart);
        if (png && !png->empty()) {
            const auto encoded = GeminiClient::base64Encode(png->data(), png->size());
            JsonObject imagePart;
            imagePart.SetNamedValue(L"type", JsonValue::CreateStringValue(L"input_image"));
            imagePart.SetNamedValue(L"image_url", JsonValue::CreateStringValue(
                L"data:image/png;base64," + GeminiClient::utf8ToWide(encoded)));
            imagePart.SetNamedValue(L"detail", JsonValue::CreateStringValue(L"high"));
            content.Append(imagePart);
        }
        JsonObject message;
        message.SetNamedValue(L"role", JsonValue::CreateStringValue(L"user"));
        message.SetNamedValue(L"content", content);
        JsonArray input; input.Append(message);

        JsonObject format;
        format.SetNamedValue(L"type", JsonValue::CreateStringValue(L"json_schema"));
        format.SetNamedValue(L"name", JsonValue::CreateStringValue(schemaName));
        format.SetNamedValue(L"schema", schema);
        if (normalizeProvider(provider) == ProviderOpenAI)
            format.SetNamedValue(L"strict", JsonValue::CreateBooleanValue(true));
        JsonObject text;
        text.SetNamedValue(L"format", format);

        JsonObject root;
        root.SetNamedValue(L"model", JsonValue::CreateStringValue(model));
        root.SetNamedValue(L"input", input);
        root.SetNamedValue(L"text", text);
        root.SetNamedValue(L"max_output_tokens", JsonValue::CreateNumberValue(maxTokens));
        if (normalizeProvider(provider) == ProviderOpenAI && model.rfind(L"gpt-5.6", 0) == 0) {
            JsonObject reasoning;
            reasoning.SetNamedValue(L"effort", JsonValue::CreateStringValue(L"none"));
            root.SetNamedValue(L"reasoning", reasoning);
        }
        return root;
    }

    inline JsonObject makeAnthropicRequest(const std::wstring& model, const std::wstring& prompt,
        const std::vector<BYTE>* png, JsonObject schema, int maxTokens)
    {
        JsonArray content;
        if (png && !png->empty()) {
            JsonObject source;
            source.SetNamedValue(L"type", JsonValue::CreateStringValue(L"base64"));
            source.SetNamedValue(L"media_type", JsonValue::CreateStringValue(L"image/png"));
            source.SetNamedValue(L"data", JsonValue::CreateStringValue(
                GeminiClient::utf8ToWide(GeminiClient::base64Encode(png->data(), png->size()))));
            JsonObject image;
            image.SetNamedValue(L"type", JsonValue::CreateStringValue(L"image"));
            image.SetNamedValue(L"source", source);
            content.Append(image);
        }
        JsonObject text;
        text.SetNamedValue(L"type", JsonValue::CreateStringValue(L"text"));
        text.SetNamedValue(L"text", JsonValue::CreateStringValue(prompt));
        content.Append(text);
        JsonObject message;
        message.SetNamedValue(L"role", JsonValue::CreateStringValue(L"user"));
        message.SetNamedValue(L"content", content);
        JsonArray messages; messages.Append(message);

        JsonObject format;
        format.SetNamedValue(L"type", JsonValue::CreateStringValue(L"json_schema"));
        format.SetNamedValue(L"schema", schema);
        JsonObject outputConfig;
        outputConfig.SetNamedValue(L"format", format);

        JsonObject root;
        root.SetNamedValue(L"model", JsonValue::CreateStringValue(model));
        root.SetNamedValue(L"max_tokens", JsonValue::CreateNumberValue(maxTokens));
        root.SetNamedValue(L"messages", messages);
        root.SetNamedValue(L"output_config", outputConfig);
        return root;
    }

    inline HttpResult providerPost(const std::wstring& provider, const std::wstring& apiKey,
        const std::wstring& body)
    {
        const auto id = normalizeProvider(provider);
        const auto utf8 = GeminiClient::wideToUtf8(body);
        if (id == ProviderOpenAI) {
            return httpRequest(L"api.openai.com", L"/v1/responses", L"POST",
                L"Content-Type: application/json\r\nAuthorization: Bearer " + apiKey + L"\r\n", utf8);
        }
        if (id == ProviderDeepSeek) {
            return httpRequest(L"api.deepseek.com", L"/responses", L"POST",
                L"Content-Type: application/json\r\nAuthorization: Bearer " + apiKey + L"\r\n", utf8);
        }
        return httpRequest(L"api.anthropic.com", L"/v1/messages", L"POST",
            L"Content-Type: application/json\r\nx-api-key: " + apiKey +
            L"\r\nanthropic-version: 2023-06-01\r\n", utf8);
    }

    inline std::wstring providerStructuredText(const std::wstring& provider, const HttpResult& http)
    {
        return normalizeProvider(provider) == ProviderAnthropic
            ? extractAnthropicText(http.body) : extractResponsesText(http.body);
    }

    inline ModelsResult listModels(const std::wstring& provider, const std::wstring& apiKey)
    {
        ModelsResult out;
        const auto id = normalizeProvider(provider);
        if (apiKey.empty()) { out.error = L"请先填写 API Key。"; return out; }
        HttpResult http;
        if (id == ProviderGemini) {
            http = httpRequest(L"generativelanguage.googleapis.com", L"/v1beta/models?pageSize=1000", L"GET",
                L"x-goog-api-key: " + apiKey + L"\r\n");
        }
        else if (id == ProviderOpenAI) {
            http = httpRequest(L"api.openai.com", L"/v1/models", L"GET",
                L"Authorization: Bearer " + apiKey + L"\r\n");
        }
        else if (id == ProviderAnthropic) {
            http = httpRequest(L"api.anthropic.com", L"/v1/models?limit=100", L"GET",
                L"x-api-key: " + apiKey + L"\r\nanthropic-version: 2023-06-01\r\n");
        }
        else {
            http = httpRequest(L"api.deepseek.com", L"/models", L"GET",
                L"Authorization: Bearer " + apiKey + L"\r\n");
        }
        if (!http.error.empty()) { out.error = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"获取模型列表失败（HTTP {}）：{}", http.status, apiError(http.body));
            return out;
        }
        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(GeminiClient::utf8ToWide(http.body), root)) {
            out.error = L"模型列表返回格式无法解析。"; return out;
        }
        auto add = [&](std::wstring model) {
            if (model.rfind(L"models/", 0) == 0) model.erase(0, 7);
            if (!isSupportedScreenshotModel(id, model)) return;
            if (std::find(out.models.begin(), out.models.end(), model) == out.models.end())
                out.models.push_back(std::move(model));
        };
        if (id == ProviderGemini) {
            auto arr = root.GetNamedArray(L"models", nullptr);
            if (arr) for (uint32_t i = 0; i < arr.Size(); ++i)
                add(std::wstring{ arr.GetObjectAt(i).GetNamedString(L"name", L"") });
        }
        else {
            auto arr = root.GetNamedArray(L"data", nullptr);
            if (arr) for (uint32_t i = 0; i < arr.Size(); ++i) {
                auto item = arr.GetObjectAt(i);
                auto model = std::wstring{ item.GetNamedString(L"id", L"") };
                if (id == ProviderAnthropic) {
                    auto caps = item.GetNamedObject(L"capabilities", nullptr);
                    if (caps) {
                        auto image = caps.GetNamedObject(L"image_input", nullptr);
                        auto structured = caps.GetNamedObject(L"structured_outputs", nullptr);
                        if (image && !image.GetNamedBoolean(L"supported", false)) continue;
                        if (structured && !structured.GetNamedBoolean(L"supported", false)) continue;
                    }
                }
                add(std::move(model));
            }
        }
        // Keep curated defaults visible even if an account's model-list endpoint is
        // temporarily incomplete; append account-visible models after them.
        auto merged = builtInModels(id);
        for (auto& model : out.models)
            if (std::find(merged.begin(), merged.end(), model) == merged.end()) merged.push_back(model);
        out.models = std::move(merged);
        out.ok = true;
        return out;
    }

    inline TestResult testConnection(const std::wstring& provider, const std::wstring& apiKey,
        const std::wstring& model)
    {
        const auto id = normalizeProvider(provider);
        if (id == ProviderGemini) return GeminiClient::testConnection(apiKey, model);
        TestResult out;
        if (apiKey.empty()) { out.message = L"API Key 为空。"; return out; }
        const auto modelId = model.empty() ? defaultModel(id) : model;
        JsonObject root;
        if (id == ProviderAnthropic) {
            JsonObject msg;
            msg.SetNamedValue(L"role", JsonValue::CreateStringValue(L"user"));
            msg.SetNamedValue(L"content", JsonValue::CreateStringValue(L"Reply with exactly OK"));
            JsonArray messages; messages.Append(msg);
            root.SetNamedValue(L"model", JsonValue::CreateStringValue(modelId));
            root.SetNamedValue(L"max_tokens", JsonValue::CreateNumberValue(16));
            root.SetNamedValue(L"messages", messages);
        }
        else {
            root.SetNamedValue(L"model", JsonValue::CreateStringValue(modelId));
            root.SetNamedValue(L"input", JsonValue::CreateStringValue(L"Reply with exactly OK"));
            root.SetNamedValue(L"max_output_tokens", JsonValue::CreateNumberValue(32));
            if (id == ProviderOpenAI && modelId.rfind(L"gpt-5.6", 0) == 0) {
                JsonObject reasoning; reasoning.SetNamedValue(L"effort", JsonValue::CreateStringValue(L"none"));
                root.SetNamedValue(L"reasoning", reasoning);
            }
        }
        auto http = providerPost(id, apiKey, root.Stringify().c_str());
        if (!http.error.empty()) { out.message = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.message = std::format(L"连接失败（HTTP {}）：{}", http.status, apiError(http.body)); return out;
        }
        const auto text = id == ProviderAnthropic ? extractAnthropicText(http.body) : extractResponsesText(http.body);
        if (text.empty()) { out.message = providerName(id) + L" 已响应，但没有返回文本。"; return out; }
        out.ok = true; out.message = L"连接成功"; return out;
    }

    inline OcrResult recognizeImage(const std::wstring& provider,
        const std::vector<BYTE>& pixels, int width, int height,
        const std::wstring& apiKey, const std::wstring& model)
    {
        const auto id = normalizeProvider(provider);
        if (id == ProviderGemini) return GeminiClient::recognizeImage(pixels, width, height, apiKey, model);
        OcrResult out;
        if (apiKey.empty()) { out.error = L"API Key 为空。"; return out; }
        std::vector<BYTE> png;
        GeminiClient::ComScope com;
        if (!GeminiClient::encodePng(pixels, width, height, png)) { out.error = L"图片编码失败。"; return out; }
        if (id == ProviderAnthropic && png.size() > 7 * 1024 * 1024) {
            out.error = L"图片过大，超过 Claude 内联图片的安全大小限制。"; return out;
        }
        const auto modelId = model.empty() ? defaultModel(id) : model;
        if (!isSupportedScreenshotModel(id, modelId)) { out.error = L"当前模型不支持 StarCap 截图识别。"; return out; }
        const std::wstring prompt =
            L"OCR this screenshot. Extract every readable text exactly as shown. Return JSON with full_text in natural reading order and blocks. "
            L"Each block must contain the exact source text and box_2d=[ymin,xmin,ymax,xmax] normalized 0-1000. "
            L"Do not translate, summarize, correct spelling, or add commentary. Include small UI labels when readable.";
        JsonObject request = id == ProviderAnthropic
            ? makeAnthropicRequest(modelId, prompt, &png, makeOcrSchema(), 8192)
            : makeResponsesRequest(id, modelId, prompt, &png, makeOcrSchema(), L"starcap_ocr", 8192);
        auto http = providerPost(id, apiKey, request.Stringify().c_str());
        if (!http.error.empty()) { out.error = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"{} OCR 失败（HTTP {}）：{}", providerName(id), http.status, apiError(http.body)); return out;
        }
        auto text = providerStructuredText(id, http);
        JsonObject payload{ nullptr };
        if (text.empty() || !JsonObject::TryParse(text, payload)) { out.error = providerName(id) + L" OCR 返回格式无法解析。"; return out; }
        out.text = std::wstring{ payload.GetNamedString(L"full_text", L"") };
        auto blocks = payload.GetNamedArray(L"blocks", nullptr);
        if (blocks) for (uint32_t i = 0; i < blocks.Size(); ++i) {
            auto item = blocks.GetObjectAt(i);
            auto box = item.GetNamedArray(L"box_2d", nullptr);
            auto source = std::wstring{ item.GetNamedString(L"text", L"") };
            OcrBlock block;
            if (source.empty() || !GeminiClient::readBox(box, block.ymin, block.xmin, block.ymax, block.xmax)) continue;
            block.source = std::move(source); out.blocks.push_back(std::move(block));
        }
        if (out.text.empty() && !out.blocks.empty()) {
            for (auto& block : out.blocks) { if (!out.text.empty()) out.text += L"\r\n"; out.text += block.source; }
        }
        if (out.text.empty()) { out.error = providerName(id) + L" 没有识别到文字。"; return out; }
        out.ok = true; return out;
    }

    inline TranslationResult translateImage(const std::wstring& provider,
        const std::vector<BYTE>& pixels, int width, int height,
        const std::wstring& apiKey, const std::wstring& model,
        const std::wstring& targetLanguage = L"Simplified Chinese")
    {
        const auto id = normalizeProvider(provider);
        if (id == ProviderGemini) return GeminiClient::translateImage(pixels, width, height, apiKey, model, targetLanguage);
        TranslationResult out;
        if (apiKey.empty()) { out.error = L"API Key 为空。"; return out; }
        std::vector<BYTE> png;
        GeminiClient::ComScope com;
        if (!GeminiClient::encodePng(pixels, width, height, png)) { out.error = L"图片编码失败。"; return out; }
        if (id == ProviderAnthropic && png.size() > 7 * 1024 * 1024) {
            out.error = L"图片过大，超过 Claude 内联图片的安全大小限制。"; return out;
        }
        const auto modelId = model.empty() ? defaultModel(id) : model;
        if (!isSupportedScreenshotModel(id, modelId)) { out.error = L"当前模型不支持 StarCap 截图翻译。"; return out; }
        const std::wstring prompt =
            L"Read ALL readable text in this screenshot from top to bottom. Return JSON with source_text exactly as read and translated_text translated into " + targetLanguage +
            L". Preserve paragraph breaks. For blocks, group text by visual region: one title, heading, paragraph, caption, or short UI label per block. "
            L"Merge adjacent wrapped lines belonging to the same paragraph and never combine text across visible paragraph whitespace. "
            L"Each block must contain box_2d=[ymin,xmin,ymax,xmax] normalized to 0-1000, source copied exactly from that region, translation, "
            L"role as exactly one of title, heading, body, caption, label, and source_lines as the number of visible source lines. "
            L"Keep original reading order. Do not summarize or add commentary.";
        JsonObject request = id == ProviderAnthropic
            ? makeAnthropicRequest(modelId, prompt, &png, makeTranslationSchema(), 16384)
            : makeResponsesRequest(id, modelId, prompt, &png, makeTranslationSchema(), L"starcap_translation", 16384);
        auto http = providerPost(id, apiKey, request.Stringify().c_str());
        if (!http.error.empty()) { out.error = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"{} 翻译失败（HTTP {}）：{}", providerName(id), http.status, apiError(http.body)); return out;
        }
        auto text = providerStructuredText(id, http);
        JsonObject payload{ nullptr };
        if (text.empty() || !JsonObject::TryParse(text, payload)) { out.error = providerName(id) + L" 翻译返回格式无法解析。"; return out; }
        out.sourceText = std::wstring{ payload.GetNamedString(L"source_text", L"") };
        out.translatedText = std::wstring{ payload.GetNamedString(L"translated_text", L"") };
        auto blocks = payload.GetNamedArray(L"blocks", nullptr);
        if (blocks) for (uint32_t i = 0; i < blocks.Size(); ++i) {
            auto item = blocks.GetObjectAt(i);
            auto box = item.GetNamedArray(L"box_2d", nullptr);
            auto source = std::wstring{ item.GetNamedString(L"source", L"") };
            auto translated = std::wstring{ item.GetNamedString(L"translation", L"") };
            auto role = std::wstring{ item.GetNamedString(L"role", L"body") };
            const int lines = std::clamp((int)std::lround(item.GetNamedNumber(L"source_lines", 1)), 1, 100);
            TranslationBlock block;
            if (translated.empty() || !GeminiClient::readTranslationBox(box, width, height, source, lines,
                block.ymin, block.xmin, block.ymax, block.xmax)) continue;
            block.source = std::move(source); block.translation = std::move(translated);
            block.role = role.empty() ? L"body" : std::move(role); block.sourceLines = lines;
            out.blocks.push_back(std::move(block));
        }
        if (out.translatedText.empty() && !out.blocks.empty()) {
            for (auto& block : out.blocks) { if (!out.translatedText.empty()) out.translatedText += L"\r\n"; out.translatedText += block.translation; }
        }
        if (out.translatedText.empty()) { out.error = providerName(id) + L" 没有返回译文。"; return out; }
        out.ok = true; return out;
    }

    inline TranslationResult translateOcrBlocks(const std::wstring& provider,
        const std::vector<OcrBlock>& sourceBlocks, const std::wstring& apiKey,
        const std::wstring& model, const std::wstring& targetLanguage = L"Simplified Chinese")
    {
        const auto id = normalizeProvider(provider);
        if (id == ProviderGemini) return GeminiClient::translateOcrBlocks(sourceBlocks, apiKey, model, targetLanguage);
        TranslationResult out;
        if (sourceBlocks.empty()) { out.error = L"没有可翻译的 OCR 文字块。"; return out; }
        if (apiKey.empty()) { out.error = L"API Key 为空。"; return out; }
        JsonArray source;
        for (uint32_t i = 0; i < sourceBlocks.size(); ++i) {
            JsonObject item;
            item.SetNamedValue(L"id", JsonValue::CreateNumberValue(i));
            item.SetNamedValue(L"text", JsonValue::CreateStringValue(sourceBlocks[i].source));
            source.Append(item);
            if (!out.sourceText.empty()) out.sourceText += L"\r\n";
            out.sourceText += sourceBlocks[i].source;
        }
        const auto modelId = model.empty() ? defaultModel(id) : model;
        const std::wstring prompt = L"Translate every item in this JSON array into " + targetLanguage +
            L". Return JSON with source_text, translated_text, and one items entry for every id. Preserve ids. No commentary. Source blocks: " +
            std::wstring{ source.Stringify().c_str() };
        JsonObject request = id == ProviderAnthropic
            ? makeAnthropicRequest(modelId, prompt, nullptr, makeTextTranslationSchema(), 8192)
            : makeResponsesRequest(id, modelId, prompt, nullptr, makeTextTranslationSchema(), L"starcap_text_translation", 8192);
        auto http = providerPost(id, apiKey, request.Stringify().c_str());
        if (!http.error.empty()) { out.error = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"{} 翻译失败（HTTP {}）：{}", providerName(id), http.status, apiError(http.body)); return out;
        }
        auto text = providerStructuredText(id, http);
        JsonObject payload{ nullptr };
        if (text.empty() || !JsonObject::TryParse(text, payload)) { out.error = providerName(id) + L" 翻译返回格式无法解析。"; return out; }
        out.sourceText = std::wstring{ payload.GetNamedString(L"source_text", out.sourceText) };
        out.translatedText = std::wstring{ payload.GetNamedString(L"translated_text", L"") };
        auto items = payload.GetNamedArray(L"items", nullptr);
        if (items) for (uint32_t i = 0; i < items.Size(); ++i) {
            auto item = items.GetObjectAt(i);
            const int index = (int)std::lround(item.GetNamedNumber(L"id", -1));
            auto translated = std::wstring{ item.GetNamedString(L"translation", L"") };
            if (index < 0 || index >= (int)sourceBlocks.size() || translated.empty()) continue;
            TranslationBlock block;
            block.ymin = sourceBlocks[index].ymin; block.xmin = sourceBlocks[index].xmin;
            block.ymax = sourceBlocks[index].ymax; block.xmax = sourceBlocks[index].xmax;
            block.source = sourceBlocks[index].source; block.translation = std::move(translated);
            block.role = L"body"; block.sourceLines = 1;
            for (wchar_t ch : block.source) if (ch == L'\n') ++block.sourceLines;
            out.blocks.push_back(std::move(block));
        }
        if (out.translatedText.empty() && !out.blocks.empty()) {
            for (auto& block : out.blocks) { if (!out.translatedText.empty()) out.translatedText += L"\r\n"; out.translatedText += block.translation; }
        }
        if (out.translatedText.empty()) { out.error = providerName(id) + L" 没有返回译文。"; return out; }
        out.ok = true; return out;
    }
}
