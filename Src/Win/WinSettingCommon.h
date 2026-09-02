#pragma once
#include <include/Ling.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class WinSettingCommon:public Ling::Node
{
public:
	WinSettingCommon(Ling::WinBase* parent);
	~WinSettingCommon();
	void hideSelectBox();
private:
	void initAutoStartCtrls();
	void initCaptureBorderCtrls();
	void initAiCtrls();
	void initLangCtrls();
	void setAutoStartBtn(Ling::Button* btn);
	void refreshAiControls();
	void showAiProviderBox();
	void showAiModelBox();
	void showSelectBox(Ling::Button* btn);
	void showChoiceBox(Ling::Button* btn,
		const std::vector<std::pair<std::wstring, std::wstring>>& items,
		std::function<void(const std::wstring&)> onChoose);
private:
	Ling::Button* selectBtn{ nullptr };
	Ling::Label* borderWidthLabel{ nullptr };
	Ling::Button* aiProviderBtn{ nullptr };
	Ling::Label* aiKeyLabel{ nullptr };
	Ling::TextBox* aiApiKeyBox{ nullptr };
	Ling::Button* aiModelBtn{ nullptr };
	Ling::Label* aiStatus{ nullptr };
	std::vector<std::wstring> aiModels;
	Ling::ScrollerBox* selectBox{ nullptr };
	winrt::event_token onMouseDownToken;
};
