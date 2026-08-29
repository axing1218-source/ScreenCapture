# WeShot architecture reconciliation

## Why this note exists

The upstream/public xland/ScreenCapture discussion sometimes refers to Qt-style `WinFull` event names. The current WeShot fork in this repository is not that source layout: the actual host is `WinCap : Ling::WinBase`, with `CutMask`, `ToolCap`, `Ling::Canvas`, and Ling mouse/key events. Implementation work must target the fork that is actually built here. Upstream interaction behavior is a UX reference only.

## Confirmed source-of-truth classes

- `Src/Win/WinCap.h/.cpp`: full virtual-desktop screenshot host and lifecycle owner.
- `Src/Win/CutMask.h/.cpp`: selection rectangle hit-testing/move/resize.
- `Src/Tool/ToolCap.h/.cpp`: post-selection action toolbar.
- `Ling::WinBase` events wired by `WinCap`: `onMouseDown`, `onMouseMove`, `onMouseUp`, `onKeyDown`, `onDestroy`.
- `WinCap::startOcr()` already obtains the selected image through `getCutPixels()` as tightly packed top-down BGRA.
- `--enter=ocr` already routes through `WinCap::enterByArg()` and therefore must remain independent of `ToolCap` creation.

This means the existing `CaptureSession` plan remains valid, but the concrete patch belongs in `WinCap`, not in an upstream `WinFull` class.

## First source patch boundary

The first functional patch should intentionally avoid Gemini and overlay painting. It should establish lifecycle correctness first.

### `WinCap` members

Add:

```cpp
std::unique_ptr<CaptureSession> captureSession;
std::unique_ptr<WinOcrPanel> ocrPanel;
D2D1_RECT_F adjustStartRect{};
bool adjustStartRectValid{false};
uint64_t nextCaptureRevision{1};
std::shared_ptr<CaptureLifetime> captureLifetime;
```

`CaptureLifetime` contains only atomic liveness/cancellation state. Workers must not retain `WinCap*`, HWND, D2D resources, `CutMask*`, or panel pointers.

### Selection invalidation

Use the Ling event flow already present in `WinCap`:

1. In `onDown()` when `stage == CapStage::Adjust`, snapshot `cutMask->maskRect` immediately before selection adjustment begins and set `adjustStartRectValid = true`.
2. Keep `onMove()` free of OCR/session work.
3. In `onUp()` after `CutMask` commits the adjustment, compare integerized physical-pixel edges with the saved rectangle.
4. Only when the rectangle truly changed, call `invalidateCaptureSessionForSelectionChange()`.
5. Clear `adjustStartRectValid` after the release path.

The invalidation helper cancels/abandons OCR and Gemini request contexts, marks panel data stale, and hides translated overlay state. It does not call `getCutPixels()` and does not allocate a new revision.

### OCR entry

Replace the current external-reader-only behavior of `startOcr()` behind a temporary fallback switch:

1. Require a valid Adjust-stage selection.
2. If the current session still matches the selection and OCR is Running/Ready, only show/focus the existing panel.
3. Otherwise call `getCutPixels()` once.
4. Move/copy the returned BGRA bytes into a new `CaptureSession` and assign `revision = nextCaptureRevision++`.
5. Keep `WinCap` open.
6. Show/create the OCR panel in Loading state.
7. Launch one OCR job using immutable session image data.
8. Marshal completion back through the existing Ling UI dispatcher and publish only when lifetime token + revision still match.

If the new path cannot initialize, the existing `Util::openWithImageReader()` behavior can remain as a temporary compatibility fallback during milestone S1.

## Panel window and input isolation

The OCR panel should be a separate top-level Ling tool window owned/associated with the capture workflow, not a child overlay inside the screenshot canvas.

Consequences:

- mouse scrolling/text selection in the panel never reaches `CutMask`;
- Ctrl+C while the panel owns focus copies selected OCR text instead of invoking `WinCap` capture-copy behavior;
- Enter while the panel owns focus must not close/finish the screenshot;
- Esc in the panel should close/dismiss the panel first, leaving the screenshot in Adjust state; Esc in `WinCap` continues to close the capture as today;
- closing `WinCap` always closes/detaches the panel.

Do not globally disable `WinCap` shortcuts: scope keyboard isolation to the panel's own window focus so existing capture behavior is unchanged when the screenshot host has focus.

## Source image rule

For OCR, use the raw selected screenshot obtained by `getCutPixels()` before future translated overlays are painted. Do not create OCR input by re-rendering the capture canvas. This guarantees annotations/translation UI cannot feed back into recognition.

The same BGRA snapshot is the source for both OCR text and all later Gemini translation work. Translation must never trigger a second screen recapture or OCR pass.

## Gemini boundary after S1

After the panel milestone is stable:

- translate the already-recognized blocks;
- give every translation request `{lifetime token, revision, requestId}`;
- use a 60-second deadline for the whole HTTP lifecycle, not only connection establishment;
- cancellation caused by selection change or window close publishes no late UI state;
- block-count mismatch falls back to panel-only translation instead of guessing overlay geometry.

## S1 acceptance test

A build is S1-testable only when all of these are true:

1. normal screenshot selection/move/8-way resize behaves exactly as before;
2. clicking OCR leaves the screenshot open;
3. one OCR panel appears and one OCR pass is started;
4. repeated OCR clicks for the same revision do not start duplicates;
5. moving/resizing the selection marks old OCR stale without doing OCR during drag;
6. Ctrl+C/Enter inside the focused panel do not finish the screenshot;
7. closing the screenshot while OCR is running cannot update or crash a destroyed window;
8. `--enter=ocr` works even when `ToolCap` was never created.

Gemini and translated overlay are deliberately excluded from S1 so screenshot interaction regressions can be isolated from networking and layout work.
