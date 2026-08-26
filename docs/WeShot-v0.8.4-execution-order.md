# WeShot v0.8.4 execution order

This note turns the current WeShot plan into a regression-friendly implementation order for the xland/ScreenCapture fork. The goal is WeChat-like screenshot interaction without coupling network, OCR-panel, and translated-image rendering changes into one patch.

## Current status

- Gate A is complete in source and now passes a clean Windows x64 CI build without Gemini production-code rewriting.
- The successful build produced a fresh `WeShot-Windows-x64` artifact and is the current regression baseline.
- Gate B has now been mapped to the actual code paths in `WeShotCaptureTranslate.h`, `WeShotOcrV2.h`, `ToolCap.cpp`, and `WinCap.cpp`.
- Do not start renderer tuning until Gate B and Gate C pass the interaction matrix.

## Gate A — Gemini source parity — COMPLETE

`Src/GeminiClient.h` now matches the tested v0.8.3 diagnostics behavior before further UI changes:

- Gemini 3.x screenshot OCR/translation uses `thinkingLevel=low`; keep the Gemini 2.5 compatibility path unchanged.
- Image-bearing requests use `MEDIA_RESOLUTION_MEDIUM`; text-only OCR-block translation does not.
- Real inference keeps short connect/send limits but allows up to 60 seconds while waiting for a Gemini response.
- Report send failure and wait-for-response failure separately, with elapsed milliseconds.
- Settings `Test connection` uses authenticated `GET /v1beta/models/{modelId}` rather than generating `OK`.
- Keep the existing automatic WinHTTP proxy policy in this gate so transport changes are not mixed with API changes.
- CI only asserts these invariants; it must not rewrite Gemini production code.

## Gate B — one capture revision, one request graph — NEXT

### Current-code audit

The current implementation has two independent async lifecycles:

- `WeShotCaptureTranslate.h` owns global `requestId`, `busy`, `ready`, cached selection geometry, copied pixels, and its translation overlay.
- `WeShotOcrV2.h` owns a different global `requestId`, its own copied pixels, Gemini OCR blocks, translation state, and result-window lifetime.
- Both modules contain their own `copyCutPixels()` implementation.
- Toolbar `译` calls `WeShotCaptureTranslate::toggle(win)`, while the OCR result window starts translation independently with `startGeminiTranslation()`.
- Selection changes are currently inferred from geometry only when toolbar Translate is clicked; there is no shared pixel revision that both modules can observe.

That means duplicate Gemini calls and stale-result races cannot be eliminated reliably by adding more request IDs inside either view. Gate B therefore introduces one shared capture-session layer before changing UI behavior.

### Shared session boundary

Add a small module such as `Src/WeShotCaptureSession.h` owned by the active `WinCap` capture lifecycle. It is a data/request coordinator only; it must not paint UI.

Minimum session data:

- `WinCap* owner`
- monotonic `uint64_t captureRevision`
- immutable snapshot for the current revision: pixels, width, height, screen origin / selection rectangle
- optional OCR cache: full text + `GeminiClient::OcrBlock[]`
- translation cache keyed by `(revision, targetLanguage)`
- in-flight map keyed by `(revision, operation, targetLanguage)`
- waiter list containing weak/view-safe callbacks, never owning a window

Operations should be explicit rather than view-specific, for example:

- `snapshotCurrentCapture(win)`
- `invalidatePixels(win)`
- `requestOcr(revision, ...)`
- `requestTranslation(revision, targetLanguage, ...)`
- `getCachedOcr(revision)` / `getCachedTranslation(revision, language)`

### Revision rules mapped to WinCap

Increment `captureRevision` only when capture pixels can change:

- after the initial selection is committed;
- after an Adjust drag finishes and the selected rectangle changed;
- when long-screenshot output replaces the normal capture pixels;
- any future crop/recapture operation that changes the bitmap.

Do not increment for:

- showing/hiding the translation overlay;
- opening/closing the OCR result window;
- selecting/copying text in the OCR panel;
- switching Original/Translation;
- moving the toolbar itself.

Prefer incrementing once on committed pixel change (`onUp`) rather than on every mouse-move while adjusting. While a drag is active, existing translated UI may be hidden, but the session should invalidate only when the final rectangle differs from the previous committed rectangle.

### Request graph

- OCR result, translation result and every in-flight request carry the revision they belong to.
- A completion whose revision is not the session's current revision updates nothing and notifies nobody.
- Starting a second request for the same `(revision, operation, language)` attaches another waiter to the existing in-flight request rather than creating a duplicate Gemini call.
- Request completion updates the session cache first, removes the in-flight entry second, then notifies surviving views.
- Views never own canonical network results; they render snapshots received from the session.
- If OCR blocks already exist for the revision, translation uses `translateOcrBlocks()` and does not upload the image again.
- If no OCR blocks exist, image translation may populate both translation and reusable OCR/block information when the API result supports it.

### Migration order

1. Extract the duplicated screenshot-to-BGRA readback into one helper used by the session.
2. Add session revision/snapshot and wire `WinCap` committed-selection changes to it without changing current UI.
3. Move toolbar translation request ownership out of `WeShotCaptureTranslate.h`; keep `TranslationOverlay` as a pure view.
4. Move OCR/Gemini request ownership out of `WeShotOcrV2.h`; keep `OcrResultWindow` as a pure result view.
5. Deduplicate same-revision requests and add diagnostics counters before changing visual behavior.

Acceptance: changing the selection while Gemini is working must never paint the old translation over the new capture, and toolbar/panel actions for the same revision must generate at most one matching Gemini request.

## Gate C — WeChat-like screenshot interaction and OCR side panel

Keep the screenshot surface as the primary interaction surface. Translation should not create a new modal window.

Toolbar behavior:

- First `译` click: state becomes Translating and sends at most one request for the current revision.
- Success: the same screenshot surface switches to translated rendering.
- Subsequent `原文` / `译文` switching is local and instant.
- Failure: return to Original and show a compact retry-capable status; do not freeze selection/edit controls.

OCR side-panel behavior:

- The panel is a sibling of the image surface, not a replacement for it.
- Mouse wheel, drag selection, Ctrl+A/C/X and text focus inside the panel must not move/resize the screenshot selection or trigger screenshot shortcuts.
- `原文` and `译文` tabs switch both the right-side text and the left image mode when a translation exists.
- Copy operations never mutate capture pixels or selection geometry.
- Opening/closing the panel does not invalidate a capture revision; only pixel changes do.
- Closing the OCR window removes its waiter/callback but must not cancel a shared request that the toolbar overlay still needs.

## Gate D — local translated-image rendering, WeChat style

Gemini returns text plus normalized text boxes only. WeShot performs all image compositing locally.

R2 renderer:

- Derive each text block rectangle from normalized coordinates.
- Sample background from a ring around the text block rather than averaging the text-covered interior.
- Fill/repair the old text region locally; do not ask Gemini to generate an image.
- Estimate text luminance/contrast from source pixels and choose a readable translated-text color.
- Fit translated text using measured layout: reduce font size until it fits, preserve multiline wrapping, and prefer original alignment when it can be inferred.
- Expand very tight boxes slightly for CJK glyphs, but clip to the image bounds.
- Use one renderer for both the transparent screenshot overlay and the OCR result-window preview so visual behavior cannot diverge.

R3+ can add stronger inpainting/gradient reconstruction only after R2 is stable and fast.

## Gate E — latency instrumentation

Expose one compact timing string in diagnostics builds:

`capture readback -> encode -> request build -> send -> wait -> parse -> render -> total`

Also expose per-session counters for `ocrRequests`, `imageTranslationRequests`, `textTranslationRequests`, and `deduplicatedWaiters`. These counters make request reuse testable without relying on visual guessing.

Do not optimize based on total time alone. The first target is avoiding client-created timeouts and duplicate requests; visual rendering should remain local and normally negligible compared with network inference.

## Test matrix after each gate

Use the same four screenshots each time: English chat, mixed Chinese/English, dense UI text, and text on non-flat background.

1. Settings -> Test connection.
2. Screenshot -> Translate; record first response time.
3. Switch Original/Translation repeatedly; no network request should occur.
4. Open OCR panel; verify text selection/copy and panel scrolling do not alter the capture.
5. Start Translate and immediately change capture pixels; stale result must be discarded.
6. Trigger Translate from toolbar and OCR panel for the same unchanged capture; verify only one matching request is sent.
7. Close the OCR window while a shared translation is in flight; toolbar result must still complete safely.
8. Adjust the selection, release it, then Translate; verify a new revision is used and old cached blocks are not reused.
9. Compare translated-image placement, font fit and background repair against the previous build.

Do not begin Gate D visual tuning until Gates A-C pass this matrix.