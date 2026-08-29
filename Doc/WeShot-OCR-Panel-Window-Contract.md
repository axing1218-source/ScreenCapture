# WeShot OCR panel/window contract

This note pins the UI/window details that should be followed by the first WeShot OCR implementation patch.

## Selection invalidation ordering

`CutMask::startAdjust(pos)` is not a passive begin-drag call. For every edge/corner hit except `Inside`, it immediately calls `adjust(pos)` and can change `maskRect` on mouse-down.

Therefore `WinCap::onDown()` must snapshot the old committed rectangle **before** calling `cutMask->startAdjust(pos)`:

```cpp
else if (stage == CapStage::Adjust) {
    isPress = true;
    adjustStartRect = normalizedPixelRect(cutMask->maskRect);
    cutMask->startAdjust(pos);
    layoutTool(toolCap.get());
}
```

`onMove()` remains unchanged. `onUp()` compares the final normalized physical-pixel rectangle with the saved one and invalidates the capture session only when they differ.

Do not use the `CutMask` private `adjustStartRect` for WeShot state. It belongs to gesture mechanics; WeShot needs the rectangle representing the last committed OCR snapshot boundary.

## Pixel-rectangle normalization

All current selection mutations originate from integer `POINT` positions, and `getCutPixels()` already casts selection edges to `UINT32`. Use the exact same pixel semantics for stale detection so OCR invalidation and the captured image can never disagree.

Recommended helper model:

```cpp
struct CapturePixelRect {
    int left{};
    int top{};
    int right{};
    int bottom{};

    bool operator==(const CapturePixelRect&) const = default;
};

static CapturePixelRect toCapturePixelRect(const D2D1_RECT_F& r) {
    return {
        static_cast<int>(r.left),
        static_cast<int>(r.top),
        static_cast<int>(r.right),
        static_cast<int>(r.bottom)
    };
}
```

The same normalized rectangle should be stored in `CaptureSession::sourceRectPx`. This is safer than deciding session reuse from width/height alone: moving an unchanged-size selection still changes the source image.

## Session reuse predicate

`startOcr()` may reuse/focus an existing OCR result only when all of these are true:

1. session exists;
2. session is not stale;
3. `session.sourceRectPx == toCapturePixelRect(cutMask->maskRect)`;
4. OCR state is `Running` or `Ready`;
5. the panel belongs to the same capture lifetime.

If any condition fails, perform exactly one new `getCutPixels()` snapshot and allocate the next revision.

## Direct `--enter=ocr` path

`WinCap::enterByArg()` sets `stage = Adjust` and invokes `startOcr()` before `makeToolCap()` is called. Consequently:

- `startOcr()` and `WinOcrPanel` must not require `toolCap` to exist;
- panel placement must use `cutMask->maskRect`, `WinCap::x/y`, and monitor work-area data directly;
- panel close/focus behavior must remain correct when `toolCap == nullptr`;
- the first milestone should explicitly test `ScreenCapture.exe --enter=ocr` in addition to clicking the OCR toolbar button.

## OCR panel window style

The panel should be a separate top-level tool window associated with the current `WinCap` lifetime. It must not be a child overlay over the capture canvas because that would complicate `CutMask` hit testing.

Recommended behavior for milestone 1:

- `WS_POPUP` top-level window;
- `WS_EX_TOOLWINDOW | WS_EX_TOPMOST` extended styles;
- do **not** use `WS_EX_TRANSPARENT`;
- do **not** use `WS_EX_NOACTIVATE` for the OCR panel: users must be able to focus/select recognized text and use Ctrl+C;
- clicking/focusing the panel must never synthesize mouse input into `WinCap`;
- when panel focus returns to the capture window, the existing Adjust cursor/hit-test behavior resumes unchanged.

If an owner HWND is assigned, the ownership must be detached before `WinCap` destruction. Whether owner assignment is used or not, `WinCap` remains the C++ lifetime owner of `WinOcrPanel`.

## Panel placement

Do not reuse `layoutTool()` verbatim: a side panel is much larger than ToolCap and should not overlap the selected image unless there is no alternative.

Use this physical-screen placement order:

1. right side of selection within its monitor work area;
2. left side of selection;
3. clamp inside the monitor work area with minimum overlap as a fallback.

The panel chooses its own DPI from the monitor on which it is placed. Selection geometry remains physical pixels; convert only at the panel/window placement and rendering boundary.

On `WinCap` DPI/size change, reposition an open panel after the capture window has been restored to the virtual-desktop bounds. Do not mutate OCR block geometry.

## Panel states for milestone 1

The panel is a view of `CaptureSession`, never a second state owner:

- `Loading`: current revision OCR is running;
- `Ready`: recognized text is available;
- `Failed`: current revision failed and may be retried;
- `Stale`: selection changed since the displayed result;
- `Closed`: UI hidden/destroyed, session may still exist until capture closes or a new snapshot replaces it.

For `Stale`, disable Translate and do not display translated overlay. Re-running OCR replaces the session snapshot and returns the same panel instance to Loading.

## Close ordering

At the beginning of `WinCap::onClosed()`:

1. mark lifetime dead;
2. cancel/abandon active OCR and Gemini request contexts;
3. detach and close `WinOcrPanel`;
4. clear overlay/session UI references;
5. then execute the existing `capVideo`, `capLong`, `toolCap`, and deferred `winCap.reset()` cleanup.

This ordering is required because the existing implementation deliberately defers `winCap.reset()` through `dq.TryEnqueue()` to avoid freeing `WinCap` from inside one of its child callback stacks.

## First build acceptance cases

Before Gemini is added, the first testable build must pass these cases:

1. normal toolbar OCR: screenshot host remains open and one panel appears;
2. repeated OCR click while Running: no second OCR request;
3. repeated OCR click while Ready without selection change: focus/show same result, no second OCR request;
4. drag selection inside: panel becomes Stale only on mouse-up;
5. resize from every edge/corner, including the immediate mouse-down edge movement: panel becomes Stale reliably;
6. selection move with unchanged width/height: still becomes Stale because source rectangle changed;
7. close screenshot while OCR is running: no crash and late completion is ignored;
8. close/reopen panel while screenshot remains alive: capture interaction stays unchanged;
9. direct `--enter=ocr`: works without ToolCap having been created;
10. mixed-DPI monitor placement: panel remains reachable and OCR geometry remains aligned to physical pixels.

Gemini translation should be introduced only after these capture/panel/lifetime tests are stable.
