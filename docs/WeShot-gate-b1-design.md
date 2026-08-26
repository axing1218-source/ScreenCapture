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

## B1 migration sequence

1. Add `CaptureSnapshot` and a minimal `WeShotCaptureSession` with revision + memoized snapshot only.
2. Add a `captureSession` member to `WinCap` and initialize/destroy it with `WinCap`.
3. Route snapshot creation through `WinCap::getCutPixels(...)`; preserve the current BGRA/top-down/tight-row contract.
4. Replace `WeShotCaptureTranslate::copyCutPixels()` with `win->captureSnapshot()` while keeping its existing request/overlay state unchanged.
5. Replace the OCR path's duplicate readback the same way.
6. Wire committed selection changes to `invalidateCaptureSnapshot()` and increment the revision.
7. Add debug logging/assertions showing `revision`, width, height, and one snapshot allocation per unchanged revision.

Only after these steps compile and pass the baseline interaction test should B2 move OCR/translation result caches into the session.

## B1 acceptance tests

- Toolbar Translate and OCR opened on the same unchanged selection observe the same revision, dimensions, and pixel buffer identity.
- Repeated Original/Translation toggles do not create a new snapshot or revision.
- Opening/closing the OCR panel does not create a new snapshot or revision.
- Adjusting the selection without releasing the mouse does not increment revision repeatedly.
- Releasing an actually changed selection increments revision exactly once and the next snapshot has a different buffer.
- Clicking without changing the final rectangle does not invalidate the snapshot.
- Existing v0.8.3/0.8.4 Gemini behavior remains untouched in B1.

## Why this is safer

The current toolbar translation module has static `owner`, `requestId`, geometry cache, and its own GPU readback. Adding a second session singleton beside it would create three competing sources of truth. Making `WinCap` the sole capture-data owner gives Gate B2/B3 a stable foundation for shared OCR cache and in-flight request deduplication, while keeping screenshot interaction responsive and WeChat-like.