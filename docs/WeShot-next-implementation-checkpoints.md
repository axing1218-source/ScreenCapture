# WeShot next implementation checkpoints

This supplements `WeShot-implementation-plan.md` and turns the current v0.8.3 direction into small, reviewable changes.

## Current code audit

The branch now has a clear source/CI mismatch that must be removed before new UI work:

- `Src/GeminiClient.h` still defaults Flash models to `thinkingLevel=minimal`;
- production inference in source still uses a 25 s receive timeout;
- Settings connection testing still performs a real `generateContent` inference asking Gemini to reply `OK`;
- source reports send/receive failure as one generic WinHTTP error;
- the successful v0.8.3 binary gets `low`, longer inference timeout, metadata connection testing and stage diagnostics from the workflow patch rather than from production source.

This means the next code change must be source parity, not another rendering or proxy experiment. Preserve the network route that already reached Google; change one variable group at a time.

## Code ownership map

Keep each concern in one place so WeChat-like interaction does not become coupled to Gemini transport details.

- `Src/GeminiClient.h`: Gemini REST transport, PNG payload construction, structured OCR/translation parsing, model metadata connection test, request-stage timing. It must not own capture UI state.
- `Src/Setting.cpp` / `Src/Setting.h`: API key, model selection, connection-test presentation. Connection testing must call the same routing policy as inference.
- capture/toolbar code under `Src/Win`: owns active selection revision, translation toggle presentation, and invalidation when source pixels change. It should consume cached translation state rather than starting ad-hoc duplicate requests.
- OCR result-window code under `Src/Win`: owns side-panel hit testing, focus, text selection/copy, Original/Translation presentation and Retry. It must not own canonical OCR/translation data.
- a capture-scoped result object should become the single owner of original pixels, OCR blocks, translated blocks, rendered overlay, request state, revision and timings.

## Migration gates

### Gate A — Gemini source parity

Move the behavior already validated by the v0.8.3 diagnostics build into `Src/GeminiClient.h` without changing visible UI:

- default `gemini-3.7-flash` uses supported low thinking;
- ordinary screenshot vision uses medium resolution;
- Settings connection test uses `GET /v1beta/models/{model}` rather than inference;
- real inference has a longer receive ceiling than the connection test;
- send failure and wait-response failure are reported separately with elapsed time;
- Windows error 12002 is described as timeout, not automatically as proxy failure.

Implementation order inside Gate A:

1. change only model request configuration (`low` + medium vision resolution);
2. move the validated inference timeout and stage timing into `postGenerate`;
3. replace `testConnection` with the metadata GET path using the same WinHTTP routing policy;
4. compile and verify behavior locally/CI;
5. only then remove the workflow source-rewrite block and replace it with assertions that fail if source regresses.

Do not combine Gate A with proxy changes. The earlier `AUTOMATIC_PROXY` path has already produced a Google HTTP response, so routing changes need independent evidence.

**Exit condition:** local and CI builds contain identical Gemini production behavior and the workflow no longer rewrites production request code.

### Gate B — shared revision cache

Introduce one capture-scoped state object keyed by `(captureRevision, model, targetLanguage)`.

Required fields:

- canonical BGRA pixels and dimensions;
- monotonically increasing capture revision;
- OCR text/boxes;
- translation text/boxes;
- rendered translated overlay/bitmap;
- in-flight request descriptor;
- timing diagnostics.

Rules:

- toolbar Translate on a fresh selection starts one combined image OCR+translation request;
- OCR-first then Translate reuses OCR blocks and uses text-only translation when possible;
- equivalent simultaneous toolbar/panel requests attach to one in-flight operation;
- panel close/reopen and Original/Translation toggles generate zero requests;
- any source-pixel/selection change invalidates derived results and makes stale callbacks no-ops.

**Exit condition:** one request per revision for equivalent work, stale result cannot paint onto a newer selection, and reopening the OCR panel is instant.

### Gate C — OCR side-panel isolation

Treat the panel as a real input surface layered above capture hit testing.

- mouse down/move/up for text selection stays captured by the panel until the gesture ends;
- Ctrl+C copies panel text only while panel/text control owns focus;
- clicking Original, Translation, Retry or Close cannot move/resize the selection;
- opening/closing the panel does not increment capture revision;
- closing restores focus to capture without cancelling it;
- panel width is stable and text wraps inside the panel.

**Exit condition:** repeated text selection/copy and tab switching never changes the capture rectangle on Windows 10 22H2 at 100%, 125% and 150% display scaling.

### Gate D — WeChat-like local translation rendering R2

Keep Gemini image generation out of the pipeline. Use returned boxes and render locally.

For each text block:

1. expand the box by a small clamped margin;
2. sample narrow border strips rather than the glyph-filled interior;
3. reject strong gradients/high disagreement instead of painting an obviously wrong flat rectangle;
4. use median/trimmed-mean background estimation;
5. estimate foreground color from pixels that differ from the background;
6. measure DirectWrite layout and shrink/wrap until translated text fits;
7. preserve likely source alignment (left/center/right) instead of always centering.

Do not add inpainting until R2 is stable; gradient/textured blocks that fail confidence should retain source background and be marked for R3.

**Exit condition:** plain UI text and chat bubbles no longer show obvious rectangular color contamination from the source glyphs.

## Latency budget

For a normal desktop screenshot, record locally:

- PNG encode;
- request build;
- send;
- wait for first response;
- body read;
- JSON parse;
- local render;
- total.

Optimization priority is `waitResponseMs` first, then image payload size/encode. Do not trade correctness for an artificial short timeout. After TranslationReady, Original/Translation switching should be local and effectively immediate.

## Long screenshot checkpoint

Do not send the full-resolution stitched image blindly. Keep original pixels for save/copy, but create a network copy and tile vertically when needed. Tiles overlap enough to catch text crossing boundaries; returned boxes are remapped into global coordinates and duplicate lines are merged by overlap/text similarity.

This comes only after Gates A–D, so long-capture complexity cannot hide basic request/cache bugs.

## Test order for the next binary

1. Settings → Test connection: verify model name and elapsed milliseconds.
2. Screenshot → Translate: record first total and wait-response time.
3. Toggle Original/Translation repeatedly: confirm zero additional network requests.
4. Start Translate, then move/resize selection before response: old result must be discarded.
5. Open OCR panel, select text, Ctrl+C, switch tabs and close/reopen: selection must remain unchanged and cached results must remain.
6. Repeat on Windows 10 22H2 at 125% scaling.

Only after these pass should the next binary add R2 rendering changes.