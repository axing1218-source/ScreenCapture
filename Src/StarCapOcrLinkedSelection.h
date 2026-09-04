#pragma once

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>
#include "StarCapOcrLinkedControls.h"

namespace StarCapOcrLinkedSelection
{
    class OcrBridge final : public Bridge
    {
    public:
        explicit OcrBridge(StarCapOcrV2::OcrResultWindow* owner) : owner(owner) {}

        bool beginImageSelection(Ling::StarCapOcrLinkedCanvas* canvas, POINT pos) override
        {
            if (!owner || !canvas) return false;
            int index = -1;
            if (!imagePointToTextIndex(canvas, pos, true, index)) return false;

            // OcrResultWindow's normal left-button handler runs before the Canvas handler
            // and starts image panning.  Once a text hit is confirmed, switch that gesture
            // from panning to text selection.
            owner->imageDragging = false;
            anchor = focus = index;
            source = Source::Image;
            if (GetCapture() != owner->hwnd) SetCapture(owner->hwnd);
            syncImageSelectionToText(true);
            return true;
        }

        void updateImageSelection(Ling::StarCapOcrLinkedCanvas* canvas, POINT pos) override
        {
            if (!owner || source != Source::Image || !canvas) return;
            int index = -1;
            if (!imagePointToTextIndex(canvas, pos, false, index)) return;
            if (index == focus) return;
            focus = index;
            syncImageSelectionToText(false);
        }

        void endImageSelection(Ling::StarCapOcrLinkedCanvas*, POINT) override
        {
            if (!owner || source != Source::Image) return;
            if (GetCapture() == owner->hwnd) ReleaseCapture();
            syncImageSelectionToText(true);
        }

        bool imageHitText(Ling::StarCapOcrLinkedCanvas* canvas, POINT pos) override
        {
            if (!owner || !canvas) return false;
            int index = -1;
            return imagePointToTextIndex(canvas, pos, true, index);
        }

        bool handleImageKey(UINT key) override
        {
            if (!owner || source != Source::Image) return false;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl && key == 'A') {
                const auto text = displayText();
                if (text.empty()) return false;
                anchor = 0;
                focus = (int)text.size();
                syncImageSelectionToText(true);
                return true;
            }
            if (ctrl && key == 'C') {
                auto text = selectedText();
                if (text.empty()) return false;
                if (StarCapOcrV2::copyTextReliable(owner->hwnd, text)) {
                    if (owner->status) owner->status->setText(L"已复制图片中选中的文字");
                }
                else if (owner->status) {
                    owner->status->setText(L"复制失败，请重试");
                }
                return true;
            }
            if (key == VK_ESCAPE) {
                clearSelection();
                return true;
            }
            return false;
        }

        void paintImageSelection(Ling::StarCapOcrLinkedCanvas* canvas, ID2D1DeviceContext* ctx) override
        {
            if (!owner || !canvas || !ctx || !hasSelection()) return;
            const auto segments = buildSegments();
            if (segments.empty()) return;
            const auto imageRect = getImageRect(canvas);
            if (imageRect.right <= imageRect.left || imageRect.bottom <= imageRect.top) return;

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill, edge;
            ctx->CreateSolidColorBrush(D2D1::ColorF(0.086f, 0.467f, 1.f, 0.25f), fill.GetAddressOf());
            ctx->CreateSolidColorBrush(D2D1::ColorF(0.086f, 0.467f, 1.f, 0.76f), edge.GetAddressOf());
            if (!fill) return;

            const auto [selStart, selEnd] = orderedSelection();
            for (const auto& segment : segments) {
                const int a = std::max(selStart, segment.start);
                const int b = std::min(selEnd, segment.end);
                if (a >= b) continue;
                paintSegmentRange(ctx, imageRect, segment, a, b, fill.Get(), edge.Get());
            }
        }

        void textSelectionChanged(Ling::StarCapOcrLinkedTextBox* box, int start, int end, bool) override
        {
            if (!owner || !box || box != owner->textBox) return;
            const int n = (int)displayText().size();
            start = std::clamp(start, 0, n);
            end = std::clamp(end, 0, n);
            if (end < start) std::swap(start, end);

            // Clicking the image blurs the TextBox after the image Canvas already started
            // a selection.  Ignore that zero-length blur notification so it cannot erase
            // the newly-started image selection.
            if (start == end && source == Source::Image) return;

            box->clearLinkedSelection();
            if (start == end) {
                source = Source::None;
                anchor = focus = start;
            }
            else {
                source = Source::Text;
                anchor = start;
                focus = end;
            }
            owner->refresh();
        }

    private:
        enum class Source { None, Image, Text };

        struct Segment
        {
            int start{ 0 }, end{ 0 };
            int ymin{ 0 }, xmin{ 0 }, ymax{ 0 }, xmax{ 0 };
            std::wstring text;
        };

        struct LineSpan
        {
            int begin{ 0 }, end{ 0 };
        };

        static std::wstring lfText(std::wstring value)
        {
            std::wstring out;
            out.reserve(value.size());
            for (size_t i = 0; i < value.size(); ++i) {
                if (value[i] == L'\r') {
                    if (i + 1 < value.size() && value[i + 1] == L'\n') continue;
                    out.push_back(L'\n');
                }
                else out.push_back(value[i]);
            }
            return out;
        }

        static std::wstring crlfText(const std::wstring& value)
        {
            std::wstring out;
            out.reserve(value.size() + 8);
            for (size_t i = 0; i < value.size(); ++i) {
                if (value[i] == L'\r') {
                    out.push_back(L'\r');
                    if (i + 1 >= value.size() || value[i + 1] != L'\n') out.push_back(L'\n');
                }
                else if (value[i] == L'\n') {
                    out.push_back(L'\r');
                    out.push_back(L'\n');
                }
                else out.push_back(value[i]);
            }
            return out;
        }

        static std::wstring trimCopy(const std::wstring& value)
        {
            size_t first = 0, last = value.size();
            while (first < last && iswspace(value[first])) ++first;
            while (last > first && iswspace(value[last - 1])) --last;
            return value.substr(first, last - first);
        }

        bool locateBlockText(const std::wstring& display, const std::wstring& raw,
            size_t cursor, size_t& at, std::wstring& matched) const
        {
            if (raw.empty()) return false;
            std::vector<std::wstring> candidates;
            candidates.push_back(raw);
            auto lf = lfText(raw);
            if (lf != raw) candidates.push_back(lf);
            auto crlf = crlfText(lf);
            if (crlf != raw && crlf != lf) candidates.push_back(crlf);

            const size_t searchFrom = std::min(cursor, display.size());
            for (const auto& candidate : candidates) {
                auto pos = display.find(candidate, searchFrom);
                if (pos != std::wstring::npos) {
                    at = pos;
                    matched = candidate;
                    return true;
                }
            }
            for (const auto& candidate : candidates) {
                auto trimmed = trimCopy(candidate);
                if (trimmed.empty()) continue;
                auto pos = display.find(trimmed, searchFrom);
                if (pos != std::wstring::npos) {
                    at = pos;
                    matched = std::move(trimmed);
                    return true;
                }
            }
            return false;
        }

        std::wstring displayText() const
        {
            if (!owner || !owner->textBox) return {};
            return owner->textBox->getText();
        }

        std::vector<Segment> buildSegments() const
        {
            std::vector<Segment> out;
            if (!owner || !owner->textBox) return out;
            const auto display = displayText();
            if (display.empty()) return out;
            size_t cursor = 0;

            auto add = [&](int ymin, int xmin, int ymax, int xmax, const std::wstring& raw) {
                size_t at = 0;
                std::wstring matched;
                if (!locateBlockText(display, raw, cursor, at, matched)) return;
                Segment segment;
                segment.start = (int)at;
                segment.end = (int)std::min(display.size(), at + matched.size());
                segment.ymin = std::clamp(ymin, 0, 1000);
                segment.xmin = std::clamp(xmin, 0, 1000);
                segment.ymax = std::clamp(ymax, 0, 1000);
                segment.xmax = std::clamp(xmax, 0, 1000);
                if (segment.end <= segment.start || segment.ymax <= segment.ymin || segment.xmax <= segment.xmin)
                    return;
                segment.text = display.substr(segment.start, segment.end - segment.start);
                cursor = segment.end;
                out.push_back(std::move(segment));
            };

            if (owner->translationReady && owner->showingTranslationText) {
                for (const auto& b : owner->translationBlocks)
                    add(b.ymin, b.xmin, b.ymax, b.xmax, b.translation);
            }
            else if (!owner->geminiOcrBlocks.empty()) {
                for (const auto& b : owner->geminiOcrBlocks)
                    add(b.ymin, b.xmin, b.ymax, b.xmax, b.source);
            }
            else if (owner->translationReady) {
                for (const auto& b : owner->translationBlocks)
                    add(b.ymin, b.xmin, b.ymax, b.xmax, b.source);
            }
            return out;
        }

        static std::vector<LineSpan> lineSpans(const std::wstring& text)
        {
            std::vector<LineSpan> lines;
            int start = 0;
            for (int i = 0; i < (int)text.size(); ++i) {
                if (text[i] != L'\n') continue;
                int end = i;
                if (end > start && text[end - 1] == L'\r') --end;
                lines.push_back({ start, end });
                start = i + 1;
            }
            int end = (int)text.size();
            if (end > start && text[end - 1] == L'\r') --end;
            lines.push_back({ start, end });
            if (lines.empty()) lines.push_back({ 0, (int)text.size() });
            return lines;
        }

        static float glyphWeight(wchar_t ch)
        {
            if (ch == L'\r' || ch == L'\n') return 0.f;
            if (ch == L'\t') return 1.2f;
            if (iswspace(ch)) return .34f;
            if (ch >= 0x2E80) return 1.f;
            if (iswalnum(ch)) return .58f;
            return .42f;
        }

        static float rangeWeight(const std::wstring& text, int begin, int end)
        {
            begin = std::clamp(begin, 0, (int)text.size());
            end = std::clamp(end, begin, (int)text.size());
            float value = 0.f;
            for (int i = begin; i < end; ++i) value += glyphWeight(text[i]);
            return value;
        }

        static int boundaryAtRatio(const std::wstring& text, int begin, int end, float ratio)
        {
            begin = std::clamp(begin, 0, (int)text.size());
            end = std::clamp(end, begin, (int)text.size());
            ratio = std::clamp(ratio, 0.f, 1.f);
            const float total = std::max(.001f, rangeWeight(text, begin, end));
            const float target = total * ratio;
            float used = 0.f;
            for (int i = begin; i < end; ++i) {
                const float next = used + glyphWeight(text[i]);
                if (target <= (used + next) * .5f) return i;
                used = next;
            }
            return end;
        }

        D2D1_RECT_F getImageRect(Ling::StarCapOcrLinkedCanvas* canvas) const
        {
            if (!owner || !canvas || owner->imageW <= 0 || owner->imageH <= 0) return {};
            const float scale = owner->getImageScale();
            const float dw = owner->imageW * scale;
            const float dh = owner->imageH * scale;
            const float left = (canvas->w - dw) * .5f + owner->imageOffsetX;
            const float top = (canvas->h - dh) * .5f + owner->imageOffsetY;
            return D2D1::RectF(left, top, left + dw, top + dh);
        }

        bool imagePointToNormalized(Ling::StarCapOcrLinkedCanvas* canvas, POINT pos,
            float& nx, float& ny) const
        {
            if (!owner || !canvas) return false;
            const auto rect = getImageRect(canvas);
            const float dw = rect.right - rect.left, dh = rect.bottom - rect.top;
            if (dw <= .5f || dh <= .5f) return false;
            const float localX = pos.x - canvas->x;
            const float localY = pos.y - canvas->y;
            nx = (localX - rect.left) * 1000.f / dw;
            ny = (localY - rect.top) * 1000.f / dh;
            return true;
        }

        static float rectDistanceSq(const Segment& s, float x, float y)
        {
            float dx = 0.f, dy = 0.f;
            if (x < s.xmin) dx = s.xmin - x;
            else if (x > s.xmax) dx = x - s.xmax;
            if (y < s.ymin) dy = s.ymin - y;
            else if (y > s.ymax) dy = y - s.ymax;
            return dx * dx + dy * dy;
        }

        int indexInsideSegment(const Segment& s, float nx, float ny) const
        {
            const auto lines = lineSpans(s.text);
            const float relY = std::clamp((ny - s.ymin) / std::max(1.f, (float)(s.ymax - s.ymin)), 0.f, .999999f);
            const int lineIndex = std::clamp((int)std::floor(relY * lines.size()), 0, (int)lines.size() - 1);
            const auto line = lines[lineIndex];
            const float relX = std::clamp((nx - s.xmin) / std::max(1.f, (float)(s.xmax - s.xmin)), 0.f, 1.f);
            const int local = boundaryAtRatio(s.text, line.begin, line.end, relX);
            return std::clamp(s.start + local, s.start, s.end);
        }

        bool imagePointToTextIndex(Ling::StarCapOcrLinkedCanvas* canvas, POINT pos,
            bool requireInside, int& index) const
        {
            const auto segments = buildSegments();
            if (segments.empty()) return false;
            float nx = 0.f, ny = 0.f;
            if (!imagePointToNormalized(canvas, pos, nx, ny)) return false;

            int best = -1;
            float bestScore = FLT_MAX;
            for (int i = 0; i < (int)segments.size(); ++i) {
                const auto& s = segments[i];
                const bool inside = nx >= s.xmin - 3.f && nx <= s.xmax + 3.f &&
                    ny >= s.ymin - 3.f && ny <= s.ymax + 3.f;
                if (requireInside && !inside) continue;
                const float score = inside ? 0.f : rectDistanceSq(s, nx, ny);
                const float areaBias = (s.xmax - s.xmin) * (s.ymax - s.ymin) * 1e-7f;
                if (score + areaBias < bestScore) {
                    bestScore = score + areaBias;
                    best = i;
                }
            }
            if (best < 0) return false;
            index = indexInsideSegment(segments[best], nx, ny);
            return true;
        }

        static D2D1_RECT_F segmentRect(const D2D1_RECT_F& imageRect, const Segment& s)
        {
            const float dw = imageRect.right - imageRect.left;
            const float dh = imageRect.bottom - imageRect.top;
            return D2D1::RectF(
                imageRect.left + dw * s.xmin / 1000.f,
                imageRect.top + dh * s.ymin / 1000.f,
                imageRect.left + dw * s.xmax / 1000.f,
                imageRect.top + dh * s.ymax / 1000.f);
        }

        void paintSegmentRange(ID2D1DeviceContext* ctx, const D2D1_RECT_F& imageRect,
            const Segment& s, int globalStart, int globalEnd,
            ID2D1SolidColorBrush* fill, ID2D1SolidColorBrush* edge) const
        {
            if (!ctx || !fill) return;
            const int localStart = std::clamp(globalStart - s.start, 0, (int)s.text.size());
            const int localEnd = std::clamp(globalEnd - s.start, localStart, (int)s.text.size());
            if (localStart >= localEnd) return;
            const auto base = segmentRect(imageRect, s);
            const float bw = base.right - base.left, bh = base.bottom - base.top;
            if (bw <= .25f || bh <= .25f) return;

            const auto lines = lineSpans(s.text);
            for (int li = 0; li < (int)lines.size(); ++li) {
                const auto line = lines[li];
                const int a = std::max(localStart, line.begin);
                const int b = std::min(localEnd, line.end);
                if (a >= b) continue;
                const float total = std::max(.001f, rangeWeight(s.text, line.begin, line.end));
                const float wa = rangeWeight(s.text, line.begin, a) / total;
                const float wb = rangeWeight(s.text, line.begin, b) / total;
                const float y0 = base.top + bh * li / std::max(1, (int)lines.size());
                const float y1 = base.top + bh * (li + 1) / std::max(1, (int)lines.size());
                auto rect = D2D1::RectF(base.left + bw * wa, y0, base.left + bw * wb, y1);
                ctx->FillRectangle(rect, fill);
                if (edge && rect.right - rect.left > .5f && rect.bottom - rect.top > .5f)
                    ctx->DrawRectangle(rect, edge, std::max(.7f, owner->dpi));
            }
        }

        std::pair<int, int> orderedSelection() const
        {
            return { std::min(anchor, focus), std::max(anchor, focus) };
        }

        bool hasSelection() const
        {
            auto [a, b] = orderedSelection();
            return b > a;
        }

        std::wstring selectedText() const
        {
            const auto text = displayText();
            auto [a, b] = orderedSelection();
            a = std::clamp(a, 0, (int)text.size());
            b = std::clamp(b, a, (int)text.size());
            return b > a ? text.substr(a, b - a) : std::wstring{};
        }

        void syncImageSelectionToText(bool finished)
        {
            if (!owner || !owner->textBox) return;
            auto [a, b] = orderedSelection();
            const int n = (int)displayText().size();
            a = std::clamp(a, 0, n);
            b = std::clamp(b, a, n);
            owner->textBox->setLinkedSelection(a, b, b > a);
            if (owner->status && b > a && finished)
                owner->status->setText(std::format(L"已选择 {} 个字符；按 Ctrl+C 复制", b - a));
            owner->refresh();
        }

        void clearSelection()
        {
            source = Source::None;
            anchor = focus = 0;
            if (owner && owner->textBox) owner->textBox->clearLinkedSelection();
            if (owner) owner->refresh();
        }

    private:
        StarCapOcrV2::OcrResultWindow* owner{ nullptr };
        Source source{ Source::None };
        int anchor{ 0 }, focus{ 0 };
    };

    inline std::unique_ptr<OcrBridge> bridgeOwner;

    inline void detach(StarCapOcrV2::OcrResultWindow* window = nullptr)
    {
        if (window && activeBridgeWindow != window) return;
        activeBridge = nullptr;
        activeBridgeWindow = nullptr;
        bridgeOwner.reset();
    }

    inline void attach(StarCapOcrV2::OcrResultWindow* window)
    {
        detach();
        if (!window) return;
        bridgeOwner = std::make_unique<OcrBridge>(window);
        activeBridgeWindow = window;
        activeBridge = bridgeOwner.get();
        window->onDestroy.add([window]() {
            if (activeBridgeWindow == window) detach(window);
        });
    }
}
