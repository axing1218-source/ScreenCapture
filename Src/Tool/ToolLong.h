#pragma once
#include <include/Ling.h>

class WinCap;
class CapLong;
class Tip;
class ToolLong : public Ling::WinBase
{
public:
	ToolLong(WinCap* win, CapLong* capLong);
	~ToolLong();
private:
	void onCreated() override;
	void onClick(Ling::Button* btn);
	void onMinMaxInfo(MINMAXINFO* mmi) override;
	void refreshSize();
private:
	WinCap* win;
	CapLong* capLong;
	bool dpiChanged{ false };
	static constexpr float btnSize{ 32.f };
	std::vector<std::wstring> btnIds = { L"auto",L"ocr",L"pin",L"close",L"save",L"clipboard" };
	std::vector<std::wstring> btnCodes = { L"▶",L"\ue67b",L"\ue6a2",L"\ue62d",L"\ue608",L"\ue6ad" };
	std::unique_ptr<Tip> tip;
};
