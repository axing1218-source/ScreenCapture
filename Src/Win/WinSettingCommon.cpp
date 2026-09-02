#include "pch.h"
#include <thread>
#include "../Lang.h"
#include "../Setting.h"
#include "../AIClient.h"
#include "WinSetting.h"
#include "WinSettingCommon.h"

WinSettingCommon::WinSettingCommon(Ling::WinBase* parent):Ling::Node(parent)
{
    initAutoStartCtrls();
    initCaptureBorderCtrls();
    initAiCtrls();
    initLangCtrls();
    auto weakThis = getWeakThis();
    win->onDestroy.add([this, weakThis]() {
        if (!weakThis.lock()) return;
        this->hideSelectBox();
    });
}

WinSettingCommon::~WinSettingCommon()
{
    win->onMouseDown.remove(onMouseDownToken);
}

void WinSettingCommon::initAutoStartCtrls()
{
    auto box = makeChild<Ling::Node>();
    box->setHeight(39.f);
    box->setFlexDirection(Ling::FlexDirection::Row);
    box->setAlignItems(Ling::Align::Center);

    auto label = box->makeChild<Ling::Label>();
    label->setText(Lang::get(L"setting.autoStart"));
    label->setHeightPercent(100.f);
    label->setJustifyContent(Ling::Justify::Center);
    label->setFlexGrow(1.f);

    auto btn = box->makeChild<Ling::Button>();
    btn->setText(L"\ue687");
    btn->setFontFamily(L"icon");
    btn->setHeightPercent(100.f);
    btn->setFontSize(18.f);
    btn->setWidth(60.f);
    setAutoStartBtn(btn);

    btn->onClick.add([this](Ling::Button* btn) {
        auto setting = Setting::get();
        auto isAutoStart = setting->getAutoStart();
        setting->setAutoStart(!isAutoStart);
        setAutoStartBtn(btn);
    });

    auto border = makeChild<Ling::Node>();
    border->setHeight(1.f);
    border->setBg(0xE0E0E0FF);
}

void WinSettingCommon::initCaptureBorderCtrls()
{
    auto box = makeChild<Ling::Node>();
    box->setHeight(39.f);
    box->setFlexDirection(Ling::FlexDirection::Row);
    box->setAlignItems(Ling::Align::Center);

    auto label = box->makeChild<Ling::Label>();
    label->setText(L"截图边框粗细");
    label->setHeightPercent(100.f);
    label->setJustifyContent(Ling::Justify::Center);
    label->setFlexGrow(1.f);

    auto minusBtn = box->makeChild<Ling::Button>();
    minusBtn->setText(L"−");
    minusBtn->setSize(30.f, 28.f);
    minusBtn->setFontSize(16.f);
    minusBtn->setBorder(1.f, 0xE0E0E0FF);
    minusBtn->setHoverBg(0xF5F5F5FF);

    borderWidthLabel = box->makeChild<Ling::Label>();
    borderWidthLabel->setWidth(48.f);
    borderWidthLabel->setHeight(28.f);
    borderWidthLabel->setAlignItems(Ling::Align::Center);
    borderWidthLabel->setJustifyContent(Ling::Justify::Center);
    auto current = std::clamp(Setting::get()->getToolNum(L"capture", L"borderWidth", 2.f), 0.f, 8.f);
    borderWidthLabel->setText(std::format(L"{:.0f}px", current));

    auto plusBtn = box->makeChild<Ling::Button>();
    plusBtn->setText(L"+");
    plusBtn->setSize(30.f, 28.f);
    plusBtn->setFontSize(16.f);
    plusBtn->setBorder(1.f, 0xE0E0E0FF);
    plusBtn->setHoverBg(0xF5F5F5FF);

    auto change = [this](float delta) {
        auto setting = Setting::get();
        auto value = std::clamp(setting->getToolNum(L"capture", L"borderWidth", 2.f) + delta, 0.f, 8.f);
        value = std::round(value);
        setting->setToolNum(L"capture", L"borderWidth", value);
        if (borderWidthLabel) borderWidthLabel->setText(std::format(L"{:.0f}px", value));
    };
    minusBtn->onClick.add([change](Ling::Button*) { change(-1.f); });
    plusBtn->onClick.add([change](Ling::Button*) { change(1.f); });

    auto border = makeChild<Ling::Node>();
    border->setHeight(1.f);
    border->setBg(0xE0E0E0FF);
}

void WinSettingCommon::initAiCtrls()
{
    auto providerRow = makeChild<Ling::Node>();
    providerRow->setHeight(44.f);
    providerRow->setFlexDirection(Ling::FlexDirection::Row);
    providerRow->setAlignItems(Ling::Align::Center);

    auto providerLabel = providerRow->makeChild<Ling::Label>();
    providerLabel->setText(L"AI 服务商");
    providerLabel->setHeightPercent(100.f);
    providerLabel->setJustifyContent(Ling::Justify::Center);
    providerLabel->setFlexGrow(1.f);

    aiProviderBtn = providerRow->makeChild<Ling::Button>();
    aiProviderBtn->setSize(230.f, 30.f);
    aiProviderBtn->setFontSize(12.f);
    aiProviderBtn->setBorder(1.f, 0xD8D8D8FF);
    aiProviderBtn->setBorderRadius(4.f);
    aiProviderBtn->setBg(0xFFFFFFFF);
    aiProviderBtn->setHoverBg(0xF5F5F5FF);
    aiProviderBtn->onClick.add([this](Ling::Button*) { showAiProviderBox(); });

    auto testBtn = providerRow->makeChild<Ling::Button>();
    testBtn->setText(L"测试连接");
    testBtn->setSize(76.f, 30.f);
    testBtn->setFontSize(12.f);
    testBtn->setMarginLeft(8.f);
    testBtn->setBorder(1.f, 0xD8D8D8FF);
    testBtn->setBorderRadius(4.f);
    testBtn->setHoverBg(0xF2F2F2FF);
    testBtn->onClick.add([this](Ling::Button*) {
        if (!aiApiKeyBox || !aiModelBtn) return;
        auto setting = Setting::get();
        auto provider = setting->getAiProvider();
        auto apiKey = aiApiKeyBox->getText();
        auto model = aiModelBtn->getText();
        setting->setAiApiKey(provider, apiKey);
        setting->setAiModel(provider, model);
        if (aiStatus) aiStatus->setText(L"正在连接 " + AIClient::providerName(provider) + L"...");
        auto weakThis = getWeakThis();
        std::thread([this, weakThis, provider = std::move(provider), apiKey = std::move(apiKey), model = std::move(model)]() mutable {
            auto result = AIClient::testConnection(provider, apiKey, model);
            Ling::App::get()->dq.TryEnqueue([this, weakThis, provider = std::move(provider), result = std::move(result)]() mutable {
                if (!weakThis.lock() || !aiStatus) return;
                aiStatus->setText(result.ok ? AIClient::providerName(provider) + L" 连接成功" : result.message);
            });
        }).detach();
    });

    auto keyRow = makeChild<Ling::Node>();
    keyRow->setHeight(44.f);
    keyRow->setFlexDirection(Ling::FlexDirection::Row);
    keyRow->setAlignItems(Ling::Align::Center);

    aiKeyLabel = keyRow->makeChild<Ling::Label>();
    aiKeyLabel->setHeightPercent(100.f);
    aiKeyLabel->setJustifyContent(Ling::Justify::Center);
    aiKeyLabel->setFlexGrow(1.f);

    aiApiKeyBox = keyRow->makeChild<Ling::TextBox>();
    aiApiKeyBox->setSize(230.f, 30.f);
    aiApiKeyBox->setPadding(6.f);
    aiApiKeyBox->setFontSize(12.f);
    aiApiKeyBox->setBg(0xFFFFFFFF);
    aiApiKeyBox->setBorder(1.f, 0xD8D8D8FF);
    aiApiKeyBox->setBorderRadius(4.f);

    auto saveBtn = keyRow->makeChild<Ling::Button>();
    saveBtn->setText(L"保存");
    saveBtn->setSize(52.f, 30.f);
    saveBtn->setFontSize(12.f);
    saveBtn->setMarginLeft(8.f);
    saveBtn->setBorder(1.f, 0xD8D8D8FF);
    saveBtn->setBorderRadius(4.f);
    saveBtn->setHoverBg(0xF2F2F2FF);
    saveBtn->onClick.add([this](Ling::Button*) {
        if (!aiApiKeyBox || !aiModelBtn) return;
        auto setting = Setting::get();
        auto provider = setting->getAiProvider();
        setting->setAiApiKey(provider, aiApiKeyBox->getText());
        setting->setAiModel(provider, aiModelBtn->getText());
        if (aiStatus) aiStatus->setText(AIClient::providerName(provider) + L" 设置已保存；API Key 已用 Windows DPAPI 加密");
    });

    auto modelRow = makeChild<Ling::Node>();
    modelRow->setHeight(44.f);
    modelRow->setFlexDirection(Ling::FlexDirection::Row);
    modelRow->setAlignItems(Ling::Align::Center);

    auto modelLabel = modelRow->makeChild<Ling::Label>();
    modelLabel->setText(L"AI 模型");
    modelLabel->setHeightPercent(100.f);
    modelLabel->setJustifyContent(Ling::Justify::Center);
    modelLabel->setFlexGrow(1.f);

    aiModelBtn = modelRow->makeChild<Ling::Button>();
    aiModelBtn->setSize(230.f, 30.f);
    aiModelBtn->setFontSize(11.f);
    aiModelBtn->setBorder(1.f, 0xD8D8D8FF);
    aiModelBtn->setBorderRadius(4.f);
    aiModelBtn->setBg(0xFFFFFFFF);
    aiModelBtn->setHoverBg(0xF5F5F5FF);
    aiModelBtn->onClick.add([this](Ling::Button*) { showAiModelBox(); });

    auto refreshBtn = modelRow->makeChild<Ling::Button>();
    refreshBtn->setText(L"刷新模型");
    refreshBtn->setSize(76.f, 30.f);
    refreshBtn->setFontSize(12.f);
    refreshBtn->setMarginLeft(8.f);
    refreshBtn->setBorder(1.f, 0xD8D8D8FF);
    refreshBtn->setBorderRadius(4.f);
    refreshBtn->setHoverBg(0xF2F2F2FF);
    refreshBtn->onClick.add([this](Ling::Button*) {
        if (!aiApiKeyBox) return;
        auto setting = Setting::get();
        auto provider = setting->getAiProvider();
        auto apiKey = aiApiKeyBox->getText();
        setting->setAiApiKey(provider, apiKey);
        if (aiStatus) aiStatus->setText(L"正在获取 " + AIClient::providerName(provider) + L" 可用模型...");
        auto weakThis = getWeakThis();
        std::thread([this, weakThis, provider = std::move(provider), apiKey = std::move(apiKey)]() mutable {
            auto result = AIClient::listModels(provider, apiKey);
            Ling::App::get()->dq.TryEnqueue([this, weakThis, provider = std::move(provider), result = std::move(result)]() mutable {
                if (!weakThis.lock() || !aiStatus) return;
                if (!result.ok) { aiStatus->setText(result.error); return; }
                aiModels = std::move(result.models);
                auto current = aiModelBtn ? aiModelBtn->getText() : L"";
                if (!current.empty() && std::find(aiModels.begin(), aiModels.end(), current) == aiModels.end())
                    aiModels.insert(aiModels.begin(), current);
                aiStatus->setText(std::format(L"{}：已刷新 {} 个兼容模型", AIClient::providerName(provider), aiModels.size()));
            });
        }).detach();
    });

    aiStatus = makeChild<Ling::Label>();
    aiStatus->setHeight(30.f);
    aiStatus->setWidthPercent(100.f);
    aiStatus->setFontSize(11.f);
    aiStatus->setColor(0x777777FF);

    refreshAiControls();

    auto border = makeChild<Ling::Node>();
    border->setHeight(1.f);
    border->setBg(0xE0E0E0FF);
}

void WinSettingCommon::refreshAiControls()
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

void WinSettingCommon::showAiProviderBox()
{
    if (!aiProviderBtn) return;
    std::vector<std::pair<std::wstring, std::wstring>> items;
    for (const auto& provider : AIClient::providers()) items.emplace_back(provider.name, provider.id);
    showChoiceBox(aiProviderBtn, items, [this](const std::wstring& provider) {
        Setting::get()->setAiProvider(provider);
        refreshAiControls();
    });
}

void WinSettingCommon::showAiModelBox()
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

void WinSettingCommon::initLangCtrls()
{
    auto box = makeChild<Ling::Node>();
    box->setHeight(39.f);
    box->setFlexDirection(Ling::FlexDirection::Row);
    box->setAlignItems(Ling::Align::Center);

    auto label = box->makeChild<Ling::Label>();
    label->setText(Lang::get(L"setting.language"));
    label->setHeightPercent(100.f);
    label->setJustifyContent(Ling::Justify::Center);
    label->setFlexGrow(1.f);

    auto langCode = Setting::get()->getLang();
    auto langs = Lang::get()->getSupportedLang();
    std::wstring langName{ L"简体中文" };
    for (auto& pair:langs) if (pair.second == langCode) { langName = pair.first; break; }

    selectBtn = box->makeChild<Ling::Button>();
    selectBtn->setText(langName);
    selectBtn->setHeight(28.f);
    selectBtn->setWidth(160.f);
    selectBtn->setBorder(1.f, 0xE0E0E0FF);
    selectBtn->setHoverBg(0XFFFFFFFF);
    selectBtn->onClick.add([this](Ling::Button* btn) { this->showSelectBox(btn); });

    auto border = makeChild<Ling::Node>();
    border->setHeight(1.f);
    border->setBg(0xE0E0E0FF);
}

void WinSettingCommon::setAutoStartBtn(Ling::Button* btn)
{
    auto setting = Setting::get();
    auto isAutoStart = setting->getAutoStart();
    if (isAutoStart) {
        btn->setText(L"\ue688");
        btn->setColor(0x597ef7ff);
        btn->setHoverColor(0x597ef7ff);
    }
    else {
        btn->setText(L"\ue687");
        btn->setColor(0x666666FF);
        btn->setHoverColor(0x666666FF);
    }
}

void WinSettingCommon::hideSelectBox()
{
    if (!selectBox) return;
    win->onMouseDown.remove(onMouseDownToken);
    win->body->removeChild(selectBox);
    selectBox = nullptr;
}

void WinSettingCommon::showChoiceBox(Ling::Button* btn,
    const std::vector<std::pair<std::wstring, std::wstring>>& items,
    std::function<void(const std::wstring&)> onChoose)
{
    if (!btn || items.empty()) return;
    hideSelectBox();
    auto weakThis = getWeakThis();
    onMouseDownToken = win->onMouseDown.add([this, weakThis](POINT pos, bool) {
        if (!weakThis.lock() || !selectBox) return;
        if (selectBox->isPosIn(pos)) return;
        hideSelectBox();
    });

    const float itemH = 30.f;
    const float totalH = std::min(320.f, itemH * (float)items.size());
    selectBox = win->body->makeChild<Ling::ScrollerBox>();
    selectBox->setSize(btn->w / win->dpi, totalH);
    selectBox->setPositionType(Ling::Position::Absolute);
    selectBox->setPosition(Ling::Edge::Left, btn->x / win->dpi);
    selectBox->setPosition(Ling::Edge::Top, (btn->y + btn->h) / win->dpi);
    selectBox->setBg(0xFFFFFFFF);
    selectBox->setBorder(1.f, 0x597ef766);

    for (const auto& pair : items) {
        auto itemBtn = selectBox->makeChild<Ling::Button>();
        itemBtn->setText(pair.first);
        itemBtn->setHeight(itemH);
        itemBtn->setWidthPercent(100.f);
        itemBtn->setFontSize(11.f);
        itemBtn->setHoverBg(0Xf2f2f2FF);
        itemBtn->setHoverColor(0X000000FF);
        auto value = pair.second;
        itemBtn->onClick.add([this, weakThis, value = std::move(value), onChoose](Ling::Button*) mutable {
            Ling::App::get()->dq.TryEnqueue([this, weakThis, value = std::move(value), onChoose]() mutable {
                if (!weakThis.lock()) return;
                hideSelectBox();
                onChoose(value);
            });
        });
    }
}

void WinSettingCommon::showSelectBox(Ling::Button* btn)
{
    auto langs = Lang::get()->getSupportedLang();
    std::vector<std::pair<std::wstring, std::wstring>> items;
    for (const auto& pair : langs) items.emplace_back(pair.first, pair.second);
    items.emplace_back(Lang::get(L"setting.getMoreLang"), L"__more__");
    showChoiceBox(btn, items, [this](const std::wstring& value) {
        if (value == L"__more__") {
            std::wstring url{ L"https://github.com/axing1218-source/StarCap/tree/main/Lang" };
            ShellExecute(win->hwnd, L"open", url.data(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        Setting::get()->setLang(value);
        win->close();
        Ling::App::get()->dq.TryEnqueue([]() { WinSetting::init(); });
    });
}

