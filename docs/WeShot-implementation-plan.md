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

## Capture revision and cache correctness
Every pixel-affecting selection state has a monotonically increasing revision.

Invalidate the active OCR/translation cache when:
- the capture rectangle moves or resizes;
- a new capture starts;
- a long-screenshot result replaces the image;
- any operation changes the source pixels used for OCR/translation.

Each asynchronous Gemini request records the revision it started from. A completed response may update the UI only if its revision still matches the active capture. Stale responses are discarded silently instead of being painted onto a newer selection.

## OCR side panel behavior
- Right-side text is independently selectable and copyable.
- Original/translation tabs switch both the textual result and the image preview consistently.
- Copy copies the currently selected mode, not a hidden stale buffer.
- Panel mouse input must be consumed before capture hit-testing.
- Gemini failure must not discard already available Windows/local OCR text.
- Closing the side panel must return keyboard focus to the capture surface without cancelling or moving the active selection.

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

## Acceptance tests for the current baseline
1. **Settings connectivity:** `Test connection` returns model metadata plus elapsed milliseconds without invoking model inference.
2. **Real translation timing:** `Screenshot → Translate` reports enough timing detail to distinguish PNG encode, send/wait, parse, and render costs.
3. **Local toggle:** after one successful translation, repeated Original/Translation switches perform no new network request and appear immediate.
4. **Selection safety:** moving/resizing the selection during an in-flight request prevents the old result from being painted.
5. **Panel isolation:** selecting/copying OCR text never moves or resizes the screenshot selection.
6. **Failure fallback:** Gemini failure leaves the original screenshot and any local OCR result intact.
7. **Windows 10 smoke test:** verify the above on Windows 10 22H2 before introducing more rendering complexity.

## Implementation order from v0.8.3
1. **Stabilize connectivity diagnostics.** Validate metadata connection test and real inference timing on Windows 10 22H2.
2. **Move Gemini production parameters into `GeminiClient.h`.** Remove CI-only behavior patches so local builds and CI builds execute the same code.
3. **Finish revision-based invalidation.** Apply the same rule to toolbar translation, OCR result window, long capture, and selection edits before adding more visual complexity.
4. **Add lightweight timing telemetry in UI.** PNG encoding, request construction, send/wait, parse, and render timings; do not transmit telemetry externally.
5. **Refine local translation rendering to R2.** Border/background sampling, better foreground color, adaptive font fitting, and left/center alignment selection.
6. **Long-screenshot Gemini tiling.** Overlapping tiles with coordinate remapping and deduplication.
7. **Snipaste-style annotation refinement.** Tool toggling, secondary controls, width/color/fill/opacity, mosaic/text behavior, undo/redo consistency.
8. **Windows 10/11 smoke-test pass and packaging.** Single portable executable, no localhost dependency.

## Current test baseline
The latest v0.8.3 diagnostic build is the current test baseline. First test Settings → **Test connection**. A successful result should identify the selected model and elapsed milliseconds. Then test **Screenshot → Translate** and record first-response time. After the first translation completes, repeatedly switch Original/Translation; those switches should be immediate and should not send another Gemini request.
