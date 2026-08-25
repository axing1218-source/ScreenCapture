# WeShot v0.9 implementation plan

## Product behavior

### Capture toolbar
- Keep the WeChat-like post-selection toolbar anchored to the selected region.
- `OCR` opens the independent OCR result window.
- `Translate` stays inside the capture UI and never opens the OCR window.
- First `Translate` click sends the selected screenshot to Gemini once, receives translated text plus normalized text boxes, and draws the translated overlay locally.
- Later clicks only toggle the local original/translated rendering. They must not call Gemini again.
- If the selection rectangle changes after a translation result exists, invalidate that cached translation before allowing another toggle.

### OCR result window
- With a saved Gemini API key, Gemini is the primary OCR engine. Windows OCR is only the no-key fallback.
- The right side remains selectable/copyable text, with `Original` and `Translation` modes.
- Switching Original/Translation must change both the right-side text and the left-side image together.
- Once Gemini OCR has returned source blocks, translation should be text-only using those blocks; do not upload the image a second time.

## Gemini latency profile

For `gemini-3.7-flash`:
- Use `generateContent`.
- Use `thinkingLevel: low` for OCR/translation latency-sensitive requests. Gemini 3.7 Flash does not support `minimal`.
- Use `mediaResolution: MEDIA_RESOLUTION_MEDIUM` for normal desktop screenshots as the speed/detail compromise.
- Keep a finite request timeout so a failed network request does not leave the screenshot UI waiting for a minute.
- Gemini returns text/boxes only. WeShot performs all image compositing locally; Gemini must never generate an edited image for this workflow.

## Source/build cleanup

The current latency-compatible test build applies `low` thinking and medium image resolution in CI. Move these settings into `Src/GeminiClient.h`, then remove the CI source patch so local builds and CI builds behave identically.

After migration, add a CI sanity check that rejects `thinkingLevel=minimal` when the configured model is `gemini-3.7-flash`.

## Next implementation order

1. Move Gemini 3.7 tuning from CI patch into source and remove the patch.
2. Add translation-cache invalidation when the selected rectangle changes.
3. Add elapsed-time status text for first OCR/translation request to make latency measurable in user testing.
4. If upload time remains dominant, downscale oversized screenshots before API upload while preserving normalized box coordinates.
5. Only after latency and interaction are stable, improve translated-overlay background cleanup/font fitting.

## Acceptance tests

- Capture -> Translate: no new window; translated overlay appears in-place.
- Translate -> Original -> Translate: subsequent switching is immediate and offline.
- Resize/move selection after translation: old translation is not reused.
- OCR with API key: Gemini OCR result appears in the right panel.
- OCR -> Translation -> Original -> Translation: left image and right text switch together.
- OCR translation after Gemini OCR: no second image upload.
- A normal screenshot should fail fast on network/API errors instead of hanging for about a minute.
