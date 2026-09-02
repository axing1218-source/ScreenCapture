#pragma once
#include <include/Ling.h>
class WinSettingCommon:public Ling::Node
{
public:
	WinSettingCommon(Ling::WinBase* parent);
	~WinSettingCommon();
	void hideSelectBox();
private:
	void initAutoStartCtrls();
	void initCaptureBorderCtrls();
	void initGeminiCtrls();
	void initLangCtrls();
	void setAutoStartBtn(Ling::Button* btn);
	void showSelectBox(Ling::Button* btn);
private:
	Ling::Button* selectBtn{ nullptr };
	Ling::Label* borderWidthLabel{ nullptr };
	Ling::TextBox* geminiApiKeyBox{ nullptr };
	Ling::TextBox* geminiModelBox{ nullptr };
	Ling::Label* geminiStatus{ nullptr };
	Ling::ScrollerBox* selectBox{ nullptr };
	winrt::event_token onMouseDownToken;
};
