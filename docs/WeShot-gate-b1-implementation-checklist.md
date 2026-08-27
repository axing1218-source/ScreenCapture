# WeShot Gate B1 — implementation checklist

This checklist turns the B1 design into concrete source edits after re-reading the current `WinCap.cpp` event ordering. It intentionally does not change Gemini request payloads, OCR result caching, or translated-image rendering.

## Confirmed current event order

`WinCap::onUp()` in Select currently does:

1. `isPress = false`
2. `cutMask->hasRect()` guard
3. `enterByArg()` early return
4. Ctrl-to-pin early return
5. `stage = Adjust`
6. toolbar creation

Therefore the initial capture identity must be committed between steps 2 and 3. Doing it later would leave `--enter=ocr` outside the shared revision/snapshot model.

`WinCap::onDown()` in Adjust currently calls `cutMask->startAdjust(pos)` immediately. B1 should snapshot the starting rectangle immediately before that call, then compare it with the final rectangle in `onUp()` and publish at most one new revision.

## Minimal source edits for B1

### 1. Add data-only session header

Create `Src/WeShotCaptureSession.h` with:

- `CaptureSnapshot`
- revision number
- memoized immutable BGRA pixel buffer
- weak lifetime token

Do not put OCR text, translation blocks, overlay state, toolbar state, or Gemini request state in this header yet.

### 2. Add semantic WinCap API

Expose only:

```cpp
CaptureSnapshot captureSnapshot();
uint64_t captureRevision() const;
std::weak_ptr<void> captureLifetime() const;
```

Keep `getCutPixels(...)` private. `captureSnapshot()` is the only public way for live OCR/Translate to obtain canonical selection pixels.

### 3. Initial revision commit

In `WinCap::onUp()` / Select, after `hasRect()` succeeds and before `enterByArg()`:

```cpp
commitCapturePixelsChanged();
```

Do not increment again during the following Select -> Adjust transition.

### 4. Adjust revision commit

In `WinCap::onDown()` / Adjust, store the current committed `maskRect` before `startAdjust(pos)`.

In `WinCap::onUp()` / Adjust:

- set `isPress = false` as today;
- compare final `maskRect` with the stored starting rectangle;
- publish exactly one new revision if it actually changed;
- clear the stored-start flag.

Do not invalidate on every `onMove()`.

### 5. Snapshot creation contract

`captureSnapshot()` should memoize one readback per revision and return the same immutable buffer to both OCR and toolbar translation.

The snapshot must include:

- `revision`
- immutable BGRA pixels
- exact width/height from the readback
- `selection = cutMask->maskRect`
- `screenOrigin = { x + selection.left, y + selection.top }`

The current screenshot window spans the virtual desktop in physical pixels, so `screenOrigin` stays in physical virtual-desktop coordinates. No extra DIP conversion should be introduced in B1.

If readback fails, return an invalid snapshot without advancing revision. A transient readback failure must not create a fake new capture identity.

### 6. Async callback safety

Worker threads may retain immutable snapshot bytes, but they must not retain an unsafe UI lifetime assumption.

Before applying a Gemini/OCR result to the live capture UI, verify in this order:

1. lifetime token still resolves;
2. active capture revision still equals the request revision;
3. only then access/update live WinCap/overlay UI.

Closing the screenshot while Gemini is running must make the callback a no-op.

### 7. Migrate live Translate first

Replace the translation module's duplicate GPU readback with `win->captureSnapshot()`.

Keep its current request state and renderer unchanged for B1. Use `snapshot.screenOrigin` for overlay placement.

### 8. Migrate live OCR second

Route toolbar OCR and `--enter=ocr` through the same snapshot API. Keep standalone `showPixels()`/long-image flows working as separate non-live inputs until the long-shot session handoff is implemented.

## Interaction invariants

B1 must not change normal screenshot feel:

- opening/closing OCR does not change revision;
- Original/Translation toggles do not change revision;
- moving the toolbar does not change revision;
- selecting/copying OCR text does not change revision;
- dragging an Adjust handle hides stale translated presentation immediately, but revision changes only on mouse-up if the rectangle changed;
- a click that ends with the same rectangle keeps the same snapshot buffer.

## B1 test matrix

1. Normal toolbar OCR and Translate on one unchanged selection: same revision, dimensions, origin, and pixel-buffer identity.
2. `--enter=ocr`: first committed revision is non-zero and follows the same snapshot contract as toolbar OCR.
3. Adjust drag before mouse-up: no repeated revision increments.
4. Adjust mouse-up with changed rectangle: exactly one increment and a new pixel buffer.
5. Adjust click/no effective change: no increment.
6. Negative-origin secondary monitor: translated overlay remains aligned to the selection.
7. Close screenshot while Gemini is pending: late callback is dropped safely.
8. Repeated Original/Translation toggles: zero new screenshot readbacks.

## Exit criterion

Gate B1 is complete only when both live OCR and toolbar Translate obtain source pixels exclusively through the shared WinCap snapshot API and the tests above pass. Only then should B2 move OCR/translation result caches into the session and B3 deduplicate in-flight Gemini requests.