#include "pch.h"
#include "../Lang.h"
#include "WinSetting.h"
#include "WinSettingAbout.h"
#include "../Util.h"
#include "../Version.h"

WinSettingAbout::WinSettingAbout(Ling::WinBase* parent):Ling::Node(parent)
{
    std::vector<std::wstring> keys = { L"version",L"project",L"author" };
    for (auto& key : keys)
    {
        auto box = makeChild<Ling::Node>();
        box->setHeight(39.f);
        box->setFlexDirection(Ling::FlexDirection::Row);
        box->setAlignItems(Ling::Align::Center);

        auto label = box->makeChild<Ling::Label>();
        label->setText(Lang::get(L"about." + key));
        label->setHeightPercent(100.f);
        label->setJustifyContent(Ling::Justify::Center);
        label->setFlexGrow(1.f);

        auto btn = box->makeChild<Ling::Button>();
        btn->setId(key);
        if (key == L"version") {
            btn->setText(STARCAP_DISPLAY_VERSION);
        }
        else if (key == L"project") {
            // Until the repository is detached/renamed to StarCap, show the
            // maintainer's GitHub profile instead of exposing the legacy repo name.
            btn->setText(L"github.com/axing1218-source");
            btn->setColor(0x597ef7ff);
            btn->setHoverColor(0x597ef7ff);
            btn->onClick.add([this](Ling::Button* btn) {
                std::wstring projectUrl{ L"https://github.com/axing1218-source" };
                ShellExecute(win->hwnd, L"open", projectUrl.data(), nullptr, nullptr, SW_SHOWNORMAL);
                });
        }
        else {
            btn->setText(L"阿星");
        }
        btn->setAlignItems(Ling::Align::FlexEnd);
        btn->setHeight(28.f);
        btn->setWidth(180.f);
        btn->setBg(0);
        btn->setHoverBg(0);
        btns.push_back(btn);

        auto border = makeChild<Ling::Node>();
        border->setHeight(1.f);
        border->setBg(0xE0E0E0FF);
    }
}

WinSettingAbout::~WinSettingAbout()
{

}
