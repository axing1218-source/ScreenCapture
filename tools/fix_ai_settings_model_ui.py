from pathlib import Path


def replace_exact(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)

# Setting.h: persist per-provider refreshed model lists.
path = Path('Src/Setting.h')
text = path.read_text(encoding='utf-8-sig')
text = replace_exact(text,
    '\tstd::wstring getAiModel(const std::wstring& provider);\n\tvoid setAiModel(const std::wstring& provider, const std::wstring& model);',
    '\tstd::wstring getAiModel(const std::wstring& provider);\n\tvoid setAiModel(const std::wstring& provider, const std::wstring& model);\n\tstd::vector<std::wstring> getAiModels(const std::wstring& provider);\n\tvoid setAiModels(const std::wstring& provider, const std::vector<std::wstring>& models);',
    1, 'Setting model cache declarations')
path.write_text(text, encoding='utf-8')

# Setting.cpp: safe Gemini default, migrate old 2.x selection, persist refreshed models.
path = Path('Src/Setting.cpp')
text = path.read_text(encoding='utf-8-sig')
text = replace_exact(text,
    '        return L"gemini-3.7-flash";',
    '        return L"gemini-3.5-flash-lite";',
    1, 'Gemini default model')
text = replace_exact(text,
'''    auto model = std::wstring{ obj.GetNamedString(L"model", L"") };
    if (!model.empty()) return model;

    if (id == L"gemini") {
        auto legacy = configObj.GetNamedObject(L"gemini", nullptr);
        if (legacy) {
            model = std::wstring{ legacy.GetNamedString(L"model", L"") };
            if (!model.empty()) return model;
        }
    }
    return defaultAiModel(id);''',
'''    auto model = std::wstring{ obj.GetNamedString(L"model", L"") };
    if (!model.empty()) {
        // Gemini 2.x access is now restricted for many new API users. Do not keep
        // an inherited v0.9.7 2.x selection as the active v0.9.8 default.
        if (id == L"gemini" && model.rfind(L"gemini-2.", 0) == 0) return defaultAiModel(id);
        return model;
    }

    if (id == L"gemini") {
        auto legacy = configObj.GetNamedObject(L"gemini", nullptr);
        if (legacy) {
            model = std::wstring{ legacy.GetNamedString(L"model", L"") };
            if (!model.empty() && model.rfind(L"gemini-2.", 0) != 0) return model;
        }
    }
    return defaultAiModel(id);''',
    1, 'Legacy Gemini model migration')
needle = '''void Setting::setAiModel(const std::wstring& provider, const std::wstring& model)
{
    const auto id = normalizeAiProviderId(provider);
    const auto value = model.empty() ? defaultAiModel(id) : model;
    auto obj = getAiProviderObj(id);
    obj.SetNamedValue(L"model", JsonValue::CreateStringValue(value));

    if (id == L"gemini") {
        auto legacy = configObj.GetNamedObject(L"gemini", nullptr);
        if (!legacy) {
            legacy = JsonObject();
            configObj.SetNamedValue(L"gemini", legacy);
        }
        legacy.SetNamedValue(L"model", JsonValue::CreateStringValue(value));
    }
    save();
}
'''
addition = needle + '''
std::vector<std::wstring> Setting::getAiModels(const std::wstring& provider)
{
    std::vector<std::wstring> result;
    auto obj = getAiProviderObj(normalizeAiProviderId(provider));
    auto arr = obj.GetNamedArray(L"models", nullptr);
    if (!arr) return result;
    for (uint32_t i = 0; i < arr.Size(); ++i) {
        auto value = std::wstring{ arr.GetStringAt(i) };
        if (!value.empty() && std::find(result.begin(), result.end(), value) == result.end())
            result.push_back(std::move(value));
    }
    return result;
}

void Setting::setAiModels(const std::wstring& provider, const std::vector<std::wstring>& models)
{
    JsonArray arr;
    for (const auto& model : models) {
        if (!model.empty()) arr.Append(JsonValue::CreateStringValue(model));
    }
    auto obj = getAiProviderObj(normalizeAiProviderId(provider));
    obj.SetNamedValue(L"models", arr);
    save();
}
'''
text = replace_exact(text, needle, addition, 1, 'Setting model cache implementation')
path.write_text(text, encoding='utf-8')

# AIClient: default to reliable/cheap Gemini model and filter specialized/legacy entries.
path = Path('Src/AIClient.h')
text = path.read_text(encoding='utf-8-sig')
text = replace_exact(text,
    '        return { L"gemini-3.7-flash", L"gemini-3.6-flash", L"gemini-3.5-flash", L"gemini-3.5-flash-lite" };',
    '        return { L"gemini-3.5-flash-lite", L"gemini-3.5-flash", L"gemini-3.6-flash", L"gemini-3.7-flash" };',
    1, 'Gemini curated model order')
text = replace_exact(text,
'''        return model.rfind(L"gemini-", 0) == 0 &&
            model.find(L"embedding") == std::wstring::npos &&
            model.find(L"image") == std::wstring::npos &&
            model.find(L"tts") == std::wstring::npos;''',
'''        // StarCap needs a general multimodal generation model, not every model that
        // happens to expose generateContent. Restrict Gemini to current 3.x Flash/Pro
        // families and exclude image-generation/specialized variants.
        if (model.rfind(L"gemini-3", 0) != 0) return false;
        if (model.find(L"embedding") != std::wstring::npos ||
            model.find(L"image") != std::wstring::npos ||
            model.find(L"tts") != std::wstring::npos ||
            model.find(L"transcribe") != std::wstring::npos ||
            model.find(L"robotics") != std::wstring::npos ||
            model.find(L"computer-use") != std::wstring::npos ||
            model.find(L"omni") != std::wstring::npos ||
            model.find(L"live") != std::wstring::npos ||
            model.find(L"audio") != std::wstring::npos) return false;
        return model.find(L"flash") != std::wstring::npos || model.find(L"pro") != std::wstring::npos;''',
    1, 'Gemini screenshot model filter')
path.write_text(text, encoding='utf-8')

# GeminiClient: connection test must leave room for thinking tokens.
path = Path('Src/GeminiClient.h')
text = path.read_text(encoding='utf-8-sig')
text = replace_exact(text,
    '        generation.SetNamedValue(L"maxOutputTokens", JsonValue::CreateNumberValue(16));',
    '        // Thinking-enabled Gemini 3.x models can consume more than 16 tokens before\n        // emitting the visible "OK". Keep the test small, but large enough to be valid.\n        generation.SetNamedValue(L"maxOutputTokens", JsonValue::CreateNumberValue(256));',
    1, 'Gemini connection test output budget')
path.write_text(text, encoding='utf-8')

# WinSettingCommon.h: status sanitizer and paged model selector.
path = Path('Src/Win/WinSettingCommon.h')
text = path.read_text(encoding='utf-8-sig')
text = replace_exact(text,
    '\tvoid refreshAiControls();\n\tvoid showAiProviderBox();',
    '\tvoid refreshAiControls();\n\tvoid setAiStatus(const std::wstring& text);\n\tvoid showAiProviderBox();',
    1, 'AI status declaration')
text = replace_exact(text,
    '\tstd::vector<std::wstring> aiModels;\n\tLing::ScrollerBox* selectBox{ nullptr };',
    '\tstd::vector<std::wstring> aiModels;\n\tsize_t aiModelPage{ 0 };\n\tLing::ScrollerBox* selectBox{ nullptr };',
    1, 'AI model page state')
path.write_text(text, encoding='utf-8')

# WinSettingCommon.cpp: compact status, persist refreshed models, auto-replace stale models,
# and paginate the model popup so no ScrollerBox scroll hit-testing is required.
path = Path('Src/Win/WinSettingCommon.cpp')
text = path.read_text(encoding='utf-8-sig')
text = text.replace('if (aiStatus) aiStatus->setText(L"正在连接 " + AIClient::providerName(provider) + L"...");',
                    'setAiStatus(L"正在连接 " + AIClient::providerName(provider) + L"...");')
text = replace_exact(text,
'''                if (!weakThis.lock() || !aiStatus) return;
                aiStatus->setText(result.ok ? AIClient::providerName(provider) + L" 连接成功" : result.message);''',
'''                if (!weakThis.lock() || !aiStatus) return;
                if (result.ok) {
                    setAiStatus(AIClient::providerName(provider) + L" 连接成功");
                }
                else {
                    setAiStatus(AIClient::providerName(provider) + L" 连接失败；详细原因已弹出");
                    MessageBoxW(win->hwnd, result.message.c_str(), L"AI 连接失败", MB_OK | MB_ICONWARNING);
                }''',
    1, 'Connection result UI')
text = text.replace('if (aiStatus) aiStatus->setText(AIClient::providerName(provider) + L" 设置已保存；API Key 已用 Windows DPAPI 加密");',
                    'setAiStatus(AIClient::providerName(provider) + L" 设置已保存；API Key 已用 Windows DPAPI 加密");')
text = text.replace('if (aiStatus) aiStatus->setText(L"正在获取 " + AIClient::providerName(provider) + L" 可用模型...");',
                    'setAiStatus(L"正在获取 " + AIClient::providerName(provider) + L" 可用模型...");')
text = replace_exact(text,
'''                if (!weakThis.lock() || !aiStatus) return;
                if (!result.ok) { aiStatus->setText(result.error); return; }
                aiModels = std::move(result.models);
                auto current = aiModelBtn ? aiModelBtn->getText() : L"";
                if (!current.empty() && std::find(aiModels.begin(), aiModels.end(), current) == aiModels.end())
                    aiModels.insert(aiModels.begin(), current);
                aiStatus->setText(std::format(L"{}：已刷新 {} 个兼容模型", AIClient::providerName(provider), aiModels.size()));''',
'''                if (!weakThis.lock() || !aiStatus) return;
                if (!result.ok) {
                    setAiStatus(AIClient::providerName(provider) + L" 刷新模型失败；详细原因已弹出");
                    MessageBoxW(win->hwnd, result.error.c_str(), L"刷新 AI 模型失败", MB_OK | MB_ICONWARNING);
                    return;
                }
                aiModels = std::move(result.models);
                auto setting = Setting::get();
                setting->setAiModels(provider, aiModels);
                auto current = setting->getAiModel(provider);
                if (!aiModels.empty() && std::find(aiModels.begin(), aiModels.end(), current) == aiModels.end()) {
                    current = aiModels.front();
                    setting->setAiModel(provider, current);
                    if (aiModelBtn) aiModelBtn->setText(current);
                }
                aiModelPage = 0;
                setAiStatus(std::format(L"{}：已刷新并保存 {} 个兼容模型", AIClient::providerName(provider), aiModels.size()));''',
    1, 'Refresh result persistence')

old_refresh = '''void WinSettingCommon::refreshAiControls()
{
    auto setting = Setting::get();
    if (!setting) return;
    auto provider = setting->getAiProvider();
    auto name = AIClient::providerName(provider);
    auto model = setting->getAiModel(provider);
    if (model.empty()) model = AIClient::defaultModel(provider);
    aiModels = AIClient::builtInModels(provider);
    if (!model.empty() && std::find(aiModels.begin(), aiModels.end(), model) == aiModels.end())
        aiModels.insert(aiModels.begin(), model);
    if (aiProviderBtn) aiProviderBtn->setText(name + L"  ▼");
    if (aiKeyLabel) aiKeyLabel->setText(name + L" API Key");
    if (aiApiKeyBox) {
        aiApiKeyBox->setPlaceholder(L"粘贴 " + name + L" API Key");
        aiApiKeyBox->setText(setting->getAiApiKey(provider));
    }
    if (aiModelBtn) aiModelBtn->setText(model);
    if (aiStatus) aiStatus->setText(L"当前：" + name + L" / " + model + L"；Key 仅加密保存在本机");
}
'''
new_refresh = '''void WinSettingCommon::refreshAiControls()
{
    auto setting = Setting::get();
    if (!setting) return;
    auto provider = setting->getAiProvider();
    auto name = AIClient::providerName(provider);
    auto model = setting->getAiModel(provider);
    if (model.empty()) model = AIClient::defaultModel(provider);
    aiModels = setting->getAiModels(provider);
    if (aiModels.empty()) aiModels = AIClient::builtInModels(provider);
    if (!aiModels.empty() && std::find(aiModels.begin(), aiModels.end(), model) == aiModels.end()) {
        model = aiModels.front();
        setting->setAiModel(provider, model);
    }
    aiModelPage = 0;
    if (aiProviderBtn) aiProviderBtn->setText(name + L"  ▼");
    if (aiKeyLabel) aiKeyLabel->setText(name + L" API Key");
    if (aiApiKeyBox) {
        aiApiKeyBox->setPlaceholder(L"粘贴 " + name + L" API Key");
        aiApiKeyBox->setText(setting->getAiApiKey(provider));
    }
    if (aiModelBtn) aiModelBtn->setText(model);
    setAiStatus(L"当前：" + name + L" / " + model + L"；Key 仅加密保存在本机");
}

void WinSettingCommon::setAiStatus(const std::wstring& value)
{
    if (!aiStatus) return;
    std::wstring text = value;
    for (auto& ch : text) if (ch == L'\\r' || ch == L'\\n' || ch == L'\\t') ch = L' ';
    while (text.find(L"  ") != std::wstring::npos) text.replace(text.find(L"  "), 2, L" ");
    constexpr size_t maxChars = 52;
    if (text.size() > maxChars) text = text.substr(0, maxChars - 1) + L"…";
    aiStatus->setText(text);
}
'''
text = replace_exact(text, old_refresh, new_refresh, 1, 'Refresh controls implementation')

text = replace_exact(text,
'''    showChoiceBox(aiProviderBtn, items, [this](const std::wstring& provider) {
        Setting::get()->setAiProvider(provider);
        refreshAiControls();
    });''',
'''    showChoiceBox(aiProviderBtn, items, [this](const std::wstring& provider) {
        Setting::get()->setAiProvider(provider);
        aiModelPage = 0;
        refreshAiControls();
    });''',
    1, 'Provider page reset')

old_models = '''void WinSettingCommon::showAiModelBox()
{
    if (!aiModelBtn) return;
    if (aiModels.empty()) aiModels = AIClient::builtInModels(Setting::get()->getAiProvider());
    std::vector<std::pair<std::wstring, std::wstring>> items;
    for (const auto& model : aiModels) items.emplace_back(model, model);
    showChoiceBox(aiModelBtn, items, [this](const std::wstring& model) {
        auto setting = Setting::get();
        auto provider = setting->getAiProvider();
        setting->setAiModel(provider, model);
        if (aiModelBtn) aiModelBtn->setText(model);
        if (aiStatus) aiStatus->setText(L"已选择 " + AIClient::providerName(provider) + L" / " + model);
    });
}
'''
new_models = '''void WinSettingCommon::showAiModelBox()
{
    if (!aiModelBtn) return;
    if (aiModels.empty()) {
        aiModels = Setting::get()->getAiModels(Setting::get()->getAiProvider());
        if (aiModels.empty()) aiModels = AIClient::builtInModels(Setting::get()->getAiProvider());
    }
    constexpr size_t pageSize = 8;
    const size_t pageCount = std::max<size_t>(1, (aiModels.size() + pageSize - 1) / pageSize);
    aiModelPage = std::min(aiModelPage, pageCount - 1);
    const size_t begin = aiModelPage * pageSize;
    const size_t end = std::min(aiModels.size(), begin + pageSize);

    std::vector<std::pair<std::wstring, std::wstring>> items;
    if (aiModelPage > 0) items.emplace_back(L"← 上一页", L"__prev_page__");
    for (size_t i = begin; i < end; ++i) items.emplace_back(aiModels[i], aiModels[i]);
    if (aiModelPage + 1 < pageCount) items.emplace_back(L"下一页 →", L"__next_page__");

    showChoiceBox(aiModelBtn, items, [this](const std::wstring& model) {
        if (model == L"__prev_page__") {
            if (aiModelPage > 0) --aiModelPage;
            showAiModelBox();
            return;
        }
        if (model == L"__next_page__") {
            ++aiModelPage;
            showAiModelBox();
            return;
        }
        auto setting = Setting::get();
        auto provider = setting->getAiProvider();
        setting->setAiModel(provider, model);
        if (aiModelBtn) aiModelBtn->setText(model);
        setAiStatus(L"已选择 " + AIClient::providerName(provider) + L" / " + model);
    });
}
'''
text = replace_exact(text, old_models, new_models, 1, 'Paged model selector')
path.write_text(text, encoding='utf-8')

print('AI settings/model UI fixes applied.')
