# WeShot v0.8.4 execution order

This note turns the current WeShot plan into a regression-friendly implementation order for the xland/ScreenCapture fork. The goal is WeChat-like screenshot interaction without coupling network, OCR-panel, and translated-image rendering changes into one patch.

## Gate A — Gemini source parity

Make `Src/GeminiClient.h` itself match the already-tested v0.8.3 diagnostics behavior before changing UI:

- Gemini 3.x screenshot OCR/translation uses `thinkingLevel=low`; keep the Gemini 2.5 compatibility path unchanged.
- Image-bearing requests use `MEDIA_RESOLUTION_MEDIUM`; text-only OCR-block translation does not.
- Real inference keeps short connect/send limits but allows up to 60 seconds while waiting for a Gemini response.
- Report send failure and wait-for-response failure separately, with elapsed milliseconds.
- Settings `Test connection` uses authenticated `GET /v1beta/models/{modelId}` rather than generating `OK`.
- Keep the existing automatic WinHTTP proxy policy in this gate so transport changes are not mixed with API changes.

After this compiles once, CI must stop rewriting Gemini production code and only assert the above invariants.

## Gate B — one capture revision, one request graph

Introduce a small capture-session state owned by the screenshot result flow:

- `captureRevision` increments whenever the selected pixels change, including resize/move recapture and long-screenshot replacement.
- OCR result, translation result and in-flight request all carry the revision they belong to.
- A callback whose revision is stale is discarded before touching the UI.
- Toolbar Translate and OCR-side-panel Translate share the same cache and in-flight request for the same revision.
- If OCR text/boxes already exist for the same revision, translating from the OCR panel should use text-only block translation instead of uploading the image again.

Acceptance: changing the selection while Gemini is working must never paint the old translation over the new capture.

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

## Gate D — local translated-image rendering, WeChat style

Gemini returns text plus normalized text boxes only. WeShot performs all image compositing locally.

R2 renderer:

- Derive each text block rectangle from normalized coordinates.
- Sample background from a ring around the text block rather than averaging the text-covered interior.
- Fill/repair the old text region locally; do not ask Gemini to generate an image.
- Estimate text luminance/contrast from source pixels and choose a readable translated-text color.
- Fit translated text using measured layout: reduce font size until it fits, preserve multiline wrapping, and prefer original alignment when it can be inferred.
- Expand very tight boxes slightly for CJK glyphs, but clip to the image bounds.

R3+ can add stronger inpainting/gradient reconstruction only after R2 is stable and fast.

## Gate E — latency instrumentation

Expose one compact timing string in diagnostics builds:

`encode -> request build -> send -> wait -> parse -> render -> total`

Do not optimize based on total time alone. The first target is avoiding client-created timeouts and duplicate requests; visual rendering should remain local and normally negligible compared with network inference.

## Test matrix after each gate

Use the same four screenshots each time: English chat, mixed Chinese/English, dense UI text, and text on non-flat background.

1. Settings -> Test connection.
2. Screenshot -> Translate; record first response time.
3. Switch Original/Translation repeatedly; no network request should occur.
4. Open OCR panel; verify text selection/copy and panel scrolling do not alter the capture.
5. Start Translate and immediately change capture pixels; stale result must be discarded.
6. Compare translated-image placement, font fit and background repair against the previous build.

Do not begin Gate D visual tuning until Gates A-C pass this matrix.
