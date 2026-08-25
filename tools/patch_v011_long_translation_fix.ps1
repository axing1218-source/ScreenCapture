$ErrorActionPreference = 'Stop'

function Replace-Checked([ref]$textRef, [string]$old, [string]$new, [string]$label) {
    if (-not $textRef.Value.Contains($old)) { throw "Patch target not found: $label" }
    $textRef.Value = $textRef.Value.Replace($old, $new)
}

# -----------------------------------------------------------------------------
# 1) Gemini image translation returns the detected source text as well as the
#    translation. This fixes the direct-long-translate window's Original tab
#    without spending a second OCR request.
# -----------------------------------------------------------------------------
$path = 'Src\GeminiClient.h'
$src = Get-Content $path -Raw

Replace-Checked ([ref]$src) @'
    struct TranslationResult
    {
        bool ok{ false };
        std::wstring translatedText;
        std::vector<TranslationBlock> blocks;
        std::wstring error;
    };
'@ @'
    struct TranslationResult
    {
        bool ok{ false };
        std::wstring sourceText;
        std::wstring translatedText;
        std::vector<TranslationBlock> blocks;
        std::wstring error;
    };
'@ 'TranslationResult source text'

Replace-Checked ([ref]$src) @'
        JsonObject props;
        props.SetNamedValue(L"translated_text", strType);
        props.SetNamedValue(L"blocks", blocks);
        JsonArray req;
        req.Append(JsonValue::CreateStringValue(L"translated_text"));
        req.Append(JsonValue::CreateStringValue(L"blocks"));
'@ @'
        JsonObject props;
        props.SetNamedValue(L"source_text", strType);
        props.SetNamedValue(L"translated_text", strType);
        props.SetNamedValue(L"blocks", blocks);
        JsonArray req;
        req.Append(JsonValue::CreateStringValue(L"source_text"));
        req.Append(JsonValue::CreateStringValue(L"translated_text"));
        req.Append(JsonValue::CreateStringValue(L"blocks"));
'@ 'image translation schema source text'

Replace-Checked ([ref]$src) @'
        auto req = makeBaseRequest(modelId,
            L"Translate every readable text region in this screenshot into " + targetLanguage +
            L". Return translated_text in natural reading order and blocks. Each block contains only translation and "
            L"box_2d=[ymin,xmin,ymax,xmax] normalized 0-1000. Do not generate or edit an image. Do not add commentary. "
            L"Keep UI translations concise enough to fit their original regions.",
            &png, makeTranslationSchema(), 8192);
'@ @'
        auto req = makeBaseRequest(modelId,
            L"Read ALL readable text in this screenshot from top to bottom, including a tall/long screenshot. "
            L"Return source_text exactly as read in natural reading order, then translate all of it into " + targetLanguage +
            L" as translated_text. Also return translation blocks with box_2d=[ymin,xmin,ymax,xmax] normalized 0-1000. "
            L"Do not omit later/bottom sections of a long screenshot. Do not summarize, add commentary, generate, or edit an image. "
            L"Keep UI translations concise enough to fit their original regions.",
            &png, makeTranslationSchema(), 16384);
'@ 'complete image translation prompt'

Replace-Checked ([ref]$src) @'
        if (text.empty() || !JsonObject::TryParse(text, payload)) { out.error = L"Gemini 翻译返回格式无法解析。"; return out; }
        out.translatedText = std::wstring{ payload.GetNamedString(L"translated_text", L"") };
'@ @'
        if (text.empty() || !JsonObject::TryParse(text, payload)) { out.error = L"Gemini 翻译返回格式无法解析。"; return out; }
        out.sourceText = std::wstring{ payload.GetNamedString(L"source_text", L"") };
        out.translatedText = std::wstring{ payload.GetNamedString(L"translated_text", L"") };
'@ 'parse image translation source text'

Replace-Checked ([ref]$src) @'
        TranslationResult out;
        if (sourceBlocks.empty()) { out.error = L"没有可翻译的 OCR 文字块。"; return out; }
        JsonArray src;
'@ @'
        TranslationResult out;
        if (sourceBlocks.empty()) { out.error = L"没有可翻译的 OCR 文字块。"; return out; }
        for (const auto& b : sourceBlocks) {
            if (!out.sourceText.empty()) out.sourceText += L"\r\n";
            out.sourceText += b.source;
        }
        JsonArray src;
'@ 'text translation source text'

Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 2) Result window: direct image translation now has a real Original tab.
#    For long screenshots (or suspiciously incomplete OCR blocks), translation
#    uses the complete image instead of translating only the OCR block subset.
# -----------------------------------------------------------------------------
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw

Replace-Checked ([ref]$src) @'
            translatedText = std::move(result.translatedText);
            translationBlocks = std::move(result.blocks);
'@ @'
            if (originalText.empty() && !result.sourceText.empty()) {
                originalText = std::move(result.sourceText);
            }
            translatedText = std::move(result.translatedText);
            translationBlocks = std::move(result.blocks);
'@ 'populate original text from direct translation'

Replace-Checked ([ref]$src) @'
            if (status) status->setText(geminiOcrBlocks.empty()
                ? L"正在让 Gemini 识别图片并翻译..."
                : L"正在翻译已识别文字（无需再次上传图片）...");
            const auto myRequest = requestId.load();
            auto blocks = geminiOcrBlocks;
            auto imagePixels = pixels;
            std::thread([blocks = std::move(blocks), imagePixels = std::move(imagePixels),
                width = imageW, height = imageH, apiKey = std::move(apiKey), model = std::move(model), myRequest]() mutable {
                GeminiClient::TranslationResult r;
                if (!blocks.empty()) r = GeminiClient::translateOcrBlocks(blocks, apiKey, model);
                else r = GeminiClient::translateImage(imagePixels, width, height, apiKey, model);
'@ @'
            size_t blockChars = 0;
            for (const auto& b : geminiOcrBlocks) blockChars += b.source.size();
            const bool longImage = imageW > 0 && imageH > imageW * 2;
            const bool blocksLookIncomplete = !originalText.empty() &&
                blockChars * 10 < originalText.size() * 7;
            const bool preferImageTranslation = longImage || blocksLookIncomplete;
            if (status) status->setText(preferImageTranslation
                ? L"正在翻译完整图片，长截图会从顶部处理到底部..."
                : (geminiOcrBlocks.empty()
                    ? L"正在让 Gemini 识别图片并翻译..."
                    : L"正在翻译已识别文字（无需再次上传图片）..."));
            const auto myRequest = requestId.load();
            auto blocks = geminiOcrBlocks;
            auto imagePixels = pixels;
            std::thread([blocks = std::move(blocks), imagePixels = std::move(imagePixels),
                width = imageW, height = imageH, apiKey = std::move(apiKey), model = std::move(model),
                myRequest, preferImageTranslation]() mutable {
                GeminiClient::TranslationResult r;
                if (!blocks.empty() && !preferImageTranslation) r = GeminiClient::translateOcrBlocks(blocks, apiKey, model);
                else r = GeminiClient::translateImage(imagePixels, width, height, apiKey, model);
'@ 'prefer full image translation for long/incomplete OCR'

Set-Content $path $src -Encoding utf8

foreach ($check in @(
    @{ Path='Src\GeminiClient.h'; Needle='std::wstring sourceText;' },
    @{ Path='Src\GeminiClient.h'; Needle='L"source_text"' },
    @{ Path='Src\GeminiClient.h'; Needle='makeTranslationSchema(), 16384' },
    @{ Path='Src\WeShotOcrV2.h'; Needle='blocksLookIncomplete' },
    @{ Path='Src\WeShotOcrV2.h'; Needle='!preferImageTranslation' },
    @{ Path='Src\WeShotOcrV2.h'; Needle='originalText = std::move(result.sourceText);' }
)) {
    $v = Get-Content $check.Path -Raw
    if (-not $v.Contains($check.Needle)) { throw "Verification failed: $($check.Needle)" }
}

Write-Host 'v0.8.11 long translation completeness fix applied successfully.'
