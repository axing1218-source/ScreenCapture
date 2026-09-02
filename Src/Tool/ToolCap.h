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
	// Primary feature order is fixed and identical wherever this standard capture toolbar is used:
	// 图像标记 -> 二维码 -> 录像 -> 长截图 -> 文字识别 -> 翻译 -> 贴到屏幕 -> 关闭 -> 保存 -> 剪切板
	std::vector<std::wstring> btnIds = { L"mark",L"qrcode",L"video",L"long",L"ocr",L"translate",L"spliter",L"pin",L"close",L"save",L"clipboard" };
	std::vector<std::wstring> btnCodes = { L"\ue97f",L"\ue71e",L"\ue660",L"\ue73e",L"\ue67b",L"译",L"",L"\ue718",L"\ue62d",L"\ue608",L"\ue6ad" };
	std::vector<std::wstring> btnTips = { L"cap.mark",L"cap.qrcode",L"cap.video",L"cap.long",L"cap.ocr",L"",L"",L"tool.pin",L"tool.close",L"tool.save",L"tool.clipboard" };
	static constexpr float btnSize{ 32.f };
	static constexpr float spliterW{ 1.f };
};

