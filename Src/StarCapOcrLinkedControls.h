#pragma once

#include <include/Ling.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Ling {
    class StarCapOcrLinkedTextBox;
    class StarCapOcrLinkedCanvas;
}

namespace StarCapOcrLinkedSelection
{
    class Bridge
    {
    public:
        virtual ~Bridge() = default;
        virtual bool beginImageSelection(Ling::StarCapOcrLinkedCanvas* canvas, POINT pos) = 0;
        virtual void updateImageSelection(Ling::StarCapOcrLinkedCanvas* canvas, POINT pos) = 0;
        virtual void endImageSelection(Ling::StarCapOcrLinkedCanvas* canvas, POINT pos) = 0;
        virtual bool imageHitText(Ling::StarCapOcrLinkedCanvas* canvas, POINT pos) = 0;
        virtual bool handleImageKey(UINT key) = 0;
        virtual void paintImageSelection(Ling::StarCapOcrLinkedCanvas* canvas, ID2D1DeviceContext* ctx) = 0;
        virtual void textSelectionChanged(Ling::StarCapOcrLinkedTextBox* box, int start, int end, bool finished) = 0;
    };

    inline Ling::WinBase* activeBridgeWindow{ nullptr };
    inline Bridge* activeBridge{ nullptr };

    inline Bridge* bridgeFor(Ling::WinBase* win)
    {
        return win && win == activeBridgeWindow ? activeBridge : nullptr;
    }
}

namespace Ling
{
    class StarCapOcrLinkedCanvas : public Canvas
    {
    public:
        explicit StarCapOcrLinkedCanvas(WinBase* win) : Canvas(win)
        {
            downTok = win->onMouseDown.add([this](POINT pos, bool isRight) {
                if (isRight || !isPosIn(pos)) return;
                auto* bridge = StarCapOcrLinkedSelection::bridgeFor(win);
                if (!bridge) return;
                selectingText = bridge->beginImageSelection(this, pos);
            });
            moveTok = win->onMouseMove.add([this](POINT pos) {
                if (!selectingText) return;
                if (auto* bridge = StarCapOcrLinkedSelection::bridgeFor(win))
                    bridge->updateImageSelection(this, pos);
            });
            upTok = win->onMouseUp.add([this](POINT pos, bool isRight) {
                if (isRight || !selectingText) return;
                selectingText = false;
                if (auto* bridge = StarCapOcrLinkedSelection::bridgeFor(win))
                    bridge->endImageSelection(this, pos);
            });
            cursorTok = win->onCursor.add([this](bool* handled) {
                if (!handled || selectingText) return;
                POINT pos{};
                GetCursorPos(&pos);
                ScreenToClient(win->hwnd, &pos);
                auto* bridge = StarCapOcrLinkedSelection::bridgeFor(win);
                if (!bridge || !bridge->imageHitText(this, pos)) return;
                *handled = true;
                SetCursor(LoadCursor(nullptr, IDC_IBEAM));
            });
            keyTok = win->onKeyDown.add([this](UINT key) {
                if (auto* bridge = StarCapOcrLinkedSelection::bridgeFor(win))
                    bridge->handleImageKey(key);
            });
        }

        ~StarCapOcrLinkedCanvas()
        {
            win->onMouseDown.remove(downTok);
            win->onMouseMove.remove(moveTok);
            win->onMouseUp.remove(upTok);
            win->onCursor.remove(cursorTok);
            win->onKeyDown.remove(keyTok);
        }

        ID2D1DeviceContext* startPaint()
        {
            currentPaint = Canvas::startPaint();
            return currentPaint;
        }

        void finishPaint()
        {
            if (currentPaint) {
                if (auto* bridge = StarCapOcrLinkedSelection::bridgeFor(win))
                    bridge->paintImageSelection(this, currentPaint);
            }
            Canvas::finishPaint();
            currentPaint = nullptr;
        }

        bool isSelectingLinkedText() const { return selectingText; }

    private:
        ID2D1DeviceContext* currentPaint{ nullptr };
        bool selectingText{ false };
        winrt::event_token downTok{}, moveTok{}, upTok{}, cursorTok{}, keyTok{};
    };

    class StarCapOcrLinkedTextBox : public TextBox
    {
    public:
        explicit StarCapOcrLinkedTextBox(WinBase* win) : TextBox(win)
        {
            // Draw the image-driven selection above TextBox's own text surface.  It is
            // transparent except for the linked selection rectangles.
            overlay = Node::makeChild<Canvas>();
            overlay->setPositionType(Position::Absolute);
            overlay->setPosition(Edge::Left, 0.f);
            overlay->setPosition(Edge::Top, 0.f);
            overlay->setPosition(Edge::Right, 0.f);
            overlay->setPosition(Edge::Bottom, 0.f);

            downTok = win->onMouseDown.add([this](POINT pos, bool isRight) { onLinkedDown(pos, isRight); });
            moveTok = win->onMouseMove.add([this](POINT pos) { onLinkedMove(pos); });
            upTok = win->onMouseUp.add([this](POINT pos, bool isRight) { onLinkedUp(pos, isRight); });
            wheelTok = win->onMouseWheel.add([this](POINT pos, float space) { onLinkedWheel(pos, space); });
            keyTok = win->onKeyDown.add([this](UINT key) { onLinkedKey(key); });
            sizeTok = win->onSizeChanged.add([this]() { rebuildLinkedLayout(true); paintLinkedOverlay(); });
            focusTok = onFocusChanged.add([this](TextBox*, bool focused) {
                if (focused) return;
                mirrorAnchor = mirrorCaret;
                if (auto* bridge = StarCapOcrLinkedSelection::bridgeFor(win))
                    bridge->textSelectionChanged(this, mirrorCaret, mirrorCaret, true);
            });
            textTok = onTextChanged.add([this](TextBox*, const std::wstring& value) {
                mirrorText = value;
                mirrorCaret = std::clamp(mirrorCaret, 0, (int)mirrorText.size());
                mirrorAnchor = mirrorCaret;
                linkedStart = linkedEnd = -1;
                rebuildLinkedLayout(true);
                paintLinkedOverlay();
            });
        }

        ~StarCapOcrLinkedTextBox()
        {
            win->onMouseDown.remove(downTok);
            win->onMouseMove.remove(moveTok);
            win->onMouseUp.remove(upTok);
            win->onMouseWheel.remove(wheelTok);
            win->onKeyDown.remove(keyTok);
            win->onSizeChanged.remove(sizeTok);
            onFocusChanged.remove(focusTok);
            onTextChanged.remove(textTok);
        }

        // Hide the base setters used by the OCR window so our mirror layout always has
        // the same font/padding as the real TextBox.
        void setText(const std::wstring& value)
        {
            TextBox::setText(value);
            mirrorText = value;
            mirrorCaret = std::clamp(mirrorCaret, 0, (int)mirrorText.size());
            mirrorAnchor = mirrorCaret;
            rebuildLinkedLayout(true);
            paintLinkedOverlay();
        }

        void setFontSize(float value)
        {
            mirrorFontSize = value;
            TextBox::setFontSize(value);
            rebuildLinkedLayout(true);
            paintLinkedOverlay();
        }

        void setFontFamily(const std::wstring& value)
        {
            mirrorFontFamily = value;
            TextBox::setFontFamily(value);
            rebuildLinkedLayout(true);
            paintLinkedOverlay();
        }

        void setPadding(float value)
        {
            Node::setPadding(value);
            rebuildLinkedLayout(true);
            paintLinkedOverlay();
        }

        void setPadding(float left, float top, float right, float bottom)
        {
            Node::setPadding(left, top, right, bottom);
            rebuildLinkedLayout(true);
            paintLinkedOverlay();
        }

        void selectAll()
        {
            TextBox::selectAll();
            mirrorAnchor = 0;
            mirrorCaret = (int)mirrorText.size();
            notifyTextSelection(false);
        }

        void setLinkedSelection(int start, int end, bool scrollIntoView)
        {
            const int n = (int)mirrorText.size();
            start = std::clamp(start, 0, n);
            end = std::clamp(end, 0, n);
            if (end < start) std::swap(start, end);
            linkedStart = start;
            linkedEnd = end;
            rebuildLinkedLayout(false);
            if (scrollIntoView && linkedEnd > linkedStart)
                scrollLinkedRangeIntoView(linkedStart, linkedEnd);
            paintLinkedOverlay();
        }

        void clearLinkedSelection()
        {
            if (linkedStart < 0 && linkedEnd < 0) return;
            linkedStart = linkedEnd = -1;
            paintLinkedOverlay();
        }

        int linkedSelectionStart() const { return linkedStart; }
        int linkedSelectionEnd() const { return linkedEnd; }

    private:
        float contentWidthPx() const
        {
            auto [pl, pt, pr, pb] = const_cast<StarCapOcrLinkedTextBox*>(this)->getPadding();
            return std::max(1.f, w - (pl + pr) * win->dpi);
        }

        float contentHeightPx() const
        {
            auto [pl, pt, pr, pb] = const_cast<StarCapOcrLinkedTextBox*>(this)->getPadding();
            return std::max(1.f, h - (pt + pb) * win->dpi);
        }

        float linkedTextHeight() const
        {
            if (!linkedLayout) return 0.f;
            DWRITE_TEXT_METRICS metrics{};
            linkedLayout->GetMetrics(&metrics);
            return metrics.height;
        }

        float maxMirrorScroll() const
        {
            return std::max(0.f, linkedTextHeight() - contentHeightPx());
        }

        void rebuildLinkedLayout(bool force)
        {
            const float targetW = contentWidthPx();
            if (!force && linkedLayout && std::fabs(targetW - lastLinkedWidth) < .5f)
                return;
            lastLinkedWidth = targetW;
            linkedLayout.Reset();
            if (mirrorText.empty()) return;
            auto* d2d = D2D::get();
            if (!d2d || !d2d->dwriteFactory) return;
            auto* format = d2d->getTextFormat(mirrorFontFamily);
            if (!format) return;
            if (FAILED(d2d->dwriteFactory->CreateTextLayout(mirrorText.data(), (UINT32)mirrorText.size(),
                format, targetW, FLT_MAX, linkedLayout.ReleaseAndGetAddressOf()))) return;
            linkedLayout->SetFontSize(mirrorFontSize * win->dpi, { 0, INT_MAX });
            if (!mirrorFontFamily.empty())
                linkedLayout->SetFontFamilyName(mirrorFontFamily.data(), { 0, INT_MAX });
            mirrorScroll = std::clamp(mirrorScroll, 0.f, maxMirrorScroll());
        }

        int linkedHitIndex(POINT pos)
        {
            rebuildLinkedLayout(false);
            if (!linkedLayout) return 0;
            auto [pl, pt, pr, pb] = getPadding();
            const float localX = pos.x - x - pl * win->dpi;
            const float localY = pos.y - y - pt * win->dpi + mirrorScroll;
            BOOL trailing = FALSE, inside = FALSE;
            DWRITE_HIT_TEST_METRICS hit{};
            if (FAILED(linkedLayout->HitTestPoint(localX, localY, &trailing, &inside, &hit)))
                return std::clamp(mirrorCaret, 0, (int)mirrorText.size());
            int index = (int)hit.textPosition + (trailing ? (int)hit.length : 0);
            return std::clamp(index, 0, (int)mirrorText.size());
        }

        void updateMirrorCaret(int index, bool extend)
        {
            index = std::clamp(index, 0, (int)mirrorText.size());
            if (!extend) mirrorAnchor = index;
            mirrorCaret = index;
            rebuildLinkedLayout(false);
            if (!linkedLayout) return;
            FLOAT cx = 0.f, cy = 0.f;
            DWRITE_HIT_TEST_METRICS hit{};
            if (SUCCEEDED(linkedLayout->HitTestTextPosition((UINT32)mirrorCaret, FALSE, &cx, &cy, &hit))) {
                const float visH = contentHeightPx();
                if (cy < mirrorScroll) mirrorScroll = cy;
                else if (cy + hit.height > mirrorScroll + visH)
                    mirrorScroll = cy + hit.height - visH;
                mirrorScroll = std::clamp(mirrorScroll, 0.f, maxMirrorScroll());
            }
        }

        bool isOnOwnScrollbar(POINT pos)
        {
            rebuildLinkedLayout(false);
            if (maxMirrorScroll() <= .5f) return false;
            const float barW = 6.f * win->dpi;
            return pos.x >= x + w - barW && pos.x < x + w && pos.y >= y && pos.y < y + h;
        }

        void onLinkedDown(POINT pos, bool isRight)
        {
            if (isRight || !visual.IsVisible() || !isPosIn(pos) || isOnOwnScrollbar(pos)) return;
            mirrorDragging = true;
            linkedStart = linkedEnd = -1;
            updateMirrorCaret(linkedHitIndex(pos), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            notifyTextSelection(false);
            paintLinkedOverlay();
        }

        void onLinkedMove(POINT pos)
        {
            if (!mirrorDragging) return;
            updateMirrorCaret(linkedHitIndex(pos), true);
            notifyTextSelection(false);
        }

        void onLinkedUp(POINT, bool isRight)
        {
            if (isRight || !mirrorDragging) return;
            mirrorDragging = false;
            notifyTextSelection(true);
        }

        void onLinkedWheel(POINT pos, float space)
        {
            if (!visual.IsVisible() || !isPosIn(pos)) return;
            rebuildLinkedLayout(false);
            if (maxMirrorScroll() <= .5f) return;
            mirrorScroll = std::round(std::clamp(mirrorScroll - space, 0.f, maxMirrorScroll()));
            paintLinkedOverlay();
        }

        void onLinkedKey(UINT key)
        {
            if (!isFocused()) return;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (ctrl && key == 'A') {
                mirrorAnchor = 0;
                mirrorCaret = (int)mirrorText.size();
                notifyTextSelection(true);
                return;
            }
            if (key == VK_LEFT || key == VK_RIGHT) {
                int next = mirrorCaret + (key == VK_LEFT ? -1 : 1);
                updateMirrorCaret(next, shift);
                notifyTextSelection(true);
            }
            else if (key == VK_HOME) {
                updateMirrorCaret(0, shift);
                notifyTextSelection(true);
            }
            else if (key == VK_END) {
                updateMirrorCaret((int)mirrorText.size(), shift);
                notifyTextSelection(true);
            }
        }

        void notifyTextSelection(bool finished)
        {
            int a = std::min(mirrorAnchor, mirrorCaret);
            int b = std::max(mirrorAnchor, mirrorCaret);
            if (auto* bridge = StarCapOcrLinkedSelection::bridgeFor(win))
                bridge->textSelectionChanged(this, a, b, finished);
        }

        void scrollLinkedRangeIntoView(int start, int end)
        {
            rebuildLinkedLayout(false);
            if (!linkedLayout || start >= end) return;
            FLOAT x1 = 0.f, y1 = 0.f, x2 = 0.f, y2 = 0.f;
            DWRITE_HIT_TEST_METRICS h1{}, h2{};
            linkedLayout->HitTestTextPosition((UINT32)start, FALSE, &x1, &y1, &h1);
            linkedLayout->HitTestTextPosition((UINT32)std::max(start, end - 1), TRUE, &x2, &y2, &h2);
            const float visH = contentHeightPx();
            float desired = mirrorScroll;
            if (y1 < desired) desired = y1;
            const float bottom = std::max(y1 + h1.height, y2 + h2.height);
            if (bottom > desired + visH) desired = bottom - visH;
            desired = std::round(std::clamp(desired, 0.f, maxMirrorScroll()));
            const float delta = mirrorScroll - desired;
            if (std::fabs(delta) <= .5f) return;

            // Feed the same wheel event to the real TextBox so its private scrollY stays
            // aligned with our overlay without reaching into Ling internals.
            POINT inside{ (LONG)std::lround(x + std::min(w * .5f, std::max(8.f, w - 12.f))),
                (LONG)std::lround(y + std::min(h * .5f, std::max(8.f, h - 12.f))) };
            win->onMouseWheel(inside, delta);
        }

        void paintLinkedOverlay()
        {
            if (!overlay) return;
            auto* ctx = overlay->startPaint();
            if (!ctx) return;
            ctx->Clear(0);
            rebuildLinkedLayout(false);
            if (linkedLayout && linkedStart >= 0 && linkedEnd > linkedStart) {
                auto [pl, pt, pr, pb] = getPadding();
                const float ox = pl * win->dpi;
                const float oy = pt * win->dpi - mirrorScroll;
                const float cw = contentWidthPx();
                const float ch = contentHeightPx();
                ctx->PushAxisAlignedClip(D2D1::RectF(ox, pt * win->dpi, ox + cw, pt * win->dpi + ch),
                    D2D1_ANTIALIAS_MODE_ALIASED);
                UINT32 count = 0;
                linkedLayout->HitTestTextRange((UINT32)linkedStart, (UINT32)(linkedEnd - linkedStart),
                    ox, oy, nullptr, 0, &count);
                if (count) {
                    std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
                    if (SUCCEEDED(linkedLayout->HitTestTextRange((UINT32)linkedStart,
                        (UINT32)(linkedEnd - linkedStart), ox, oy, hits.data(), count, &count))) {
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
                        ctx->CreateSolidColorBrush(D2D1::ColorF(0.086f, 0.467f, 1.f, 0.26f), brush.GetAddressOf());
                        if (brush) {
                            for (UINT32 i = 0; i < count; ++i) {
                                const auto& m = hits[i];
                                if (m.width > 0.f && m.height > 0.f)
                                    ctx->FillRectangle(D2D1::RectF(m.left, m.top, m.left + m.width, m.top + m.height), brush.Get());
                            }
                        }
                    }
                }
                ctx->PopAxisAlignedClip();
            }
            overlay->finishPaint();
        }

    private:
        Canvas* overlay{ nullptr };
        Microsoft::WRL::ComPtr<IDWriteTextLayout> linkedLayout;
        std::wstring mirrorText;
        std::wstring mirrorFontFamily;
        float mirrorFontSize{ 14.f };
        float lastLinkedWidth{ -1.f };
        float mirrorScroll{ 0.f };
        int mirrorAnchor{ 0 }, mirrorCaret{ 0 };
        int linkedStart{ -1 }, linkedEnd{ -1 };
        bool mirrorDragging{ false };
        winrt::event_token downTok{}, moveTok{}, upTok{}, wheelTok{}, keyTok{}, sizeTok{}, focusTok{}, textTok{};
    };
}
