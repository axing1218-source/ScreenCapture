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
	// 标注工具直接放在截图主工具栏，点击后进入贴图/标注窗口并自动选中对应工具。
	// 顺序：矩形 -> 椭圆 -> 箭头 -> 序号 -> 直线 -> 文字 -> 马赛克 -> 橡皮擦
	//      -> 二维码 -> 录像 -> 长截图 -> 文字识别 -> 翻译 -> 贴到屏幕 -> 关闭 -> 保存 -> 剪切板
	std::vector<std::wstring> btnIds = {
		L"rect",L"ellipse",L"arrow",L"number",L"line",L"text",L"mosaic",L"eraser",L"spliter",
		L"qrcode",L"video",L"long",L"ocr",L"translate",L"spliter",L"pin",L"close",L"save",L"clipboard"
	};
	std::vector<std::wstring> btnCodes = {
		L"\ue8e8",L"\ue6bc",L"\ue603",L"\ue776",L"\ue601",L"\ue6ec",L"\ue82e",L"\ue6be",L"",
		L"\ue71e",L"\ue660",L"\ue73e",L"\ue67b",L"译",L"",L"\ue718",L"\ue62d",L"\ue608",L"\ue6ad"
	};
	std::vector<std::wstring> btnTips = {
		L"tool.rect",L"tool.ellipse",L"tool.arrow",L"tool.number",L"tool.line",L"tool.text",L"tool.mosaic",L"tool.eraser",L"",
		L"cap.qrcode",L"cap.video",L"cap.long",L"cap.ocr",L"",L"",L"tool.pin",L"tool.close",L"tool.save",L"tool.clipboard"
	};
	static constexpr float btnSize{ 32.f };
	static constexpr float spliterW{ 1.f };
};

