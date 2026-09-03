#include "pch.h"
#include <thread>
#include "../Lang.h"
#include "../Setting.h"
#include "../GeminiClient.h"
#include "WinSetting.h"
#include "WinSettingCommon.h"

namespace {
    bool settingsDarkMode()
    {
        auto* setting = Setting::get();
        return setting && setting->getToolFlag(L"app", L"darkMode", false);
    }
    uint32_t settingsText() { return settingsDarkMode() ? 0xE8EAEDFF : 0x333333FF; }
    uint32_t settingsMuted() { return settingsDarkMode() ? 0xAEB2B9FF : 0x777777FF; }
    uint32_t settingsSurface() { return settingsDarkMode() ? 0x2B2C30FF : 0xFFFFFFFF; }
    uint32_t settingsBorder() { return settingsDarkMode() ? 0x4A4C52FF : 0xE0E0E0FF; }
    uint32_t settingsHover() { return settingsDarkMode() ? 0x383A3FFF : 0xF5F5F5FF; }
    uint32_t settingsPlaceholder() { return settingsDarkMode() ? 0x8B9098FF : 0xAAAAAAFF; }
}

WinSettingCommon::WinSettingCommon(Ling::WinBase* parent):Ling::Node(parent)
{
    initAutoStartCtrls();
    initCaptureBorderCtrls();
    initGeminiCtrls();
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
    label->setColor(settingsText());
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
    border->setBg(settingsBorder());
}

void WinSettingCommon::initCaptureBorderCtrls()
{
    auto box = makeChild<Ling::Node>();
    box->setHeight(39.f);
    box->setFlexDirection(Ling::FlexDirection::Row);
    box->setAlignItems(Ling::Align::Center);

    auto label = box->makeChild<Ling::Label>();
    label->setText(L"截图边框粗细");
    label->setColor(settingsText());
    label->setHeightPercent(100.f);
    label->setJustifyContent(Ling::Justify::Center);
    label->setFlexGrow(1.f);

    auto minusBtn = box->makeChild<Ling::Button>();
    minusBtn->setText(L"−");
    minusBtn->setColor(settingsText());
    minusBtn->setHoverColor(settingsText());
    minusBtn->setSize(30.f, 28.f);
    minusBtn->setFontSize(16.f);
    minusBtn->setBorder(1.f, settingsBorder());
    minusBtn->setHoverBg(settingsHover());

    borderWidthLabel = box->makeChild<Ling::Label>();
    borderWidthLabel->setWidth(48.f);
    borderWidthLabel->setHeight(28.f);
    borderWidthLabel->setColor(settingsText());
    borderWidthLabel->setAlignItems(Ling::Align::Center);
    borderWidthLabel->setJustifyContent(Ling::Justify::Center);
    auto current = std::clamp(Setting::get()->getToolNum(L"capture", L"borderWidth", 2.f), 0.f, 8.f);
    borderWidthLabel->setText(std::format(L"{:.0f}px", current));

    auto plusBtn = box->makeChild<Ling::Button>();
    plusBtn->setText(L"+");
    plusBtn->setColor(settingsText());
    plusBtn->setHoverColor(settingsText());
    plusBtn->setSize(30.f, 28.f);
    plusBtn->setFontSize(16.f);
    plusBtn->setBorder(1.f, settingsBorder());
    plusBtn->setHoverBg(settingsHover());

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
    border->setBg(settingsBorder());
}

void WinSettingCommon::initGeminiCtrls()
{
    auto keyRow = makeChild<Ling::Node>();
    keyRow->setHeight(44.f);
    keyRow->setFlexDirection(Ling::FlexDirection::Row);
    keyRow->setAlignItems(Ling::Align::Center);

    auto keyLabel = keyRow->makeChild<Ling::Label>();
    keyLabel->setText(L"Gemini API Key");
    keyLabel->setColor(settingsText());
    keyLabel->setHeightPercent(100.f);
    keyLabel->setJustifyContent(Ling::Justify::Center);
    keyLabel->setFlexGrow(1.f);

    geminiApiKeyBox = keyRow->makeChild<Ling::TextBox>();
    geminiApiKeyBox->setSize(230.f, 30.f);
    geminiApiKeyBox->setPadding(6.f);
    geminiApiKeyBox->setFontSize(12.f);
    geminiApiKeyBox->setBg(settingsSurface());
    geminiApiKeyBox->setBorder(1.f, settingsBorder());
    geminiApiKeyBox->setBorderRadius(4.f);
    geminiApiKeyBox->setColor(settingsText());
    geminiApiKeyBox->setCaretColor(settingsText());
    geminiApiKeyBox->setPlaceholderColor(settingsPlaceholder());
    geminiApiKeyBox->setSelectionBgColor(settingsDarkMode() ? 0x596EF766 : 0x99C9EF99);
    geminiApiKeyBox->setPlaceholder(L"粘贴 Gemini API Key");
    geminiApiKeyBox->setText(Setting::get()->getGeminiApiKey());

    auto saveBtn = keyRow->makeChild<Ling::Button>();
    saveBtn->setText(L"保存");
    saveBtn->setColor(settingsText());
    saveBtn->setHoverColor(settingsText());
    saveBtn->setSize(52.f, 30.f);
    saveBtn->setFontSize(12.f);
    saveBtn->setMarginLeft(8.f);
    saveBtn->setBorder(1.f, settingsBorder());
    saveBtn->setBorderRadius(4.f);
    saveBtn->setHoverBg(settingsHover());
    saveBtn->onClick.add([this](Ling::Button*) {
        if (!geminiApiKeyBox || !geminiModelBox) return;
        Setting::get()->setGeminiApiKey(geminiApiKeyBox->getText());
        Setting::get()->setGeminiModel(geminiModelBox->getText());
        if (geminiStatus) geminiStatus->setText(L"Gemini 设置已保存（API Key 已用 Windows 加密保存）");
    });

    auto modelRow = makeChild<Ling::Node>();
    modelRow->setHeight(44.f);
    modelRow->setFlexDirection(Ling::FlexDirection::Row);
    modelRow->setAlignItems(Ling::Align::Center);

    auto modelLabel = modelRow->makeChild<Ling::Label>();
    modelLabel->setText(L"Gemini 模型");
    modelLabel->setColor(settingsText());
    modelLabel->setHeightPercent(100.f);
    modelLabel->setJustifyContent(Ling::Justify::Center);
    modelLabel->setFlexGrow(1.f);

    geminiModelBox = modelRow->makeChild<Ling::TextBox>();
    geminiModelBox->setSize(230.f, 30.f);
    geminiModelBox->setPadding(6.f);
    geminiModelBox->setFontSize(12.f);
    geminiModelBox->setBg(settingsSurface());
    geminiModelBox->setBorder(1.f, settingsBorder());
    geminiModelBox->setBorderRadius(4.f);
    geminiModelBox->setColor(settingsText());
    geminiModelBox->setCaretColor(settingsText());
    geminiModelBox->setSelectionBgColor(settingsDarkMode() ? 0x596EF766 : 0x99C9EF99);
    geminiModelBox->setText(Setting::get()->getGeminiModel());

    auto testBtn = modelRow->makeChild<Ling::Button>();
    testBtn->setText(L"测试连接");
    testBtn->setColor(settingsText());
    testBtn->setHoverColor(settingsText());
    testBtn->setSize(76.f, 30.f);
    testBtn->setFontSize(12.f);
    testBtn->setMarginLeft(8.f);
    testBtn->setBorder(1.f, settingsBorder());
    testBtn->setBorderRadius(4.f);
    testBtn->setHoverBg(settingsHover());
    testBtn->onClick.add([this](Ling::Button*) {
        if (!geminiApiKeyBox || !geminiModelBox) return;
        auto apiKey = geminiApiKeyBox->getText();
        auto model = geminiModelBox->getText();
        Setting::get()->setGeminiApiKey(apiKey);
        Setting::get()->setGeminiModel(model);
        if (geminiStatus) geminiStatus->setText(L"正在连接 Gemini...");
        auto weakThis = getWeakThis();
        std::thread([this, weakThis, apiKey = std::move(apiKey), model = std::move(model)]() mutable {
            auto result = GeminiClient::testConnection(apiKey, model);
            Ling::App::get()->dq.TryEnqueue([this, weakThis, result = std::move(result)]() mutable {
                if (!weakThis.lock() || !geminiStatus) return;
                geminiStatus->setText(result.ok ? L"Gemini 连接成功" : result.message);
            });
        }).detach();
    });

    geminiStatus = makeChild<Ling::Label>();
    geminiStatus->setHeight(28.f);
    geminiStatus->setWidthPercent(100.f);
    geminiStatus->setFontSize(11.f);
    geminiStatus->setColor(settingsMuted());
    geminiStatus->setText(L"翻译功能使用此 API Key；程序不会把 Key 写入源码。默认模型 gemini-3.7-flash");

    auto border = makeChild<Ling::Node>();
    border->setHeight(1.f);
    border->setBg(settingsBorder());
}

void WinSettingCommon::initLangCtrls()
{
    auto box = makeChild<Ling::Node>();
    box->setHeight(39.f);
    box->setFlexDirection(Ling::FlexDirection::Row);
    box->setAlignItems(Ling::Align::Center);

    auto label = box->makeChild<Ling::Label>();
    label->setText(Lang::get(L"setting.language"));
    label->setColor(settingsText());
    label->setHeightPercent(100.f);
    label->setJustifyContent(Ling::Justify::Center);
    label->setFlexGrow(1.f);

    auto langCode = Setting::get()->getLang();
    auto langs = Lang::get()->getSupportedLang();
    std::wstring langName{ L"简体中文" };
    for (auto& pair:langs)
    {
        if (pair.second == langCode) {
            langName = pair.first;
            break;
        }
    }
    selectBtn = box->makeChild<Ling::Button>();
    selectBtn->setText(langName);
    selectBtn->setColor(settingsText());
    selectBtn->setHoverColor(settingsText());
    selectBtn->setBg(settingsDarkMode() ? settingsSurface() : 0x00000000);
    selectBtn->setHeight(28.f);
    selectBtn->setWidth(160.f);
    selectBtn->setBorder(1.f, settingsBorder());
    selectBtn->setHoverBg(settingsSurface());
    selectBtn->onClick.add([this](Ling::Button* btn) {
        if (selectBox) return;
        this->showSelectBox(btn);
        });
    auto border = makeChild<Ling::Node>();
    border->setHeight(1.f);
    border->setBg(settingsBorder());
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
    else
    {
        btn->setText(L"\ue687");
        btn->setColor(settingsDarkMode() ? 0xAEB2B9FF : 0x666666FF);
        btn->setHoverColor(settingsDarkMode() ? 0xAEB2B9FF : 0x666666FF);
    }
}

void WinSettingCommon::hideSelectBox()
{
    if (!selectBox) return;
    win->onMouseDown.remove(onMouseDownToken);
    win->body->removeChild(selectBox);
    selectBox = nullptr;
}

void WinSettingCommon::showSelectBox(Ling::Button* btn)
{
    auto weakThis = getWeakThis();
    onMouseDownToken = win->onMouseDown.add([this,weakThis](POINT pos, bool isRight) {
        if (!weakThis.lock()) return;
        if (!this->selectBox) return;
        if (this->selectBtn->isPosIn(pos)) return;
        if (this->selectBox->isPosIn(pos)) return;
        win->body->removeChild(selectBox);
        this->selectBox = nullptr;
        this->win->onMouseDown.remove(this->onMouseDownToken);
    });
    if (selectBox) {
        win->body->removeChild(selectBox);
    }
    auto langs = Lang::get()->getSupportedLang();
    auto itemH{ 30.f };
    auto totalH = std::min(320.f, itemH * (langs.size()+1));

    selectBox = win->body->makeChild<Ling::ScrollerBox>();
    selectBox->setSize(btn->w/win->dpi, totalH);
    selectBox->setPositionType(Ling::Position::Absolute);
    selectBox->setPosition(Ling::Edge::Left, btn->x/win->dpi);
    selectBox->setPosition(Ling::Edge::Top, btn->y/win->dpi);
    selectBox->setBg(settingsSurface());
    selectBox->setBorder(1.f, settingsDarkMode() ? 0x596EF7AA : 0x597ef766);
    for (auto& pair:langs)
    {
        auto itemBtn = selectBox->makeChild<Ling::Button>();
        itemBtn->setText(pair.first);
        itemBtn->setColor(settingsText());
        itemBtn->setHoverColor(settingsText());
        itemBtn->setHeight(itemH);
        itemBtn->setWidthPercent(100.f);
        itemBtn->setHoverBg(settingsHover());
        itemBtn->onClick.add([this](Ling::Button* itemBtn) {
            auto lang = Lang::get();
            auto langName = itemBtn->getText();
            auto langs = lang->getSupportedLang();
            for (auto& pair : langs)
            {
                if (pair.first == langName) {
                    Setting::get()->setLang(pair.second);
                    win->close();
                    Ling::App::get()->dq.TryEnqueue([this]() {
                        WinSetting::init();
                    });
                    break;
                }
            }
        });
    }
    auto lastItem = selectBox->makeChild<Ling::Button>();
    lastItem->setText(Lang::get(L"setting.getMoreLang"));
    lastItem->setColor(settingsText());
    lastItem->setHoverColor(settingsText());
    lastItem->setHeight(itemH);
    lastItem->setWidthPercent(100.f);
    lastItem->setHoverBg(settingsHover());
    lastItem->onClick.add([this](Ling::Button* btn) {
        win->onMouseDown.remove(onMouseDownToken);
        std::wstring downloadUrl{ L"https://github.com/axing1218-source" };
        ShellExecute(win->hwnd, L"open", downloadUrl.data(), nullptr, nullptr, SW_SHOWNORMAL);
        win->body->removeChild(selectBox);
        selectBox = nullptr;
    });
}

