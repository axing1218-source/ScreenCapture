# WeShot Gate B1 — DPI and UI-boundary refinement

This note extends the existing B1 snapshot/revision design with the coordinate and input constraints now verified against the current WinCap/App implementation.

## 1. Snapshot geometry is physical-pixel data

The current capture path already uses a physical-pixel contract:

- `App::getScreenArea()` returns `SM_XVIRTUALSCREEN / SM_YVIRTUALSCREEN / SM_CXVIRTUALSCREEN / SM_CYVIRTUALSCREEN`.
- `App::takeScreenShot()` feeds those coordinates directly to `BitBlt` and creates the D2D bitmap at exactly that pixel width/height.
- `WinCap` stores that virtual-desktop origin in `x/y` and explicitly documents `cutMask->maskRect` as physical pixels.
- `WinCap::getCutImg()` copies the selected source rectangle directly from `screenImg`, so no DPI conversion belongs in the BGRA snapshot path.
- `WinCap::layoutTool()` converts the client-space mask rectangle to absolute desktop coordinates as `x + maskRect.left/top/...`; this is the coordinate convention `CaptureSnapshot::screenOrigin` should reuse.

Therefore B1 should not add a DIP conversion layer. Construct the immutable snapshot as:

- `width  = maskRect.right - maskRect.left`
- `height = maskRect.bottom - maskRect.top`
- `screenOrigin.x = WinCap::x + maskRect.left`
- `screenOrigin.y = WinCap::y + maskRect.top`

Store those geometry fields as integer physical pixels. Gemini's normalized boxes remain 0..1000 values and are converted exactly once against snapshot `width/height` when rendering.

DPI is presentation-only for this path. It can affect toolbar gaps, fonts, handles, and other UI chrome, but must not alter capture identity or snapshot pixel coordinates.

Acceptance case: capture the same text on two monitors with different scale factors (for example 100% and 150%), including a monitor positioned at a negative virtual-desktop X/Y origin. The translated overlay must remain aligned to the source glyphs.

## 2. Snapshot identity must not include presentation state

OCR side-panel visibility, text selection, Original/Translation toggles, toolbar movement, hover states, translated overlay visibility, and DPI-only relayout must never change `captureRevision`.

Only canonical source-pixel changes create a new revision:

- first committed selection;
- a committed resize/move that changes the source rectangle;
- a completed long-screenshot image becoming the canonical source;
- any future operation that actually replaces captured pixels.

The current `WinCap` event path supports a clean commit boundary: `CutMask::adjust()` runs continuously in `onMove()`, while `onUp()` marks the end of an adjustment gesture. B1 should capture the rectangle at mouse-down and compare it with the final rectangle at mouse-up; increment revision once only when the committed rectangle actually changed.

This keeps the interaction WeChat-like: opening OCR or toggling translation feels instantaneous and does not invalidate work already completed for the same image.

## 3. OCR panel must own its input gestures

When the OCR side panel begins a text-selection, scrollbar, copy, or button gesture, mouse down/move/up for that gesture must not reach `WinCap` selection-adjust logic.

Implement this as an explicit input boundary, not by checking coordinates late in `WinCap::onMove()`. The panel should consume the gesture from mouse-down until release. Keyboard focus inside OCR should likewise prevent screenshot hotkeys from interpreting normal editing shortcuts such as Ctrl+C, Ctrl+A, arrows, Home/End, or wheel scrolling.

This is important because `WinCap::onDown()` immediately calls `cutMask->startAdjust()` in Adjust stage and `onMove()` then mutates the selection whenever `isPress` is true. Letting the panel's initial mouse-down leak to WinCap would already arm an unwanted selection adjustment before later coordinate checks could stop it.

Acceptance case: drag-select multiple lines of OCR text, scroll the OCR result, and press Ctrl+C while the screenshot selection remains completely unchanged.

## 4. Async result application order

For every Gemini/OCR completion, apply results only after all three checks pass:

1. capture/session lifetime is still valid;
2. completion revision equals current committed revision;
3. the UI target still exists and is in a state that accepts that result.

The third check matters even when revision is unchanged: a user may have closed the OCR panel or switched back to Original while a request is completing. Cache the result, but do not force the UI back into translated mode.

The existing `WinCap::onClosed()` destroys the native window immediately but defers the C++ `winCap.reset()` to the next message-loop turn. That deferred destruction is useful for current callbacks but is not a safe async lifetime guarantee. Gemini completion must therefore use a session/lifetime token (for example `weak_ptr`/generation token), not a captured raw `WinCap*`.

## 5. B1 implementation order refinement

Use this order to minimize regressions:

1. add `CaptureSnapshot` + minimal session/revision storage using the now-confirmed physical-pixel contract;
2. record the committed mask rectangle at selection/adjust start and increment revision once at mouse-up only if it changed;
3. memoize one BGRA readback per unchanged revision;
4. migrate toolbar Translate to the snapshot API;
5. migrate active-capture OCR to the same snapshot API;
6. add lifetime/revision/UI-state gates to async completion;
7. add OCR panel gesture isolation regression tests;
8. run single-monitor, negative-origin multi-monitor, and mixed-DPI monitor tests.

Only after those pass should Gate B2 add shared OCR/translation caches and in-flight request deduplication.

## 6. B1 invariants to enforce in code review

- No OCR/translation code may call DPI conversion helpers for snapshot geometry.
- No OCR/translation code may reconstruct desktop origin independently from the shared snapshot.
- `captureRevision` changes at committed source-image boundaries only, never on hover, tool relayout, OCR-panel actions, or Original/Translation toggles.
- A revision has at most one immutable BGRA snapshot instance.
- Async callbacks never dereference a raw `WinCap*` after leaving the initiating UI callback.
