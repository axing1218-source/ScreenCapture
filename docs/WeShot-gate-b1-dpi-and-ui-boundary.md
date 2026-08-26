# WeShot Gate B1 — DPI and UI-boundary refinement

This note extends the existing B1 snapshot/revision design with two implementation constraints that should be verified before code lands.

## 1. Treat snapshot geometry as physical-pixel data

`CaptureSnapshot::width/height`, Gemini normalized boxes, BGRA rows, and the translation overlay must all share one physical-pixel coordinate contract.

`screenOrigin` should therefore represent the physical virtual-desktop pixel at the top-left of the captured bitmap. Do not mix it with DPI-scaled DIPs from Ling/Direct2D layout code.

Before B1 implementation, verify whether `WinCap::x/y` and `CutMask::maskRect` are already physical pixels under the process DPI-awareness mode. If either is DIP/logical space, convert exactly once when constructing the snapshot rather than scattering conversions through OCR/translation code.

Acceptance case: capture the same text on two monitors with different scale factors (for example 100% and 150%). The translated overlay must remain aligned to the source glyphs after moving the capture between monitors.

## 2. Snapshot identity must not include presentation state

OCR side-panel visibility, text selection, Original/Translation toggles, toolbar movement, hover states, and translated overlay visibility must never change `captureRevision`.

Only canonical source-pixel changes create a new revision:

- first committed selection;
- a committed resize/move that changes the source rectangle;
- a completed long-screenshot image becoming the canonical source;
- any future operation that actually replaces captured pixels.

This keeps the interaction WeChat-like: opening OCR or toggling translation feels instantaneous and does not invalidate work already completed for the same image.

## 3. OCR panel must own its input gestures

When the OCR side panel begins a text-selection, scrollbar, copy, or button gesture, mouse down/move/up for that gesture must not reach `WinCap` selection-adjust logic.

Implement this as an explicit input boundary, not by checking coordinates late in `WinCap::onMove()`. The panel should consume the gesture from mouse-down until release. Keyboard focus inside OCR should likewise prevent screenshot hotkeys from interpreting normal editing shortcuts such as Ctrl+C, Ctrl+A, arrows, Home/End, or wheel scrolling.

Acceptance case: drag-select multiple lines of OCR text, scroll the OCR result, and press Ctrl+C while the screenshot selection remains completely unchanged.

## 4. Async result application order

For every Gemini/OCR completion, apply results only after all three checks pass:

1. capture/session lifetime is still valid;
2. completion revision equals current committed revision;
3. the UI target still exists and is in a state that accepts that result.

The third check matters even when revision is unchanged: a user may have closed the OCR panel or switched back to Original while a request is completing. Cache the result, but do not force the UI back into translated mode.

## 5. B1 implementation order refinement

Use this order to minimize regressions:

1. verify DPI-awareness/coordinate units in `WinCap` and document the physical-pixel contract;
2. add `CaptureSnapshot` + minimal session/revision storage;
3. memoize one BGRA readback per unchanged revision;
4. migrate toolbar Translate to the snapshot API;
5. migrate active-capture OCR to the same snapshot API;
6. add lifetime/revision/UI-state gates to async completion;
7. add OCR panel gesture isolation regression tests;
8. run single-monitor, negative-origin multi-monitor, and mixed-DPI monitor tests.

Only after those pass should Gate B2 add shared OCR/translation caches and in-flight request deduplication.
