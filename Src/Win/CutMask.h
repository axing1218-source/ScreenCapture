#pragma once
#include <include/Ling.h>

// 光标落在哪一块。Inside 是选区内部；8 个方向对应四角和四边中点控制柄。
enum class MaskHit { None, Inside, Left, Top, Right, Bottom, TopLeft, TopRight, BottomRight, BottomLeft };

// 框选遮罩。四块半透明遮罩 + 蓝色边框 + 8 个可拖动控制点 + 尺寸标签。
// 框选拖动/调整期间还会在光标附近画像素放大镜；使用的是截图开始时缓存的屏幕图，
// 因此放大镜本身和遮罩永远不会被采样进去。
class CutMask
{
public:
	CutMask(Ling::WinBase* win);
	~CutMask();
	bool highlight(POINT pos);
	void startMakeRect(POINT pos);
	void makeRect(POINT pos);
	MaskHit hitTest(POINT pos) const;
	void startAdjust(POINT pos);
	void adjust(POINT pos);
	bool hasRect() const;
	void paint(ID2D1DeviceContext* ctx);
public:
	D2D1_RECT_F maskRect{};
	float strokeWidth{ 2.f };
	// 选区定死之后（录屏 / 滚动截图）所有辅助 UI 都隐藏，避免录入内容。
	bool hideLabel{ false };
private:
	void initWinRect();
	void makeLayout();
	void paintHandles(ID2D1DeviceContext* ctx);
	void paintMagnifier(ID2D1DeviceContext* ctx);
	std::array<std::pair<D2D1_POINT_2F, MaskHit>, 8> handlePoints() const;
private:
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBorder;
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushText;
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHandle;
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHandleOutline;
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushMagnifierBg;
	Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
	D2D1_RECT_F layoutRect{};
	std::vector<D2D1_RECT_F> winRect;
	POINT pressPos{};
	POINT cursorPos{ INT_MAX, INT_MAX };
	Ling::WinBase* win{ nullptr };
	winrt::event_token onMouseMoveToken{};
	float paddingTop{ 2.f }, paddingMargin{3.f};
	MaskHit adjustHit{ MaskHit::None };
	D2D1_RECT_F adjustStartRect{};
	POINT adjustPressPos{};
	static constexpr float minSize{ 4.f };
};
