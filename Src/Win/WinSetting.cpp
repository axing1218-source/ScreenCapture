#include "pch.h"
#include <filesystem>
#include "../App.h"
#include "../Lang.h"
#include "../Setting.h"
#include "../ClipboardHistory.h"
#include "WinSetting.h"
#include "WinSettingCommon.h"
#include "WinSettingShortcut.h"
#include "WinSettingAbout.h"

std::unique_ptr<WinSetting> winSetting;

namespace {
	bool useDarkMode()
	{
		auto* setting = Setting::get();
		return setting && setting->getToolFlag(L"app", L"darkMode", false);
	}

	uint32_t pageBg() { return useDarkMode() ? 0x202124FF : 0xFAFAFAFF; }
	uint32_t sideBg() { return useDarkMode() ? 0x292A2DFF : 0xEEEEF0FF; }
	uint32_t normalText() { return useDarkMode() ? 0xE8EAEDFF : 0x333333FF; }
	uint32_t hoverBg() { return useDarkMode() ? 0x383A3FFF : 0xE1E1E3FF; }

	void applyContentTheme(Ling::Node* node)
	{
		if (!node) return;
		node->setBg(pageBg());
		node->setColor(normalText());
	}
}

WinSetting::WinSetting() :Ling::WinBase()
{
	// 关窗按钮是 body 的子节点，而 close() 正是从它的点击回调里一路进来的 ——
	// 在那里同步 winSetting.reset() 就是 use-after-free，所以推迟到下一轮消息循环。
	// 不放掉的话这个对象会一直活着，Ling 那边就永远看不到"一个窗口都不剩"，D2D 设备
	// 也就永远还不回去
	onDestroy.add([]() {
		Ling::App::get()->dq.TryEnqueue([]() { winSetting.reset(); });
	});
	setTitle(Lang::get(L"setting.title"));
	setSize(680, 560);
	setCenter();
	createNativeWindow();
}

WinSetting::~WinSetting()
{

}

void WinSetting::init()
{
	// 已经开着就拉到前台，不建第二个。原来是"关掉旧的再建新的"，那样会和上面那个
	// 延迟释放撞车：排在队列里的 reset 跑起来时放掉的是刚建好的这一个
	if (winSetting) {
		SetForegroundWindow(winSetting->hwnd);
		return;
	}
	winSetting.reset(new WinSetting());
}

void WinSetting::dispose()
{
	winSetting.reset();
}

void WinSetting::onCreated()
{
	enableShadow();
	body->setBg(pageBg());
	body->setColor(normalText());
	body->setFlexDirection(Ling::FlexDirection::Row);
	auto menuBox = body->makeChild<Ling::Node>();
	menuBox->setBg(sideBg());
	menuBox->setColor(normalText());
	menuBox->setWidth(160.f);
	menuBox->setHeightPercent(100.f);
	menuBox->setPaddingTop(40.f);
	initMenuItems(menuBox);

	content = body->makeChild<WinSettingCommon>();
	applyContentTheme(content);
	content->setFlexGrow(1.0);
	content->setHeightPercent(100.f);
	content->setPaddingTop(40.f);
	content->setPadding(20.f, 40.f, 20.f, 40.f);
	content->setFlexDirection(Ling::FlexDirection::Column);

	auto closeBtn = body->makeChild<Ling::Button>();
	closeBtn->setSize(42.f, 32.f);
	closeBtn->setPositionType(Ling::Position::Absolute);
	closeBtn->setPosition(Ling::Edge::Right, 0);
	closeBtn->setPosition(Ling::Edge::Top, 0);
	closeBtn->setColor(normalText());
	closeBtn->setHoverColor(0xFFFFFFFF);
	closeBtn->setHoverBg(0xE81123FF);
	closeBtn->setText(L"\ue62d");
	closeBtn->setFontFamily(L"icon");
	closeBtn->onClick.add([](Ling::Button* btn) {
		btn->win->close();
		});
	show();
}
void WinSetting::initMenuItems(Ling::Node* menuBox)
{
	const bool dark = useDarkMode();
	for (size_t i = 0; i < 3; i++)
	{
		auto menuItem = menuBox->makeChild<Ling::Button>();
		menuItem->setFontSize(14.f);
		menuItem->setHeight(40.f);
		if (i == 0) {
			menuItem->setColor(0xFFFFFFFF);
			menuItem->setBg(0x597ef7ff);
			menuItem->setHoverColor(0xFFFFFFFF);
			menuItem->setHoverBg(0x597ef7ff);
			menuItem->setText(Lang::get(L"setting.common"));
		}
		else {
			menuItem->setColor(normalText());
			menuItem->setHoverColor(normalText());
			menuItem->setHoverBg(hoverBg());
			if (i == 1) {
				menuItem->setText(Lang::get(L"setting.shortcut"));
			}
			else if (i == 2) {
				menuItem->setText(Lang::get(L"setting.about"));
			}
		}
		menuItem->onClick.add([this](auto menuItem) {this->onMenuItemClick(menuItem);});
		menus.push_back(menuItem);
	}

	// Put the app theme switch inside Settings without adding another settings page.
	// A flex spacer keeps it at the bottom of the left rail where it stays visible
	// regardless of which settings section is selected.
	auto spacer = menuBox->makeChild<Ling::Node>();
	spacer->setFlexGrow(1.f);

	auto themeBtn = menuBox->makeChild<Ling::Button>();
	themeBtn->setHeight(46.f);
	themeBtn->setFontSize(13.f);
	themeBtn->setColor(dark ? 0xDDE3FFFF : 0x4A4F5AFF);
	themeBtn->setHoverColor(dark ? 0xFFFFFFFF : 0x202124FF);
	themeBtn->setHoverBg(dark ? 0x383A3FFF : 0xE1E1E3FF);
	themeBtn->setText(dark ? L"☾  深色模式：开" : L"☾  深色模式：关");
	themeBtn->onClick.add([](Ling::Button* btn) {
		auto* setting = Setting::get();
		if (!setting) return;
		const bool next = !setting->getToolFlag(L"app", L"darkMode", false);
		setting->setToolFlag(L"app", L"darkMode", next);
		ClipboardHistory::v099RefreshTheme();

		// Rebuild the settings surface so every static color is recreated from the
		// selected palette. Clipboard windows are repainted immediately above.
		btn->win->close();
		Ling::App::get()->dq.TryEnqueue([]() { WinSetting::init(); });
	});
}
void WinSetting::onMenuItemClick(Ling::Button* menuItem)
{
	auto index = Ling::Util::getIndex(menus, menuItem);
	if (index < 0 || index == menuIndex) return;
	// 通用设置里的语言下拉框是挂在 body 上的（要能盖住下面的控件），content 被换掉
	// 它不会跟着消失，所以切菜单之前先收掉
	if (menuIndex == 0) {
		static_cast<WinSettingCommon*>(content)->hideSelectBox();
	}
	auto oldItem = menus[menuIndex];
	oldItem->setColor(normalText());
	oldItem->setBg(0x00000000);
	oldItem->setHoverColor(normalText());
	oldItem->setHoverBg(hoverBg());
	menuIndex = index;
	menuItem->setColor(0xFFFFFFFF);
	menuItem->setBg(0x597ef7ff);
	menuItem->setHoverColor(0xFFFFFFFF);
	menuItem->setHoverBg(0x597ef7ff);

	body->removeChild(content);
	if (menuIndex == 0) {
		content = body->makeChild<WinSettingCommon>();
	}
	else if (menuIndex == 1) {
		content = body->makeChild<WinSettingShortcut>();
	}
	else if (menuIndex == 2) {
		content = body->makeChild<WinSettingAbout>();
	}
	applyContentTheme(content);
	content->setFlexGrow(1.0);
	content->setHeightPercent(100.f);
	content->setPadding(20.f,40.f,20.f,40.f);
	content->setFlexDirection(Ling::FlexDirection::Column);
}

LRESULT WinSetting::onHitTest(const POINT pos)
{
	POINT pt = pos;
	ScreenToClient(hwnd, &pt);
	if (!isMaximized) {
		auto result = borderHitTest(pt);
		if (result != HTCLIENT) return result;
	}
	if (pt.x > 0 && pt.y > 0 && pt.x < w - 32 * dpi && pt.y < 40 * dpi) {
		return HTCAPTION;
	}
	if (pt.x > 0 && pt.y > 40*4*dpi && pt.x < 120 * dpi && pt.y < h) {
		return HTCAPTION;
	}
	return HTCLIENT;
}


