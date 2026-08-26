# WeShot Gate B1 — capture snapshot and revision design

This note refines Gate B1 after auditing the current `WinCap` and toolbar translation code. It deliberately stops before request deduplication or renderer changes.

## Key audit finding

`WinCap` already owns the capture lifecycle and has the only canonical notion of capture stage. Its pixel helper `getCutPixels(...)` is private, while the current translation module works around that boundary by calling `getCutImg()` and performing its own GPU-to-CPU readback. That duplicate readback also exists on the OCR path.

Therefore B1 should not introduce another global singleton that independently interrogates `WinCap`. The capture session should be owned by `WinCap` and refreshed from inside `WinCap`, where committed selection changes are already known.

## Ownership

Add `std::unique_ptr<WeShotCaptureSession> captureSession;` to `WinCap`.

Create it with the capture window and destroy it with the window. No static `owner` pointer is required in the session layer.

`WeShotCaptureSession` is data-only and thread-safe enough for request coordination later. It must not depend on `TranslationOverlay`, `OcrResultWindow`, or toolbar widgets.

## Snapshot structure

Use an immutable value object:

```cpp
struct CaptureSnapshot {
    uint64_t revision{};
    std::shared_ptr<const std::vector<BYTE>> pixels;
    int width{};
    int height{};
    POINT screenOrigin{};
    D2D1_RECT_F selection{};
};
```

`shared_ptr<const vector<BYTE>>` lets toolbar translation and OCR share exactly the same bytes without a second full-image copy while an async Gemini request is running.

## WinCap API boundary

Do not make `getCutPixels(...)` public just for translation/OCR. Instead expose semantic operations:

```cpp
CaptureSnapshot captureSnapshot();
uint64_t captureRevision() const;
void invalidateCaptureSnapshot();
```

Internally `captureSnapshot()` may call the existing private `getCutPixels(...)` once and memoize the result for the current revision.

This removes both duplicated `copyCutPixels()` implementations and keeps the D2D readback implementation inside `WinCap`, where capture pixel ownership already belongs.

## Revision commits

Revision changes must happen at committed pixel boundaries, not UI events in general.

- Initial Select -> Adjust commit: increment once after the final selection rectangle is established.
- Adjust drag: do not increment on every `onMove`; remember the starting rectangle and increment once in `onUp` only if the final rectangle differs.
- Entering/leaving OCR, toggling translation, moving the toolbar, selecting text, or hiding an overlay: no revision change.
- Long screenshot: when a completed long-image bitmap becomes the canonical captured image, publish a new snapshot/revision at that handoff.

While the user is actively adjusting the selection, translated UI can be hidden immediately, but cache invalidation should wait until the committed rectangle changes.

## Concrete WinCap hook map

The current `WinCap.cpp` event flow gives B1 clean commit points; no polling or per-frame invalidation is needed.

Add one private rectangle field such as `D2D1_RECT_F adjustStartRect{}` and one boolean `hasAdjustStartRect{false}`.

- `onDown()` / `CapStage::Adjust`: immediately before `cutMask->startAdjust(pos)`, save `cutMask->maskRect` to `adjustStartRect` and mark it valid. Do not change the revision here.
- `onMove()` / `CapStage::Adjust`: continue calling `cutMask->adjust(pos)` and moving the toolbar exactly as today. Do not invalidate the snapshot on every mouse move.
- `onUp()` / transition `Select -> Adjust`: once `cutMask->hasRect()` succeeds and before `makeToolCap()`, call one commit helper such as `commitCapturePixelsChanged()` so the first stable selection receives a revision.
- `onUp()` / `CapStage::Adjust`: compare the final `cutMask->maskRect` with `adjustStartRect`. Only if the rectangle actually changed, call `commitCapturePixelsChanged()` once. Then clear the saved-start flag.
- `onClosed()`: expire/reset the session before deferred destruction so late async callbacks can observe an expired session and drop safely.

Use an exact/epsilon-safe rectangle comparison appropriate to the existing integer-like mask coordinates; do not increment the revision for a click that ends at the same rectangle.

Suggested helper split:

```cpp
void WinCap::commitCapturePixelsChanged()
{
    if (!captureSession) captureSession = std::make_unique<WeShotCaptureSession>();
    captureSession->revision++;
    captureSession->snapshot = {};
}

CaptureSnapshot WinCap::captureSnapshot()
{
    if (!captureSession) captureSession = std::make_unique<WeShotCaptureSession>();
    if (captureSession->snapshot.pixels &&
        captureSession->snapshot.revision == captureSession->revision) {
        return captureSession->snapshot;
    }

    std::vector<BYTE> pixels;
    int width = 0, height = 0;
    if (!getCutPixels(pixels, width, height)) return {};

    CaptureSnapshot out;
    out.revision = captureSession->revision;
    out.width = width;
    out.height = height;
    out.pixels = std::make_shared<const std::vector<BYTE>>(std::move(pixels));
    out.selection = cutMask->maskRect;
    out.screenOrigin = { (LONG)cutMask->maskRect.left, (LONG)cutMask->maskRect.top };
    captureSession->snapshot = out;
    return out;
}
```

The exact storage type may change, but keep the behavioral contract: one readback per unchanged revision, immutable pixels for workers, and revision mutation only at committed source-image changes.

## Translation/OCR migration boundary

B1 should make the smallest possible call-site change:

- In `WeShotCaptureTranslate.h`, replace the local cut-image readback with `WinCap::captureSnapshot()` and pass `*snapshot.pixels`, width, and height into the existing Gemini path. Keep its current overlay/request state unchanged until B2/B3.
- In `WeShotOcrV2.h`, use the same snapshot API when the OCR result flow originates from the active screenshot. Do not yet move OCR results into the session.
- Preserve non-live entry points such as `showPixels()` for long screenshots/result-window reuse; B1 can later publish those pixels as a new canonical session snapshot rather than forcing them through `CutMask`.

This boundary is important: B1 centralizes source pixels only. It does not yet centralize OCR text, translation blocks, display state, or in-flight requests.

## B1 migration sequence

1. Add `CaptureSnapshot` and a minimal `WeShotCaptureSession` with revision + memoized snapshot only.
2. Add a `captureSession` member to `WinCap` and initialize/destroy it with `WinCap`.
3. Route snapshot creation through `WinCap::getCutPixels(...)`; preserve the current BGRA/top-down/tight-row contract.
4. Add the concrete `onDown/onUp` commit hooks above and verify one revision change per committed selection change.
5. Replace `WeShotCaptureTranslate::copyCutPixels()` with `win->captureSnapshot()` while keeping its existing request/overlay state unchanged.
6. Replace the OCR path's duplicate readback the same way.
7. Add debug logging/assertions showing `revision`, width, height, pixel-buffer address, and snapshot cache hit/miss.

Only after these steps compile and pass the baseline interaction test should B2 move OCR/translation result caches into the session.

## B1 acceptance tests

- Toolbar Translate and OCR opened on the same unchanged selection observe the same revision, dimensions, and pixel buffer identity.
- Repeated Original/Translation toggles do not create a new snapshot or revision.
- Opening/closing the OCR panel does not create a new snapshot or revision.
- Adjusting the selection without releasing the mouse does not increment revision repeatedly.
- Releasing an actually changed selection increments revision exactly once and the next snapshot has a different buffer.
- Clicking without changing the final rectangle does not invalidate the snapshot.
- Closing OCR while Gemini is working does not dereference a destroyed result window.
- Existing v0.8.3/0.8.4 Gemini behavior remains untouched in B1.

## Why this is safer

The current toolbar translation module has static `owner`, `requestId`, geometry cache, and its own GPU readback. Adding a second session singleton beside it would create three competing sources of truth. Making `WinCap` the sole capture-data owner gives Gate B2/B3 a stable foundation for shared OCR cache and in-flight request deduplication, while keeping screenshot interaction responsive and WeChat-like.