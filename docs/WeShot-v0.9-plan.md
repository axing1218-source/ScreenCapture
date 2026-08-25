# WeShot v0.9 implementation plan

## Product behavior

### Capture toolbar
- Keep the WeChat-like post-selection toolbar anchored to the selected region.
- `OCR` opens the independent OCR result window.
- `Translate` stays inside the capture UI and never opens the OCR window.
- First `Translate` click sends the selected screenshot to Gemini once, receives translated text plus normalized text boxes, and draws the translated overlay locally.
- Later clicks only toggle the local original/translated rendering. They must not call Gemini again.
- If the selection rectangle changes after a translation result exists, invalidate that cached translation before allowing another toggle.
- If the selection changes while a Gemini request is still in flight, validate the current rectangle again when the result is delivered and discard stale results instead of painting them over the new selection. Implemented in `Src/WeShotCaptureTranslate.h`.

### Capture interaction state
Use one explicit capture-session state instead of independent booleans spread across toolbar handlers:
- `selectionRevision`: increment whenever the capture rectangle or source pixels change.
- `translationRequestRevision`: snapshot `selectionRevision` at request start.
- `translationReadyRevision`: revision that produced the currently cached translation.
- `viewMode`: `Original` or `Translation`.
- `requestState`: `Idle`, `Translating`, `Ready`, or `Error`.

A Gemini result may be committed only when `translationRequestRevision == selectionRevision`. This keeps resize/move, long-screenshot refresh, and late async callbacks from reusing stale text boxes.

### OCR result window
- With a saved Gemini API key, Gemini is the primary OCR engine. Windows OCR is only the no-key fallback.
- The right side remains selectable/copyable text, with `Original` and `Translation` modes.
- Switching Original/Translation must change both the right-side text and the left-side image together.
- Once Gemini OCR has returned source blocks, translation should be text-only using those blocks; do not upload the image a second time.
- Keep OCR-window state separate from capture-window translation state. Closing the OCR window must not destroy or mutate the capture overlay cache.

### OCR side-panel interaction
- Mouse/keyboard events inside selectable OCR text belong to the text control first; they must not resize or move the screenshot selection underneath.
- `Ctrl+C`, text drag-selection, mouse wheel, and scrollbar input must remain local to the OCR panel while it has focus.
- Capture hotkeys continue to work when the OCR panel is not actively editing/selecting text.

## Gemini latency profile

For `gemini-3.7-flash`:
- Use `generateContent`.
- Use `thinkingLevel: low` for OCR/translation latency-sensitive requests. Gemini 3.7 Flash does not support `minimal`.
- Use `mediaResolution: MEDIA_RESOLUTION_MEDIUM` for normal desktop screenshots as the speed/detail compromise.
- Keep a finite request timeout so a failed network request does not leave the screenshot UI waiting for a minute.
- Gemini returns text/boxes only. WeShot performs all image compositing locally; Gemini must never generate an edited image for this workflow.

### Latency instrumentation
Measure the first request as separate phases instead of one total timer:
1. PNG encoding time.
2. Request construction/base64 time.
3. HTTP send + Gemini response time.
4. JSON/schema parsing time.
5. Local translated-overlay layout/paint time.

Expose the total elapsed time in the temporary status text and write phase timings to debug output. Optimize image resizing only if encode/upload dominates; do not reduce OCR detail pre-emptively.

## Source/build cleanup

The current latency-compatible test build still applies `low` thinking and medium image resolution in CI. `Src/GeminiClient.h` must be updated so local builds and CI builds behave identically, after which the CI source patch can be removed.

The build must then verify the checked-in source directly rather than mutating it. The sanity check should fail if `minimal` is present for 3.7 Flash or if the medium media-resolution setting is missing.

## Next implementation order

1. Move Gemini 3.7 tuning from CI patch into source and remove the patch.
2. Replace ad-hoc translation flags with the revision-based capture-session state above.
3. Add phase timing for first OCR/translation request and visible elapsed-time status.
4. Confirm OCR side-panel input cannot leak into screenshot resize/move handlers.
5. If upload time remains dominant, downscale oversized screenshots before API upload while preserving normalized box coordinates.
6. Only after latency and interaction are stable, improve translated-overlay background cleanup/font fitting.

## Acceptance tests

- Capture -> Translate: no new window; translated overlay appears in-place.
- Translate -> Original -> Translate: subsequent switching is immediate and offline.
- Resize/move selection after translation: old cached translation is not reused.
- Resize/move selection while translation is still running: stale asynchronous result is discarded.
- Long-screenshot source refresh: increments the same selection revision and invalidates old translation.
- OCR with API key: Gemini OCR result appears in the right panel.
- OCR -> Translation -> Original -> Translation: left image and right text switch together.
- OCR translation after Gemini OCR: no second image upload.
- Selecting/copying/scrolling OCR text does not move or resize the capture region.
- Closing the OCR window does not clear a valid capture-window translation cache.
- A normal screenshot should fail fast on network/API errors instead of hanging for about a minute.
- Debug timing separates encode/request/network/parse/paint so latency regressions can be localized.
