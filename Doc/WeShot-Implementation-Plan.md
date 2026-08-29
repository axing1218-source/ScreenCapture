# WeShot implementation plan

## Goal

Preserve xland/ScreenCapture's existing WeChat-like selection interaction and add an OCR/translation workflow with minimal changes to `WinCap`.

## Confirmed integration point

`WinCap::startOcr()` already calls `getCutPixels()` and receives a stable, tightly packed, top-down BGRA buffer for the current selection. WeShot should branch immediately after `getCutPixels()` succeeds instead of reopening or recapturing the screen.

The existing `Select -> Adjust` interaction, `CutMask` hit-testing, drag/move/8-direction resize, toolbar placement, magnifier and other capture behavior should remain unchanged.

## CaptureSession

Introduce a session object owned by `WinCap` while the capture window is alive.

Suggested minimum model:

```cpp
struct OcrBlock {
    D2D1_RECT_F rectPx;      // coordinates in capture-image physical pixels
    std::wstring text;
    float confidence{0.f};
};

enum class AsyncState { Idle, Running, Ready, Failed, Cancelled };

struct CaptureSession {
    uint64_t revision{0};
    int width{0};
    int height{0};
    std::vector<BYTE> bgra;

    AsyncState ocrState{AsyncState::Idle};
    std::vector<OcrBlock> blocks;
    std::wstring plainText;

    AsyncState translationState{AsyncState::Idle};
    std::vector<std::wstring> translatedBlocks;
    std::wstring translatedPlainText;
};
```

`revision` is the stale-result guard. Any operation that creates a new selected-image snapshot increments it. Every OCR/Gemini request captures the current revision; a completion may update UI/state only if its revision still matches.

Do not keep pointers into temporary pixel buffers. `CaptureSession::bgra` owns the selected image for the whole OCR/translation lifetime.

## `startOcr()` target flow

1. Require `CapStage::Adjust` and a valid selection.
2. Call `getCutPixels()` exactly once.
3. Create/replace the session snapshot and increment `revision`.
4. Keep `WinCap` alive; do not call `close()`.
5. Open/show the OCR side panel in a Loading state.
6. Start one whole-image OCR job using the session BGRA data.
7. On matching-revision completion, store `blocks + plainText` once and refresh the side panel.
8. Translation uses those same OCR blocks; it must never launch a second OCR pass.

The old external `Util::openWithImageReader()` path can remain behind a compatibility/fallback flag until the new OCR path is proven stable.

## Side panel behavior

The OCR panel should be a separate top-level/tool window associated with `WinCap`, not a child hit-test layer over `CutMask`.

Requirements:

- panel scrolling/text selection only consumes mouse input inside the panel window;
- the screenshot host keeps all existing selection hit-testing behavior;
- closing the panel returns to the normal Adjust state without destroying the screenshot selection;
- while OCR is running, repeated OCR clicks should focus/show the existing panel rather than start duplicate work for the same revision;
- panel UI state is derived from `CaptureSession`, not kept as an independent source of truth.

Recommended first layout: original recognized text in one scrollable column with Copy and Translate actions. Translation can either replace a secondary column or appear beneath each source block after the basic panel is stable.

## Translation overlay

The overlay must consume the same `OcrBlock::rectPx` geometry produced by the single OCR pass.

- store geometry only in selected-image physical pixels;
- map to screenshot-window coordinates only at paint time using the current selection origin/scale;
- do not persist DPI-scaled rectangles;
- overlay should be non-interactive by default so it never steals selection drag/resize input;
- if source and translation block counts do not match, fall back to panel-only translated text rather than guessing geometry.

## DPI and multi-monitor rule

There is one geometry truth: physical pixels of the selected image. OCR boxes and translation boxes remain in that coordinate system. Window/DPI conversion happens only at the rendering boundary.

Regression coverage should include 100%, 125%, 150% and 200% scaling plus a mixed-DPI two-monitor arrangement.

## Gemini request lifecycle

Translation transport must use a per-request context with a single terminal completion path.

The deadline covers the complete lifecycle: send request -> receive headers -> read body. A 60-second deadline actively cancels/closes the request handle. Late callbacks after cancellation are ignored. Exactly one terminal state (`Ready`, `Failed`, or `Cancelled`) may be published for a request.

All callbacks also carry the originating `CaptureSession::revision`; late results for an older revision are discarded before touching UI.

## Incremental implementation order

1. Add `CaptureSession` model and ownership to `WinCap` without changing visible behavior.
2. Change `startOcr()` to snapshot the selected BGRA image into the session and keep `WinCap` open.
3. Add a minimal OCR panel shell showing Loading/Failed/Ready states.
4. Wire one whole-image OCR pass and populate `blocks/plainText`.
5. Add Copy and Translate actions.
6. Wire Gemini translation with full-request deadline/cancellation and revision guards.
7. Add non-interactive translated-block overlay.
8. Run DPI/multi-monitor and stale-result/cancellation regression tests.

## First testable milestone

The first useful build should stop before Gemini: click OCR -> screenshot remains open -> side panel appears -> one OCR pass fills recognized text -> selection drag/resize behavior still matches the original capture tool. This milestone isolates screenshot/UI regressions from Gemini networking issues.
