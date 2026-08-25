# WeShot implementation plan

## Goal
Turn the xland/ScreenCapture fork into a portable WeChat-like Windows screenshot utility with fast OCR and in-place translation while preserving the mature capture, long-capture, annotation, recording, QR, save, and clipboard flows.

## Interaction target
- Keep the current full-screen capture surface, window snapping, drag-to-select, adjustable selection, magnifier, double-click copy, Enter copy, and Esc cancel behavior.
- Preserve the floating toolbar near the active selection.
- Long screenshot remains manual-wheel/trackpad first, with automatic scrolling as an explicit action.
- OCR and translation must stay inside the capture workflow; they must not destroy, resize, or replace the active selection.
- The OCR result panel stays docked beside the selection and uses isolated hit-testing so text selection, Copy, tabs, Retry, and Close cannot trigger selection resize/drag.
- The screenshot toolbar translation action follows the WeChat interaction: first click performs OCR/translation, then the same selection can switch instantly between original and translated views without another network request.

## Current implementation state
- Windows x64 CI build and downloadable artifact: **done**.
- Manual-first long screenshot and explicit auto-scroll control: **done**.
- Configurable capture-border thickness including 0 px: **done**.
- In-process OCR/result UI: **done**.
- Gemini API key/model settings and connection-test UI: **done**.
- Gemini image OCR with structured text blocks: **done**.
- Gemini screenshot translation with normalized text-region boxes: **done**.
- Original/translation tabs in the OCR window: **done**.
- Screenshot-toolbar in-place translation toggle: **done**.
- Local redraw of translated text over the captured image: **baseline done; visual quality still needs refinement**.

## Gemini request architecture
1. Capture the selected BGRA pixels without modifying the original screenshot buffer.
2. Encode the selection as PNG in memory using WIC.
3. Send the PNG directly to Gemini over HTTPS. No localhost bridge, browser extension, Baidu OCR process, or generated-image workflow.
4. Request structured JSON with OCR/translation text and normalized `box_2d` regions.
5. Parse the response into stable text blocks.
6. Cache the result against the current capture revision.
7. Render translated text locally in WeShot; after the first response, original/translation switching is local and immediate.

### Request reuse policy
Latency is more important than forcing every UI path through identical network calls. Use the cheapest request that already has enough information:
- **Toolbar Translate on a fresh selection:** one image request that performs OCR + translation + boxes in the same round trip. Do not run a separate OCR request first.
- **OCR panel opened first:** one image OCR request. If the user then presses Translate and the OCR blocks are still valid for the same revision, send only those OCR text blocks for translation; do not upload the screenshot again.
- **Translation already cached:** opening the OCR panel, switching tabs, copying text, or toggling Original/Translation must reuse the same cached blocks and issue zero Gemini requests.
- **Concurrent actions:** if toolbar Translate and OCR panel Translate are requested while an equivalent request for the same revision is already in flight, attach both UI consumers to that request instead of starting a duplicate call.
- Cache keys include capture revision, model, and target language. Changing any of those invalidates only the affected derived result, not the canonical original pixels.

## Network and latency rules
- Keep `gemini-3.7-flash` as the default model unless testing shows a concrete regression.
- Use the model's low-latency supported thinking configuration rather than unsupported `minimal` settings.
- Use medium vision resolution for normal desktop screenshots; do not send high-resolution vision data unless recognition quality requires it.
- The Settings “Test connection” action must use the model metadata endpoint rather than running inference merely to generate `OK`.
- The connection test and real inference must use the same WinHTTP routing policy; avoid adding a separate proxy path unless a reproducible network failure proves it is needed.
- Real OCR/translation requests use a longer receive ceiling than the connection test; a slow model response must not be confused with a proxy failure.
- Error text should distinguish request-send failure from waiting-for-model-response timeout and include elapsed milliseconds where useful.
- Do not infer “proxy failure” from Windows 12002 alone; it only establishes that a WinHTTP stage timed out.
- Network calls remain off the UI thread.

## Translation state machine
Use one shared state model for toolbar translation and the OCR result window so the two UI paths cannot drift apart.

States:
- **OriginalReady**: original pixels are valid, no usable translation is cached for the current revision.
- **Translating**: one request is in flight for a specific capture revision; the original view remains visible and interactive.
- **TranslationReady**: translated blocks and/or translated bitmap are cached for the same revision; Original/Translation switching is fully local.
- **TranslationError**: original/local OCR remains available; Retry starts a new request for the current revision.

Transitions:
- `OriginalReady → Translating` only on an explicit Translate action.
- `Translating → TranslationReady` only when the completed request revision equals the active capture revision.
- `Translating → OriginalReady` when the selection/pixels change; the old callback may finish later but must be discarded.
- `Translating → TranslationError` on transport/API/parse failure without replacing the original image.
- `TranslationReady → OriginalReady` whenever the source pixels or selection revision changes.
- Original/Translation tab or toolbar toggles never create a network request while in `TranslationReady`.

UI rule: show a small non-blocking “Translating…” state near the existing toolbar/panel control rather than opening a modal window or freezing the capture surface.

## Translation rendering: WeChat-like direction
Gemini should not generate a translated image. The rendering pipeline is local:

1. Detect/receive text region boxes.
2. Preserve the untouched source image as the canonical original.
3. Estimate the background around each text region.
4. Remove/cover the source glyphs using a local background-reconstruction step.
5. Estimate source foreground color when practical.
6. Fit translated text into the original region using adaptive font size and wrapping.
7. Draw translated text locally and cache the translated bitmap/overlay.

### Rendering milestones
- **R1 baseline:** flat sampled background fill + centered translated text. Implemented.
- **R2 next:** sample border/background pixels rather than averaging the text interior; this avoids contaminating the fill with source glyph colors.
- **R3:** lightweight local inpainting/edge-aware fill for gradients, chat bubbles, and textured UI backgrounds.
- **R4:** source text color estimation and adaptive alignment/font sizing so translated text visually follows the original rather than defaulting to black/white centered text.
- **R5:** paragraph grouping for neighboring lines so long translations wrap as a paragraph instead of independent labels.

### R2 algorithm checkpoint
For each translated block:
1. Expand the block rectangle by a small clamped margin.
2. Sample four narrow border strips outside/at the edge of the text box, excluding high-gradient pixels where possible.
3. Use a robust statistic (median or trimmed mean per channel) instead of averaging the glyph-filled interior.
4. Compare strip colors. If they disagree strongly, keep the original background and defer that block to R3 rather than painting an obvious flat rectangle.
5. Estimate text luminance from pixels that differ most from the background estimate, then choose a foreground color close to the source rather than unconditional black/white.
6. Fit text using measured DirectWrite layout; reduce font size until the translated paragraph fits the original box within a defined minimum size.

## Capture revision and cache correctness
Every pixel-affecting selection state has a monotonically increasing revision.

Invalidate the active OCR/translation cache when:
- the capture rectangle moves or resizes;
- a new capture starts;
- a long-screenshot result replaces the image;
- any operation changes the source pixels used for OCR/translation.

Each asynchronous Gemini request records the revision it started from. A completed response may update the UI only if its revision still matches the active capture. Stale responses are discarded silently instead of being painted onto a newer selection.

### Shared per-revision cache
Keep one capture-scoped cache instead of separate toolbar and OCR-window copies:
- source BGRA pixels / dimensions;
- revision id;
- OCR text + OCR boxes;
- translation text + translation boxes keyed by target language/model;
- locally rendered translated overlay/bitmap;
- request state and timing diagnostics.

The toolbar and OCR result window are views over this cache. Neither owns the canonical data. Closing the OCR panel therefore must not destroy a translation already available to the toolbar, and reopening it must not trigger another request.

## OCR side panel behavior
- Right-side text is independently selectable and copyable.
- Original/translation tabs switch both the textual result and the image preview consistently.
- Copy copies the currently selected mode, not a hidden stale buffer.
- Panel mouse input must be consumed before capture hit-testing.
- Gemini failure must not discard already available Windows/local OCR text.
- Closing the side panel must return keyboard focus to the capture surface without cancelling or moving the active selection.
- While selecting text, mouse move/up events remain owned by the panel until the selection gesture ends, even if the pointer crosses the panel edge.
- Ctrl+C targets panel text only when the panel/text control owns focus; otherwise existing screenshot copy behavior is preserved.
- Opening/closing the side panel cannot alter the capture revision because it does not change source pixels.

## Long screenshot rules
- Preserve original stitched pixels and save losslessly as PNG.
- Preview scaling is separate from final output quality.
- Large Gemini requests may use a reduced network copy while save/copy continues to use the original pixels.
- Long screenshots larger than the safe request limit should be translated in overlapping vertical tiles, then merged by text-region coordinates; do not ask Gemini to regenerate a full translated image.

## Failure handling
- Missing API key: local OCR remains usable; translation clearly indicates configuration is required.
- Invalid model/key: show the HTTP/API error in Settings without closing capture state.
- Network timeout: identify whether failure occurred while sending or while waiting for Gemini.
- Empty/invalid structured response: keep original image and any available OCR result, allow Retry.
- Stale asynchronous response: discard it based on revision mismatch.

## Timing telemetry (local only)
Capture a small per-request timing struct; do not send telemetry anywhere.
- `pngEncodeMs`
- `requestBuildMs`
- `sendMs`
- `waitResponseMs`
- `readBodyMs`
- `parseMs`
- `renderMs`
- `totalMs`

The UI normally shows only `totalMs`. Detailed stage timings may be shown in a diagnostic status line or copied to logs when an error occurs. This lets us distinguish model latency from local rendering and networking without changing the user workflow.

## Acceptance tests for the current baseline
1. **Settings connectivity:** `Test connection` returns model metadata plus elapsed milliseconds without invoking model inference.
2. **Real translation timing:** `Screenshot → Translate` reports enough timing detail to distinguish PNG encode, send/wait, parse, and render costs.
3. **Local toggle:** after one successful translation, repeated Original/Translation switches perform no new network request and appear immediate.
4. **Selection safety:** moving/resizing the selection during an in-flight request prevents the old result from being painted.
5. **Panel isolation:** selecting/copying OCR text never moves or resizes the screenshot selection.
6. **Failure fallback:** Gemini failure leaves the original screenshot and any local OCR result intact.
7. **Request reuse:** `OCR → Translate` reuses valid OCR blocks with a text-only translation request; reopening the panel or toggling views makes no request.
8. **Duplicate suppression:** simultaneous toolbar/panel translation for one revision creates one in-flight network operation, not two.
9. **Windows 10 smoke test:** verify the above on Windows 10 22H2 before introducing more rendering complexity.

### Minimal test matrix
Use the same small set of screenshots for each build so regressions are comparable:
- plain black text on white background;
- white text on a dark UI panel;
- multi-line English chat bubble translated to Simplified Chinese;
- mixed Chinese/English numbers and punctuation;
- small UI text (roughly 12–14 px at 100% scaling);
- 125% or 150% Windows display scaling;
- a long screenshot with repeated chat bubbles crossing tile boundaries.

For each sample record: OCR correctness, first-response total time, wait-response time, whether local toggle is instant, whether a duplicate request occurred, and whether the translated overlay visibly damages the background.

## Implementation order from v0.8.3
1. **Move Gemini production parameters into `GeminiClient.h`.** Remove CI-only behavior patches so local builds and CI builds execute the same code.
2. **Unify capture-scoped OCR/translation cache.** Toolbar and side panel share one revision/model/language keyed result and one in-flight request.
3. **Finish revision-based invalidation.** Apply the same rule to toolbar translation, OCR result window, long capture, and selection edits before adding more visual complexity.
4. **Add lightweight timing telemetry in UI.** PNG encoding, request construction, send/wait, parse, and render timings; do not transmit telemetry externally.
5. **Refine local translation rendering to R2.** Border/background sampling, better foreground color, adaptive font fitting, and left/center alignment selection.
6. **Long-screenshot Gemini tiling.** Overlapping tiles with coordinate remapping and deduplication.
7. **Snipaste-style annotation refinement.** Tool toggling, secondary controls, width/color/fill/opacity, mosaic/text behavior, undo/redo consistency.
8. **Windows 10/11 smoke-test pass and packaging.** Single portable executable, no localhost dependency.

## Source migration checkpoint
Before changing visual rendering further, land the current CI-injected Gemini behavior directly in source:
- `gemini-3.7-flash` uses supported low thinking, never `minimal`;
- normal screenshot image requests use medium vision resolution;
- inference receive ceiling is long enough to avoid misclassifying a slow model response as network failure;
- send and wait-response stages report separately;
- Settings test uses `GET /v1beta/models/{model}` with the same WinHTTP routing policy as inference;
- after source is updated, delete the workflow-time `Patch Gemini diagnostics` production rewrite and replace it with assertions that fail CI if source regresses.

CI must build production behavior from source; it must not define production behavior.

## Current test baseline
The latest v0.8.3 diagnostic build is the current test baseline. First test Settings → **Test connection**. A successful result should identify the selected model and elapsed milliseconds. Then test **Screenshot → Translate** and record first-response time. After the first translation completes, repeatedly switch Original/Translation; those switches should be immediate and should not send another Gemini request.
