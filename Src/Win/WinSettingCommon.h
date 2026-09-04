#pragma once
#include <include/Ling.h>
#include "../StarCapTranslationLanguageSettings.h"

class WinSettingCommon:public Ling::Node
{
public:
	WinSettingCommon(Ling::WinBase* parent);
	~WinSettingCommon();
	void hideSelectBox();
protected:
	void layout() override
	{
		// Never mutate the Yoga/UI tree while Node::layout() is walking it.  The
		// previous test build inserted the translation-language row synchronously
		// here, which could invalidate the active layout traversal and crash as soon
		// as Settings opened.  Queue the one-time injection for the next UI turn.
		if (!translationLanguageInjected) {
			translationLanguageInjected = true;
			auto weakThis = getWeakThis();
			auto* self = this;
			Ling::App::get()->dq.TryEnqueue([self, weakThis]() {
				if (!weakThis.lock()) return;
				StarCapTranslationLanguageSettings::attach(self);
				if (self->win) self->win->refresh();
			});
		}
		Ling::Node::layout();
	}
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
	bool translationLanguageInjected{ false };
};
