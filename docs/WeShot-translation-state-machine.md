# WeShot translation state machine

This refines the v0.8.3 implementation plan and is the contract for WeChat-like screenshot translation before the next UI/rendering changes.

## Capture-scoped states

One capture owns one state object. UI surfaces (toolbar and OCR side panel) only observe or request transitions; they do not own duplicate Gemini state.

States:

- `Original`: current capture pixels are authoritative and no translated overlay is visible.
- `OcrPending`: OCR request is in flight for the current capture revision.
- `OcrReady`: OCR text/boxes are cached for the current revision.
- `TranslationPending`: translation is in flight. Existing OCR data remains usable.
- `TranslationReady`: translated text/boxes and local rendered overlay are cached.
- `Error`: the last requested operation failed; original pixels and any previously valid cached result remain intact.

The visible mode (`showOriginal` / `showTranslation`) is separate from request state. This prevents a tab/button click from accidentally starting a new network request.

## Revision rules

`captureRevision` increments only when source pixels can change: new capture, selection resize/move that changes captured pixels, long-shot stitch update, or recapture.

It does **not** increment when opening/closing the OCR panel, selecting/copying text, toggling Original/Translation, changing panel width, or retrying the same request.

Every asynchronous request captures `(revision, model, targetLanguage, operation)` at start. A completion callback may publish results only when the tuple still matches the active capture. Otherwise it is silently discarded.

## Request reuse

For a given `(revision, model, targetLanguage)`:

- Toolbar `译` from a fresh capture may run one image translation request that returns translated blocks/boxes.
- If OCR is already `OcrReady`, translation should prefer `translateOcrBlocks()` so the image is not uploaded again.
- If an equivalent operation is already pending, toolbar and OCR panel attach to the same in-flight request instead of creating another request.
- Once `TranslationReady`, repeated Original/Translation toggles are local-only and must generate zero HTTP requests.
- Closing and reopening the OCR panel reuses cached OCR/translation data.

## Retry behavior

Retry is explicit. A timeout/API/parse error moves the requested operation to `Error` but never destroys source pixels or a previously valid OCR result.

Retry for the same revision reuses cached PNG/OCR data where possible. It must not increment `captureRevision`.

If the user changes the selection while a retry is pending, the old callback is discarded by revision validation.

## OCR panel input ownership

When pointer-down starts inside the OCR panel, the panel owns the complete mouse gesture until pointer-up. Capture selection hit-testing must not receive that gesture.

When the text control owns keyboard focus, Ctrl+C/Ctrl+A and text-selection shortcuts are handled by the panel. Capture shortcuts resume only after focus returns to the capture surface.

Original/Translation/Retry/Close buttons are UI-only actions and must never move or resize the screenshot selection.

## Translation rendering boundary

Gemini returns text and boxes only. It never generates the translated bitmap.

The local renderer owns background estimation, source-text masking, foreground color estimation, DirectWrite wrapping/shrinking and alignment. Rendering failures must not invalidate the translation data; the UI can fall back to the original image plus side-panel translated text.

## Gate order

1. Finish Gate A source parity in `GeminiClient.h`: low thinking, medium image resolution, 60 s inference receive ceiling, separate send/wait diagnostics, metadata GET connection test.
2. Remove CI source rewriting and replace it with assertions.
3. Introduce the capture-scoped state/revision object and request reuse above.
4. Enforce OCR panel input isolation.
5. Implement R2 local translated-image rendering.
6. Only then add long-screenshot tiling/deduplication.

## Acceptance tests

A build is ready to advance past the state/cache gate only when all of these pass:

- First `译` performs at most one equivalent Gemini request.
- After `TranslationReady`, 20 repeated Original/Translation toggles make zero additional requests.
- OCR-first → Translate does not upload the screenshot again when OCR blocks are valid.
- Start Translate → change selection → old result never paints on the new selection.
- Open/close/reopen OCR panel preserves cached text and translated state.
- Selecting/copying side-panel text cannot move the screenshot selection.
- Retry after timeout keeps original image and cached OCR available.
- Windows 10 22H2 at 100%, 125% and 150% scaling behaves identically.
