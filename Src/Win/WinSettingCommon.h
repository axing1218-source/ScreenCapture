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
		// Gemini can return very long English API errors (notably HTTP 503 high-demand
		// messages). Ling::Label is single-line and its intrinsic text width can push
		// the settings controls off the right edge. Compact known errors before Yoga
		// measures this panel, and cap unknown errors as a final layout guard.
		if (geminiStatus) {
			auto text = geminiStatus->getText();
			std::wstring compact;
			if (text.find(L"HTTP 503") != std::wstring::npos)
				compact = L"连接失败（HTTP 503）：模型当前繁忙，请稍后重试或切换模型";
			else if (text.find(L"HTTP 429") != std::wstring::npos)
				compact = L"连接失败（HTTP 429）：请求过多或额度受限，请稍后重试";
			else if (text.find(L"HTTP 403") != std::wstring::npos)
				compact = L"连接失败（HTTP 403）：API Key 或项目权限受限";
			else if (text.size() > 82)
				compact = text.substr(0, 79) + L"...";
			if (!compact.empty() && compact != text) geminiStatus->setText(compact);
		}

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
