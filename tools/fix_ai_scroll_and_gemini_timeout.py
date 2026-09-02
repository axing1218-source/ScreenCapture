from pathlib import Path


def replace_exact(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


def replace_function(text: str, signature: str, replacement: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"Missing function: {signature}")
    brace = text.find('{', start)
    if brace < 0:
        raise SystemExit(f"Missing opening brace: {signature}")
    depth = 0
    i = brace
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                if end < len(text) and text[end] == '\n':
                    end += 1
                return text[:start] + replacement.rstrip() + '\n' + text[end:]
        i += 1
    raise SystemExit(f"Unbalanced function: {signature}")


# 1) Gemini: distinguish ordinary generation from the connection test and give
# heavier thinking models enough time to return their first response.
path = Path('Src/GeminiClient.h')
text = path.read_text(encoding='utf-8-sig')
text = replace_exact(
    text,
    '    inline HttpResult postGenerate(const std::wstring& apiKey, const std::wstring& model, const std::wstring& json)\n',
    '    inline HttpResult postGenerate(const std::wstring& apiKey, const std::wstring& model, const std::wstring& json,\n        DWORD receiveTimeoutMs = 45000)\n',
    1,
    'Gemini postGenerate signature',
)
text = replace_exact(
    text,
    '        // 截图工具不应让一次请求挂一分钟。连接失败尽快反馈；正常 Flash 请求通常远低于此上限。\n        WinHttpSetTimeouts(session, 8000, 8000, 15000, 25000);',
    '        // Normal OCR/translation gets a 45s receive window. The explicit connection\n        // test can request a longer window because thinking-enabled Gemini models may\n        // need substantially longer than Flash-Lite before returning the first token.\n        WinHttpSetTimeouts(session, 8000, 8000, 15000, receiveTimeoutMs);',
    1,
    'Gemini timeout configuration',
)
text = replace_exact(
    text,
    '        if (!sent) {\n            result.error = std::format(L"Gemini 网络请求失败（Windows 错误 {}）。", GetLastError());\n        }',
    '        if (!sent) {\n            const DWORD error = GetLastError();\n            if (error == ERROR_WINHTTP_TIMEOUT) {\n                result.error = std::format(L"Gemini 请求超时（等待约 {} 秒）。请稍后重试，或选择响应更快的模型。",\n                    std::max<DWORD>(1, receiveTimeoutMs / 1000));\n            }\n            else {\n                result.error = std::format(L"Gemini 网络请求失败（Windows 错误 {}）。", error);\n            }\n        }',
    1,
    'Gemini timeout error message',
)

sig = '    inline TestResult testConnection(const std::wstring& apiKey, const std::wstring& model)'
start = text.find(sig)
if start < 0:
    raise SystemExit('Gemini testConnection missing')
next_fn = text.find('\n    inline ', start + len(sig))
region_end = len(text) if next_fn < 0 else next_fn
region = text[start:region_end]
old_call = 'auto http = postGenerate(apiKey, modelId, root.Stringify().c_str());'
if region.count(old_call) != 1:
    raise SystemExit(f'Gemini test call: expected one call, found {region.count(old_call)}')
region = region.replace(old_call,
    'auto http = postGenerate(apiKey, modelId, root.Stringify().c_str(), 90000);')
text = text[:start] + region + text[region_end:]
path.write_text(text, encoding='utf-8')


# 2) Settings dropdown: use Ling ScrollerBox for wheel + draggable scrollbar, but
# perform selection hit-testing in StarCap with getScrollY(), because ordinary
# Button::isPosIn() does not account for a scrolled parent visual offset.
path = Path('Src/Win/WinSettingCommon.h')
text = path.read_text(encoding='utf-8-sig')
text = replace_exact(
    text,
    '\tstd::vector<std::wstring> aiModels;\n\tsize_t aiModelPage{ 0 };\n\tLing::ScrollerBox* selectBox{ nullptr };',
    '\tstd::vector<std::wstring> aiModels;\n\tstd::vector<std::wstring> selectValues;\n\tstd::function<void(const std::wstring&)> selectOnChoose;\n\tfloat selectItemHeight{ 30.f };\n\tLing::ScrollerBox* selectBox{ nullptr };',
    1,
    'Scrollable selector state',
)
path.write_text(text, encoding='utf-8')

path = Path('Src/Win/WinSettingCommon.cpp')
text = path.read_text(encoding='utf-8-sig')
# Remove now-obsolete pagination resets anywhere in the settings page.
text = text.replace('                aiModelPage = 0;\n', '')
text = text.replace('        aiModelPage = 0;\n', '')
text = text.replace('    aiModelPage = 0;\n', '')

text = replace_function(text, 'void WinSettingCommon::showAiModelBox()', r'''void WinSettingCommon::showAiModelBox()
{
    if (!aiModelBtn) return;
    if (aiModels.empty()) {
        aiModels = Setting::get()->getAiModels(Setting::get()->getAiProvider());
        if (aiModels.empty()) aiModels = AIClient::builtInModels(Setting::get()->getAiProvider());
    }
    std::vector<std::pair<std::wstring, std::wstring>> items;
    items.reserve(aiModels.size());
    for (const auto& model : aiModels) items.emplace_back(model, model);
    showChoiceBox(aiModelBtn, items, [this](const std::wstring& model) {
        auto setting = Setting::get();
        auto provider = setting->getAiProvider();
        setting->setAiModel(provider, model);
        if (aiModelBtn) aiModelBtn->setText(model);
        setAiStatus(L"已选择 " + AIClient::providerName(provider) + L" / " + model);
    });
}
''')

text = replace_function(text, 'void WinSettingCommon::hideSelectBox()', r'''void WinSettingCommon::hideSelectBox()
{
    win->onMouseDown.remove(onMouseDownToken);
    selectValues.clear();
    selectOnChoose = {};
    if (!selectBox) return;
    win->body->removeChild(selectBox);
    selectBox = nullptr;
}
''')

text = replace_function(text, 'void WinSettingCommon::showChoiceBox(Ling::Button* btn,', r'''void WinSettingCommon::showChoiceBox(Ling::Button* btn,
    const std::vector<std::pair<std::wstring, std::wstring>>& items,
    std::function<void(const std::wstring&)> onChoose)
{
    if (!btn || items.empty()) return;
    hideSelectBox();

    selectItemHeight = 30.f;
    selectValues.clear();
    selectValues.reserve(items.size());
    for (const auto& pair : items) selectValues.push_back(pair.second);
    selectOnChoose = std::move(onChoose);

    const float totalH = std::min(320.f, selectItemHeight * (float)items.size());
    selectBox = win->body->makeChild<Ling::ScrollerBox>();
    selectBox->setSize(btn->w / win->dpi, totalH);
    selectBox->setPositionType(Ling::Position::Absolute);
    selectBox->setPosition(Ling::Edge::Left, btn->x / win->dpi);
    selectBox->setPosition(Ling::Edge::Top, (btn->y + btn->h) / win->dpi);
    selectBox->setBg(0xFFFFFFFF);
    selectBox->setBorder(1.f, 0x597ef766);

    // Use passive labels for rows. ScrollerBox moves only the content visual when it
    // scrolls, while Ling Button hit-testing uses the original unscrolled x/y values.
    // A single parent-level mouse handler below converts window Y to content Y using
    // getScrollY(), so every row remains clickable after wheel/scrollbar movement.
    for (const auto& pair : items) {
        auto itemLabel = selectBox->makeChild<Ling::Label>();
        itemLabel->setText(pair.first);
        itemLabel->setHeight(selectItemHeight);
        itemLabel->setWidthPercent(100.f);
        itemLabel->setFontSize(11.f);
        itemLabel->setAlignItems(Ling::Align::Center);
        itemLabel->setJustifyContent(Ling::Justify::Center);
    }

    auto weakThis = getWeakThis();
    onMouseDownToken = win->onMouseDown.add([this, weakThis](POINT pos, bool isRight) {
        if (!weakThis.lock() || !selectBox || isRight) return;
        if (!selectBox->isPosIn(pos)) {
            hideSelectBox();
            return;
        }
        // Leave the right-side scrollbar strip to ScrollerBox itself so the thumb can
        // be dragged normally.
        if (!selectBox->isPosInContent(pos)) return;

        const float itemPx = selectItemHeight * win->dpi;
        if (itemPx <= 0.f) return;
        const float contentY = (float)pos.y - selectBox->y + selectBox->getScrollY();
        if (contentY < 0.f) return;
        const size_t index = (size_t)std::floor(contentY / itemPx);
        if (index >= selectValues.size()) return;

        auto value = selectValues[index];
        auto choose = selectOnChoose;
        Ling::App::get()->dq.TryEnqueue([this, weakThis, value = std::move(value), choose = std::move(choose)]() mutable {
            if (!weakThis.lock()) return;
            hideSelectBox();
            if (choose) choose(value);
        });
    });
}
''')

if 'aiModelPage' in text:
    raise SystemExit('Pagination state still referenced in WinSettingCommon.cpp')
if '__next_page__' in text or '__prev_page__' in text:
    raise SystemExit('Pagination sentinel still referenced')
path.write_text(text, encoding='utf-8')

print('Gemini timeout and scrollable AI model selector fixes applied.')
