from pathlib import Path


def replace_exact(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


ai_path = Path("Src/AIClient.h")
ai = ai_path.read_text(encoding="utf-8-sig")

ai = replace_exact(
    ai,
    '            return { L"claude-sonnet-5", L"claude-haiku-4-5-20251001", L"claude-opus-5" };',
    '            return { L"claude-sonnet-5", L"claude-opus-5", L"claude-fable-5-1", L"claude-haiku-4-5-20251001" };',
    1,
    "Claude curated models",
)

ai = replace_exact(
    ai,
    '''        if (id == ProviderGemini) {
            auto arr = root.GetNamedArray(L"models", nullptr);
            if (arr) for (uint32_t i = 0; i < arr.Size(); ++i)
                add(std::wstring{ arr.GetObjectAt(i).GetNamedString(L"name", L"") });
        }''',
    '''        if (id == ProviderGemini) {
            auto arr = root.GetNamedArray(L"models", nullptr);
            if (arr) for (uint32_t i = 0; i < arr.Size(); ++i) {
                auto item = arr.GetObjectAt(i);
                auto methods = item.GetNamedArray(L"supportedGenerationMethods", nullptr);
                bool canGenerate = false;
                if (methods) for (uint32_t j = 0; j < methods.Size(); ++j) {
                    if (std::wstring{ methods.GetStringAt(j) } == L"generateContent") {
                        canGenerate = true; break;
                    }
                }
                if (!canGenerate) continue;
                add(std::wstring{ item.GetNamedString(L"name", L"") });
            }
        }''',
    1,
    "Gemini model capability filter",
)

ai = replace_exact(
    ai,
    '''        // Keep curated defaults visible even if an account's model-list endpoint is
        // temporarily incomplete; append account-visible models after them.
        auto merged = builtInModels(id);
        for (auto& model : out.models)
            if (std::find(merged.begin(), merged.end(), model) == merged.end()) merged.push_back(model);
        out.models = std::move(merged);
        out.ok = true;''',
    '''        // Once the provider successfully returns compatible models, trust that
        // account-specific list. Curated defaults are only a fallback when no compatible
        // models were returned; the UI separately preserves the user's current selection.
        if (out.models.empty()) out.models = builtInModels(id);
        out.ok = true;''',
    1,
    "Account model list behavior",
)

ai_path.write_text(ai, encoding="utf-8")

gemini_path = Path("Src/GeminiClient.h")
gemini = gemini_path.read_text(encoding="utf-8-sig")
gemini = replace_exact(
    gemini,
    '''        else {
            // Flash 截图 OCR/翻译属于简单视觉任务，minimal 优先低延迟；非 Flash 用 low 更稳妥。
            thinking.SetNamedValue(L"thinkingLevel", JsonValue::CreateStringValue(
                model.find(L"flash") != std::wstring::npos ? L"minimal" : L"low"));
        }''',
    '''        else {
            // StarCap OCR/translation is latency-sensitive, but Gemini 3.7 Flash does
            // not support "minimal". "low" is supported by all current non-image
            // Gemini 3.x models offered by StarCap, including 3.7 Flash.
            thinking.SetNamedValue(L"thinkingLevel", JsonValue::CreateStringValue(L"low"));
        }''',
    1,
    "Gemini thinking level",
)
gemini_path.write_text(gemini, encoding="utf-8")

print("Reviewed AI provider compatibility fixes applied.")
