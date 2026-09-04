#pragma once

#include "GeminiClient.h"

namespace GeminiClient
{
    // Selection-oriented OCR variant used only by the OCR linked-selection test branch.
    // It preserves the normal OCR text result, but asks Gemini for much smaller geometry
    // spans so image hit-testing does not have to guess character positions inside a
    // paragraph/whole-line rectangle.
    inline OcrResult recognizeImageSelection(const std::vector<BYTE>& pixels, int width, int height,
        const std::wstring& apiKey, const std::wstring& model)
    {
        OcrResult out;
        ComScope com;
        std::vector<BYTE> png;
        if (!encodePng(pixels, width, height, png)) { out.error = L"图片编码失败。"; return out; }
        if (png.size() > 14 * 1024 * 1024) { out.error = L"图片过大，长截图分块识别将在后续版本处理。"; return out; }

        const auto modelId = model.empty() ? std::wstring(L"gemini-3.7-flash") : model;
        auto req = makeBaseRequest(modelId,
            L"OCR this screenshot. Extract every readable text exactly as shown. "
            L"Return full_text in natural reading order and blocks for precise mouse text selection. "
            L"IMPORTANT: blocks must be fine-grained visual text spans, NOT whole paragraphs and NOT whole long lines. "
            L"For Chinese/Japanese/Korean text, return one visible CJK character per block whenever practical. "
            L"For Latin/Cyrillic words, numbers, URLs, shortcuts, and identifiers, return one visual word/number/token per block. "
            L"Keep punctuation as its own small block when visually separable; otherwise attach it only to the nearest token. "
            L"Each block.text must be an EXACT substring of full_text and blocks must appear in the SAME reading order as full_text. "
            L"Each block must contain the tight visible box_2d=[ymin,xmin,ymax,xmax] normalized 0-1000 around only that span. "
            L"Do not include surrounding blank space, bullet indentation, or neighboring words in a block box. "
            L"Do not translate, summarize, correct spelling, normalize punctuation, or add commentary. Include small UI labels when readable.",
            &png, makeOcrSchema(), 8192);

        auto http = postGenerate(apiKey, modelId, req.Stringify().c_str());
        if (!http.error.empty()) { out.error = http.error; return out; }
        if (http.status < 200 || http.status >= 300) {
            out.error = std::format(L"Gemini OCR 失败（HTTP {}）：{}", http.status, getApiError(http.body));
            return out;
        }

        auto text = extractGenerateText(http.body);
        JsonObject payload{ nullptr };
        if (text.empty() || !JsonObject::TryParse(text, payload)) {
            out.error = L"Gemini OCR 返回格式无法解析。";
            return out;
        }

        out.text = std::wstring{ payload.GetNamedString(L"full_text", L"") };
        auto blocks = payload.GetNamedArray(L"blocks", nullptr);
        if (blocks) {
            out.blocks.reserve(blocks.Size());
            for (uint32_t i = 0; i < blocks.Size(); ++i) {
                auto item = blocks.GetObjectAt(i);
                auto box = item.GetNamedArray(L"box_2d", nullptr);
                auto source = std::wstring{ item.GetNamedString(L"text", L"") };
                OcrBlock b;
                if (source.empty() || !readBox(box, b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.source = std::move(source);
                out.blocks.push_back(std::move(b));
            }
        }

        if (out.text.empty() && !out.blocks.empty()) {
            for (auto& b : out.blocks) {
                if (!out.text.empty()) out.text += L"\r\n";
                out.text += b.source;
            }
        }
        if (out.text.empty()) { out.error = L"Gemini 没有识别到文字。"; return out; }

        // Keep diagnostics lightweight: this is useful when a screenshot still receives
        // coarse spans and lets us distinguish model geometry from UI mapping problems.
        StarCapDiag::append(std::format(L"ocr-selection spans={} chars={} image={}x{}",
            out.blocks.size(), out.text.size(), width, height));
        out.ok = true;
        return out;
    }
}
