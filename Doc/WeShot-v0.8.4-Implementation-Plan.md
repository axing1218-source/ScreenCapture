# WeShot v0.8.4 Implementation Plan

This document defines the next implementation gates for the xland/ScreenCapture-based WeShot branch. The goal is WeChat-like screenshot interaction with a stable OCR side panel and low-latency Gemini translation without generating replacement images.

## Current audited state

`Src/GeminiClient.h` already supports screenshot PNG encoding, Gemini OCR, screenshot translation with normalized text boxes, text-only translation for existing OCR blocks, and structured JSON parsing. The screenshot/OCR UI can therefore stay local after the first model request.

The source still differs from the successful v0.8.3 diagnostic build in four important places:

1. Flash models still select `minimal` thinking in source instead of the validated `low` setting.
2. The source inference receive timeout is still 25 seconds.
3. The source connection test performs generation (`Reply with exactly OK`) instead of a lightweight model metadata request.
4. Medium vision resolution and send-vs-wait timing diagnostics are injected by CI rather than owned by source code.

These must be fixed before visual translation rendering is changed again.

## Gate A - Gemini source consistency

Move the proven diagnostic-build behavior into `GeminiClient.h` and make CI validation-only.

Required source behavior:

- Keep the selected model unchanged (`gemini-3.7-flash` by default).
- Use `thinkingLevel: low` for Gemini 3.x Flash screenshot OCR/translation.
- Keep the existing Gemini 2.5 zero-thinking compatibility path.
- Add `MEDIA_RESOLUTION_MEDIUM` only when the request contains an image.
- Keep connect/send timeouts short, but allow up to 60 seconds for a real inference response.
- Distinguish send failure from response-wait failure and include elapsed milliseconds.
- Implement `testConnection()` using `GET /v1beta/models/{model}` with the same WinHTTP access mode used by inference. The test must not perform model inference.
- Remove workflow code that rewrites these production settings after the source version passes CI.

Acceptance test:

1. Settings -> Test Connection returns model name and elapsed milliseconds.
2. Screenshot -> Translate does not fail merely because the model response takes more than 25 seconds.
3. A timeout message says whether sending failed or waiting for Gemini failed.

## Gate B - Shared capture revision and request reuse

Create one capture-scoped translation state shared by the floating screenshot toolbar and OCR result window.

Suggested state:

- `captureRevision` - increment whenever source pixels/selection change.
- `ocrRevision` and `translationRevision` - revision that produced the cached result.
- `ocrPending` / `translationPending` - prevent duplicate concurrent calls.
- cached original pixels, OCR blocks, full source text, translated blocks, full translated text.

Rules:

- Any selection resize/move that changes captured pixels increments `captureRevision` and invalidates stale OCR/translation results.
- Async results are applied only when their captured revision still equals the current revision.
- If OCR blocks already exist for the current revision, translating from the OCR side panel should call text-only `translateOcrBlocks()` instead of uploading the screenshot again.
- Toolbar and OCR window should reuse an in-flight request for the same revision rather than sending two Gemini requests.
- Original/translated switching after a successful result is local-only and must never issue another network request.

## Gate C - OCR side panel interaction isolation

The OCR side panel must behave like a normal text/result pane without affecting the screenshot selection.

Requirements:

- Mouse drag/select, wheel, Ctrl+C, Ctrl+A, and text focus inside the panel are consumed by the panel.
- Clicking/copying text must not resize, move, confirm, cancel, or redraw the capture selection.
- `Esc` while the text editor owns focus first clears text selection/focus according to the existing UI convention; screenshot cancellation should happen only when the capture layer owns the command.
- Original/Translation tabs update the text pane immediately from cache.
- The left preview image and right text pane use the same original/translated state.

## Gate D - WeChat-like in-place translation rendering (R2)

Gemini must return text plus boxes only. WeShot performs all visual editing locally.

For every translated block:

1. Convert normalized `box_2d` to image pixels.
2. Sample a narrow ring outside the text box rather than averaging pixels inside the text itself.
3. Estimate background using robust median/cluster sampling; avoid sampling neighboring text when possible.
4. Fill/repair the original text area locally. Keep a fallback flat fill for complex backgrounds.
5. Estimate source text brightness from the original region and select a contrasting translated text color.
6. Fit text using the original box height as the starting font size, then shrink until line wrapping fits the box.
7. Preserve likely alignment: short UI labels center; paragraph-like boxes left-align; single-line content remains vertically centered.
8. Draw with DirectWrite/Direct2D; do not ask Gemini to generate an edited image.

The first R2 version should prefer speed and readability over perfect inpainting. A later R3 pass can add edge-aware/background interpolation for photos and gradients.

## Gate E - Performance instrumentation

Record local timings for one first translation request:

- PNG encode
- request JSON/base64 construction
- WinHTTP send
- wait for response headers
- response body read
- JSON parse
- translated image render
- total

Only expose a compact total time in normal UI. Keep the detailed breakdown in diagnostics/logging so a slow model response is not confused with local rendering cost.

## Release order

1. Gate A: source consistency and stable connectivity diagnostics.
2. Gate B: capture revision + request reuse.
3. Gate C: side-panel interaction regression test.
4. Gate D: R2 WeChat-style local rendering.
5. Gate E: timing telemetry and performance tuning.
6. Long screenshot chunking only after the normal screenshot path is stable.

Do not combine Gate A and Gate D in the same test build. Network/request regressions and visual-rendering regressions must remain independently diagnosable.
