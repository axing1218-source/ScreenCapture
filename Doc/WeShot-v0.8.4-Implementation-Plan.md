# WeShot v0.8.4 Implementation Plan

This document defines the next implementation gates for the xland/ScreenCapture-based WeShot branch. The goal is WeChat-like screenshot interaction with a stable OCR side panel and low-latency Gemini translation without generating replacement images.

## Current audited state

Gate A is complete in source: the validated Gemini behavior is owned by `Src/GeminiClient.h`, and the Windows build now treats CI as validation rather than as a production-code patcher. The current baseline therefore already has the correct model path, low thinking for Gemini 3.x Flash, medium image resolution for screenshot requests, a realistic inference timeout, model-metadata connection testing, and send-vs-response diagnostics.

The next architectural problem is duplicated capture state. The floating screenshot toolbar and the OCR result window can still independently copy the same selection pixels and independently own OCR/translation request state. That makes stale async results, duplicate Gemini calls, and inconsistent original/translated switching harder to reason about.

`WinCap` is the correct owner for the first shared-session implementation because it already owns the live `CutMask`, the capture stage, and the only low-level `getCutPixels()` path for the current selection. Do not introduce a process-wide singleton for this state.

## Gate A - Gemini source consistency (COMPLETE)

Completed source behavior:

- Keep the selected model unchanged (`gemini-3.7-flash` by default).
- Use `thinkingLevel: low` for Gemini 3.x Flash screenshot OCR/translation.
- Keep the Gemini 2.5 zero-thinking compatibility path.
- Use `MEDIA_RESOLUTION_MEDIUM` for image requests.
- Keep connect/send timeouts short while allowing a real inference response to wait substantially longer than the old 25-second limit.
- Distinguish request-send failures from response-wait failures and include elapsed time.
- Test connectivity with `GET /v1beta/models/{model}` rather than generating `OK`.
- CI validates these rules instead of rewriting them.

Do not reopen Gate A unless a real regression is reproduced by diagnostics.

## Gate B - Shared capture session, revision, cache, and request reuse

Implement this in three independently testable steps. Do not combine it with visual rendering changes.

### B1 - One immutable snapshot per capture revision

Add a capture-scoped session owned by `WinCap`.

Suggested data types:

```cpp
struct CaptureSnapshot {
    uint64_t revision{0};
    int width{0};
    int height{0};
    std::shared_ptr<const std::vector<BYTE>> bgra;
};

struct CaptureSession {
    uint64_t revision{0};
    CaptureSnapshot snapshot;
    // OCR/translation fields are added in B2/B3.
};
```

Suggested `WinCap` responsibilities:

- `std::shared_ptr<CaptureSession> captureSession;`
- `CaptureSnapshot currentSnapshot();`
- `void invalidateCaptureSession();`

`currentSnapshot()` is the only place allowed to call `getCutPixels()` for OCR/translation. If the selection has not changed since the last snapshot, it returns the existing immutable snapshot instead of copying pixels again.

Revision rules:

- Increment revision only when the selected source pixels can change: selection move, resize, new capture, or replacement of the source image.
- Merely opening/closing the OCR window, switching Original/Translation, copying text, moving the result window, or repainting must not increment revision.
- Long screenshot output is a new source image and must enter the same abstraction with a new revision; do not bolt long-image translation onto a separate state model later.
- A snapshot is immutable once handed to an async worker. Never let a worker read directly from `CutMask`, `screenImg`, or mutable UI state.

Lifetime rules:

- `WinCap` owns the session while that capture is alive.
- OCR/result windows hold `shared_ptr`/`weak_ptr` access to session data, not raw pointers back into `WinCap` for async completion.
- Closing an OCR window must not destroy a valid translation cache that the toolbar can still reuse.
- Closing the capture invalidates the session; late async callbacks must detect expiry/revision mismatch and silently drop their result.

B1 acceptance test:

1. Open OCR and toolbar translation for one unchanged selection; both must report/use the same revision and source dimensions.
2. Reopen OCR without changing selection; no second pixel copy should be required.
3. Move or resize selection; revision changes exactly once and the old snapshot becomes stale.
4. Close OCR while a request is running; completion must not access freed UI memory.

### B2 - Shared OCR and translation cache

Extend `CaptureSession` with revision-tagged results:

- `ocrRevision`, OCR blocks, full source text.
- `translationRevision`, translated blocks, full translated text.
- explicit `original/translated` display state should remain UI-local, while the underlying result cache is session-owned.

Rules:

- Cache entries are valid only when their result revision equals the current capture revision.
- Original/Translated switching after a successful result is always local-only.
- If current-revision OCR blocks already exist, OCR-side translation should prefer `translateOcrBlocks()` instead of uploading the image again.
- The toolbar may still request image translation when no current OCR cache exists.

B2 acceptance test:

1. First OCR populates the shared current-revision cache.
2. OCR-side Translate reuses OCR blocks and does not re-upload screenshot pixels.
3. Opening another result surface for the same revision shows cached text immediately.
4. Changing selection invalidates both caches without destroying old objects still referenced by an in-flight worker.

### B3 - Deduplicate in-flight Gemini work

Add request identity inside `CaptureSession`, keyed at minimum by:

- capture revision,
- operation (`OCR`, `image translation`, `text-block translation`),
- target language where relevant.

Rules:

- If toolbar and OCR window ask for the same operation on the same revision while it is already running, attach another completion observer instead of sending another HTTP request.
- Async completion writes session cache first, then notifies still-live UI observers.
- A revision mismatch discards the result before either cache or UI is mutated.
- Do not store UI raw pointers in the session. Observers must be weak/lifetime-safe.

B3 acceptance test:

1. Trigger the same translation rapidly from two surfaces; only one Gemini request is logged.
2. Both surfaces update from the one result if still open.
3. Close either surface during the request; the other still completes safely.
4. Resize selection during the request; the stale completion updates neither cache nor UI.

## Gate C - OCR side panel interaction isolation

The OCR side panel must behave like a normal text/result pane without affecting the screenshot selection.

Requirements:

- Mouse drag/select, wheel, Ctrl+C, Ctrl+A, and text focus inside the panel are consumed by the panel.
- Clicking/copying text must not resize, move, confirm, cancel, or redraw the capture selection.
- `Esc` while the text editor owns focus first follows the text/result-window convention; screenshot cancellation should happen only when the capture layer owns the command.
- Original/Translation tabs update the text pane immediately from the shared cache.
- The left preview image and right text pane use one display mode so they cannot drift to different Original/Translated states.
- Network progress/error text belongs in a status area, not inside the selectable OCR result text.

Acceptance test:

1. Drag-select OCR text while the underlying capture selection remains pixel-identical.
2. Ctrl+A/Ctrl+C affects only the focused text pane.
3. Scroll a long OCR result without moving the capture selection.
4. Switch Original/Translation repeatedly after one completed request; no network activity occurs.

## Gate D - WeChat-like in-place translation rendering (R2)

Gemini returns text plus boxes only. WeShot performs all image editing locally.

For every translated block:

1. Convert normalized `box_2d` to image pixels.
2. Expand a narrow sampling ring outside the text box; exclude pixels likely belonging to adjacent text boxes.
3. Estimate background from robust ring statistics (median/cluster), not from the text-filled center of the box.
4. Fill/repair the source text area locally. Use a fast flat/gradient approximation first; reserve true inpainting for a later R3 pass.
5. Estimate source foreground luminance/color separately from background and choose a translated text color with adequate contrast.
6. Start from the original box height/line count, then shrink DirectWrite layout until translated text fits.
7. Preserve likely alignment: compact labels/buttons center; paragraph-like regions left-align; single lines remain vertically centered.
8. Clip translated drawing to the block plus a small safe margin so one block cannot paint over neighbors.
9. Cache the rendered translated bitmap for the current revision. Original/Translated switching should draw one of two local bitmaps and be instant.

R2 should optimize for readability, speed, and visual stability. R3 can later add edge-aware interpolation for photos, textured backgrounds, and gradients.

## Gate E - Performance instrumentation

Record local timings for the first OCR/translation request:

- snapshot/pixel copy (should be zero on cache hit),
- PNG encode,
- request JSON/base64 construction,
- WinHTTP send,
- wait for response headers/model work,
- response body read,
- JSON parse,
- translated bitmap render,
- total.

Expose only a compact total in normal UI. Keep detailed timings in diagnostics/logging so model latency is not confused with local image processing.

Also record cache diagnostics in debug logs:

- capture revision,
- snapshot cache hit/miss,
- OCR cache hit/miss,
- translation cache hit/miss,
- in-flight request reuse count,
- stale completion discard count.

## Release order

1. Gate A - COMPLETE.
2. Gate B1 - shared immutable snapshot/revision.
3. Gate B2 - shared OCR/translation cache.
4. Gate B3 - in-flight request deduplication.
5. Gate C - OCR side-panel interaction isolation/regression test.
6. Gate D - R2 WeChat-style local rendering.
7. Gate E - timing telemetry and tuning.
8. Long screenshot translation only after the normal screenshot path passes the same session/cache lifecycle tests.

Each gate should produce a small test build before moving to the next. Do not mix networking, session lifetime, and visual rendering changes in one unverified build.
