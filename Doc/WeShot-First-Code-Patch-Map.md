# WeShot first code patch map

This note maps the first implementation milestone onto the current xland/ScreenCapture source without changing the existing screenshot interaction model.

## Current source facts

- `WinCap` owns the selection (`CutMask`), toolbar (`ToolCap`), long-capture object and video object.
- `onDown()` starts selection adjustment with `cutMask->startAdjust(pos)`.
- `onMove()` performs the hot-path resize/move through `cutMask->adjust(pos)` and repositions the toolbar.
- `onUp()` currently only clears `isPress` in `CapStage::Adjust`.
- `enterByArg()` can call `startOcr()` immediately after selection is created and before `ToolCap` exists.
- `onClosed()` marks `isClosed`, disposes child workflows, closes `ToolCap`, then defers `winCap.reset()` through `Ling::App::get()->dq.TryEnqueue(...)`.

These facts make it possible to add OCR/session lifetime tracking without modifying `CutMask` or the mouse-move hot path.

## Patch 1: state-only plumbing

Add these private members to `WinCap` (names are illustrative but should stay close to this shape):

```cpp
struct CaptureSession;
struct CaptureLifetime;
class WinOcrPanel;

std::unique_ptr<CaptureSession> captureSession;
std::unique_ptr<WinOcrPanel> ocrPanel;
std::shared_ptr<CaptureLifetime> captureLifetime;
D2D1_RECT_F adjustStartRect{};
bool trackingAdjustRect{false};
uint64_t nextCaptureRevision{1};
```

Add helpers:

```cpp
D2D1_RECT_U currentSelectionRectPx() const;
bool selectionRectEquals(const D2D1_RECT_U& a, const D2D1_RECT_U& b) const;
void invalidateCaptureSessionForSelectionChange();
void cancelCaptureAsyncWork();
```

The canonical comparison type should be integer physical pixels (`D2D1_RECT_U`), converted from `cutMask->maskRect` at the boundary. Do not use width/height only: moving an unchanged-size rectangle must still invalidate OCR geometry.

## Patch 2: exact mouse hooks

### `WinCap::onDown()`

Inside the existing `stage == CapStage::Adjust` branch, immediately before `cutMask->startAdjust(pos)`:

```cpp
adjustStartRect = cutMask->maskRect;
trackingAdjustRect = true;
```

Do not touch session state in `onMove()`.

The existing double-click copy branch returns before the Adjust branch, so a double-click that copies the capture will not accidentally create a fake adjustment transaction.

### `WinCap::onUp()`

Inside the existing `stage == CapStage::Adjust` branch, after `isPress = false`:

1. If `trackingAdjustRect` is false, do nothing.
2. Convert both `adjustStartRect` and the final `cutMask->maskRect` to exact integer physical-pixel edges.
3. If any edge changed, call `invalidateCaptureSessionForSelectionChange()`.
4. Set `trackingAdjustRect = false` in all cases.

This preserves xland's current mouse feel because no OCR/session code runs while the pointer is moving.

## Patch 3: `startOcr()` compatibility boundary

`startOcr()` must not assume `toolCap != nullptr`. The command-line path `--enter=ocr` reaches it directly from `enterByArg()` before `makeToolCap()`.

Target behavior:

1. Require `CapStage::Adjust` and a non-empty selection.
2. If a non-stale session already matches the same physical rectangle and OCR is Running/Ready, only show/focus the existing panel.
3. Otherwise call `getCutPixels()` once.
4. Store width, height, BGRA pixels, source rectangle and a new revision in `CaptureSession`.
5. Keep the capture window alive; do not call `close()`.
6. Create/show `WinOcrPanel` independently of `ToolCap` and render Loading.
7. Launch one OCR job for that revision.

Keep the old external image-reader route behind a temporary fallback path until the native panel milestone is stable.

## Patch 4: close ordering

At the beginning of `WinCap::onClosed()`, immediately after the existing `isClosed` guard and before `capVideo/capLong/toolCap` disposal:

1. invalidate the capture lifetime token;
2. cancel OCR and Gemini request contexts;
3. close/detach the OCR panel;
4. clear overlay/session UI references;
5. continue the current child disposal and deferred `winCap.reset()` flow unchanged.

Worker completions must use the existing `Ling::App::get()->dq.TryEnqueue(...)` bridge, then verify lifetime + session revision before changing UI.

## First-build acceptance test

The first testable build should deliberately omit Gemini. It is ready only when all of these pass:

- normal drag-to-select interaction still matches the original tool;
- drag/move/8-direction resize still feels unchanged;
- clicking OCR leaves the screenshot visible;
- the OCR panel opens even when launched with `--enter=ocr` and no `ToolCap` exists;
- one OCR request fills selectable/copyable text in the panel;
- clicking OCR repeatedly for the same revision does not launch duplicate work;
- moving or resizing the selection marks the old OCR result Stale only on mouse-up;
- no image snapshot is copied during mouse-move;
- closing the capture while OCR is running does not crash and late completion becomes a no-op.

Gemini translation should be added only after this milestone passes, so network cancellation issues cannot be confused with screenshot-interaction regressions.
