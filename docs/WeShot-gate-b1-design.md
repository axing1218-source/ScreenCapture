# WeShot Gate B1 — capture snapshot and revision design

This note refines Gate B1 after auditing the current `WinCap` and toolbar translation code. It deliberately stops before request deduplication or renderer changes.

## Key audit finding

`WinCap` already owns the capture lifecycle and has the only canonical notion of capture stage. Its pixel helper `getCutPixels(...)` is private, while the current translation module works around that boundary by calling `getCutImg()` and performing its own GPU-to-CPU readback. That duplicate readback also exists on the OCR path.

Therefore B1 should not introduce another global singleton that independently interrogates `WinCap`. The capture session should be owned by `WinCap` and refreshed from inside `WinCap`, where committed selection changes are already known.

A second audit of `WeShotCaptureTranslate.h` adds two important constraints:

1. The live translation overlay currently positions itself with `win->x + maskRect.left` / `win->y + maskRect.top`. `CaptureSnapshot::screenOrigin` must preserve that **virtual-desktop screen coordinate** contract. Storing only `maskRect.left/top` would be wrong on a non-zero or negative virtual-desktop origin and would shift overlays on multi-monitor layouts.
2. Detached Gemini workers currently capture a raw `WinCap*`. B1 should not expand that lifetime hazard. Snapshot bytes may outlive the window, but UI application must still be gated by a weak/session lifetime token plus revision before touching `WinCap` or an overlay.

## Ownership

Add `std::unique_ptr<WeShotCaptureSession> captureSession;` to `WinCap`.

Create it with the capture window and destroy/expire it with the window. No static `owner` pointer is required in the session layer.

`WeShotCaptureSession` is data-only. B1 only needs revision, memoized snapshot, and a lifetime token; request coordination belongs to B2/B3. It must not depend on `TranslationOverlay`, `OcrResultWindow`, or toolbar widgets.

## B1 source-boundary guardrails after `WinCap.h` audit

The current public API exposes `getCutImg()` while the canonical BGRA readback helper `getCutPixels(...)` is private. Keep that ownership direction rather than making the low-level readback public.

Use a small standalone header such as `WeShotCaptureSession.h` for `CaptureSnapshot` and the minimal session type. `WinCap.h` may include that data-only header, while translation/OCR code can consume `CaptureSnapshot` without including UI implementation headers. This avoids circular dependencies when B2 later adds cache/result structures.

For B1, the public `WinCap` surface should stay semantic and minimal:

```cpp
CaptureSnapshot captureSnapshot();
uint64_t captureRevision() const;
std::weak_ptr<void> captureLifetime() const;
```

`invalidateCaptureSnapshot()` should remain private unless a concrete external pixel-producing path needs it. Selection changes are already known inside `WinCap`; exposing invalidation publicly would let UI code accidentally mutate capture identity.

Do not move `CutMask`, toolbar geometry, OCR panel state, translated-view state, or Gemini request state into `CaptureSnapshot`. A snapshot identifies only the source pixels and their committed selection geometry. That keeps WeChat-like presentation toggles (`Original` / `Translation`, opening/closing OCR) from becoming false pixel revisions.

While an Adjust drag is active, hide any translated overlay immediately for visual correctness, but keep the previously committed snapshot alive until mouse-up. On mouse-up, publish exactly one new revision only if the committed rectangle changed. This gives the UI responsive WeChat-like behavior without producing a new full BGRA copy on every mouse move.

## Snapshot structure

Use an immutable value object:

```cpp
struct CaptureSnapshot {
    uint64_t revision{};
    std::shared_ptr<const std::vector<BYTE>> pixels;
    int width{};
    int height{};
    POINT screenOrigin{};       // absolute virtual-desktop screen coordinates
    D2D1_RECT_F selection{};    // WinCap-local committed selection rectangle
};
```

`shared_ptr<const vector<BYTE>>` lets toolbar translation and OCR share exactly the same bytes without a second full-image copy while an async Gemini request is running.

### Coordinate contract

Keep coordinate spaces explicit:

- `selection` is `CutMask::maskRect` in `WinCap` client coordinates.
- `screenOrigin.x = WinCap::x + selection.left`.
- `screenOrigin.y = WinCap::y + selection.top`.
- `width/height` are the exact pixel dimensions returned by `getCutPixels(...)`.
- Gemini block coordinates remain normalized to the captured image and must never be mixed with virtual-desktop coordinates.

This matters on multi-monitor desktops where the capture host can have a negative `x` or `y`. The current translation overlay already uses absolute screen coordinates; B1 must preserve that behavior exactly.

## WinCap API boundary

Do not make `getCutPixels(...)` public just for translation/OCR. Instead expose semantic operations:

```cpp
CaptureSnapshot captureSnapshot();
uint64_t captureRevision() const;
std::weak_ptr<void> captureLifetime() const;
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
- `onClosed()`: expire/reset the lifetime token before deferred destruction so late async callbacks can observe expiry and drop without dereferencing `WinCap`.

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
    out.screenOrigin = {
        (LONG)(x + cutMask->maskRect.left),
        (LONG)(y + cutMask->maskRect.top)
    };
    captureSession->snapshot = out;
    return out;
}
```

The exact storage type may change, but keep the behavioral contract: one readback per unchanged revision, immutable pixels for workers, explicit screen/client coordinate separation, and revision mutation only at committed source-image changes.

## Async lifetime contract

The pixel buffer is intentionally allowed to outlive the capture window, but UI objects are not.

A worker launch should capture only:

- immutable `CaptureSnapshot` data,
- API/model settings,
- the request's revision,
- a weak session/window lifetime token.

When the worker posts back to the UI thread, the callback must first verify:

1. the lifetime token is still valid;
2. the active capture session still has the same revision;
3. only then resolve/use the current `WinCap` and create/update UI.

Do not make B1 callbacks safer by merely checking `WinCap::get() == win` after already dereferencing `win`; that still leaves a stale raw pointer in the closure. B2/B3 can replace the current static `owner/requestId` mechanism once the shared session exists.

## Translation/OCR migration boundary

B1 should make the smallest possible call-site change:

- In `WeShotCaptureTranslate.h`, replace `copyCutPixels()` with `WinCap::captureSnapshot()`. Use `snapshot.screenOrigin` for overlay placement and pass `*snapshot.pixels`, width, and height into the existing Gemini path. Keep its current overlay/request state otherwise unchanged until B2/B3.
- In `WeShotOcrV2.h`, use the same snapshot API when the OCR result flow originates from the active screenshot. Do not yet move OCR results into the session.
- Preserve non-live entry points such as `showPixels()` for long screenshots/result-window reuse; B1 can later publish those pixels as a new canonical session snapshot rather than forcing them through `CutMask`.

This boundary is important: B1 centralizes source pixels and source identity only. It does not yet centralize OCR text, translation blocks, display state, or in-flight requests.

## WeChat-like interaction invariants during B1

B1 is an internal ownership change and must be invisible to normal screenshot interaction:

- Clicking OCR must not close, resize, or commit the selection.
- Clicking Translate may show a non-modal loading state, but the original selection remains usable.
- Opening/closing the OCR side panel is presentation-only and must not change revision.
- Original/Translation toggles are presentation-only and must never force a fresh snapshot.
- Text selection inside the OCR panel owns mouse capture for that gesture and must not leak move/up events into `WinCap` selection adjustment.
- If the user begins moving/resizing the screenshot while Gemini is still working, hide translated presentation immediately; when the request completes, its revision/lifetime gate decides whether it is still valid.

These invariants are the bridge between B1 data ownership and the later B2/B3 shared-request state machine.

## B1 migration sequence

1. Add `CaptureSnapshot` and a minimal `WeShotCaptureSession` with revision + memoized snapshot + lifetime token only.
2. Add a `captureSession` member to `WinCap` and initialize/expire it with `WinCap`.
3. Route snapshot creation through `WinCap::getCutPixels(...)`; preserve the current BGRA/top-down/tight-row contract.
4. Add the concrete `onDown/onUp` commit hooks above and verify one revision change per committed selection change.
5. Replace `WeShotCaptureTranslate::copyCutPixels()` with `win->captureSnapshot()`, use absolute `snapshot.screenOrigin`, and keep its existing request/overlay state otherwise unchanged.
6. Replace the OCR path's duplicate readback the same way.
7. Add debug logging/assertions showing `revision`, width, height, pixel-buffer address, screen origin, and snapshot cache hit/miss.
8. Add one multi-monitor regression case with the capture window at a negative virtual-desktop origin.

Only after these steps compile and pass the baseline interaction test should B2 move OCR/translation result caches into the session.

## B1 acceptance tests

- Toolbar Translate and OCR opened on the same unchanged selection observe the same revision, dimensions, screen origin, and pixel buffer identity.
- Repeated Original/Translation toggles do not create a new snapshot or revision.
- Opening/closing the OCR panel does not create a new snapshot or revision.
- Adjusting the selection without releasing the mouse does not increment revision repeatedly.
- Releasing an actually changed selection increments revision exactly once and the next snapshot has a different buffer.
- Clicking without changing the final rectangle does not invalidate the snapshot.
- A capture on a monitor left/above the primary monitor positions the translation overlay at the correct absolute screen coordinates.
- Closing the capture/OCR UI while Gemini is working causes late completion to be dropped before any stale UI pointer is used.
- Existing v0.8.3/0.8.4 Gemini request behavior remains untouched in B1.

## Why this is safer

The current toolbar translation module has static `owner`, `requestId`, geometry cache, and its own GPU readback. Adding a second session singleton beside it would create three competing sources of truth. Making `WinCap` the sole capture-data owner gives Gate B2/B3 a stable foundation for shared OCR cache and in-flight request deduplication, while keeping screenshot interaction responsive and WeChat-like.

The extra coordinate/lifetime contracts above close two failure modes that would otherwise survive a simple single-monitor happy-path test: translated overlays shifted on negative-origin monitors, and late detached worker callbacks touching a destroyed capture window.