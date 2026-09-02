#pragma once
#include <include/Ling.h>
#include "../StarCapOcr.h"

class WinCap;
class ToolLong;

class CapLong
{
public:
	CapLong(WinCap* win);
	~CapLong();
	void dispose();
	void onMove(POINT pos);
	void onUp(POINT pos);
	void onTimerCB(UINT timerId);
	void setCursor();
	void paint(ID2D1DeviceContext* ctx);
	void copyToClipboard();
	bool saveToFile();
	void pin();
	bool hasImage() const { return !imgData.empty(); }
	void layoutTool();
	// Manual capture is the default. This switches the already-running session to automatic scrolling.
	void startAutoScroll();
	// Freeze the current stitched image and open the same OCR result flow used by normal screenshots.
	bool ocr()
	{
		if (imgData.empty() || imgW <= 0 || resultH <= 0) return false;
		stopCap();
		auto data = imgData;
		StarCapOcr::showPixels(std::move(data), imgW, resultH, true);
		return true;
	}
	bool translate()
	{
		if (imgData.empty() || imgW <= 0 || resultH <= 0) return false;
		stopCap();
		auto data = imgData;
		StarCapOcr::showTranslationPixels(std::move(data), imgW, resultH, true);
		return true;
	}
private:
	void firstStep();
	void makeImgPreview();
	void capStep();
	void scheduleNextCapture(int delayMs = 120);
	void makeTool();
	void paintImgPreview(ID2D1DeviceContext* ctx);
	void stopCap();
	void makeStopText();
private:
	WinCap* win;
	bool isCapturing{ false }, isFinish{ false }, autoScroll{ false };
	bool firstCheck{ true };
	int dismissTime{ 0 };
	int settleRecheckCount{ 0 };
	int changeStartY{ -1 };
	D2D1_RECT_F stopTextRect{};
	D2D1_POINT_2F stopTextPos{};
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
	Microsoft::WRL::ComPtr<IDWriteTextLayout> layoutTextEnd;
	HWND targetHwnd{ nullptr };
	std::unique_ptr<ToolLong> tool;
	Microsoft::WRL::ComPtr<ID2D1Bitmap1> imgPreview;
	std::vector<BYTE> imgData;
	std::vector<BYTE> img1;
	int imgW{ 0 }, imgH{ 0 };
	int resultH{ 0 };
	POINT capStartPos{};
};



