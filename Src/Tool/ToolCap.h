#pragma once
#include <include/Ling.h>

class WinCap;
class Tip;
// 框选完成后出现在选区右下方的工具条。
class ToolCap : public Ling::WinBase
{
public:
	ToolCap(WinCap* win);
	~ToolCap();
private:
	void onCreated() override;
	void onMinMaxInfo(MINMAXINFO* mmi) override;
	void onClick(Ling::Button* btn);
	void refreshSize();
private:
	WinCap* win;
	bool dpiChanged{ false };
	std::unique_ptr<Tip> tip;
	// “translate” 放在 OCR 右侧：第一次点击翻译，之后同一按钮原文/译文反复切换。
	std::vector<std::wstring> btnIds = { L"mark",L"long",L"video",L"ocr",L"translate",L"qrcode",L"spliter",L"close",L"save",L"clipboard" };
	std::vector<std::wstring> btnCodes = { L"\ue97f",L"\ue73e",L"\ue660",L"\ue67b",L"译",L"\ue71e",L"",L"\ue62d",L"\ue608",L"\ue6ad" };
	std::vector<std::wstring> btnTips = { L"cap.mark",L"cap.long",L"cap.video",L"cap.ocr",L"",L"cap.qrcode",L"",L"tool.close",L"tool.save",L"tool.clipboard" };
	static constexpr float btnSize{ 32.f };
	static constexpr float spliterW{ 1.f };
};
