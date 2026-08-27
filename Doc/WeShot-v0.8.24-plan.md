# WeShot v0.8.24 integration plan

## Current baseline

Use `weshot-v0.8.23` as the integration baseline. It already contains the newest multiscale Windows OCR geometry work and a successful Windows x64 CI artifact. Do not restart from the older v0.8.1 planning branch.

One regression must be handled explicitly: the current `Src/GeminiClient.h` on v0.8.23 is older than the previously validated diagnostics implementation. It still uses a 25 s receive timeout and a single generic WinHTTP failure path. Preserve the v0.8.23 OCR/layout work, but restore the previously validated Gemini transport behavior directly in source before further translation tuning.

## Shared capture-state contract

All screenshot OCR and translation UI should consume one capture-state object owned by the active capture window rather than each feature copying pixels independently.

Minimum state per committed capture rectangle:

- `revision`: monotonically increasing capture identity.
- immutable BGRA snapshot and pixel dimensions.
- absolute virtual-desktop origin for the snapshot.
- local OCR text blocks and paragraph groups.
- translated paragraph strings keyed by target language.
- cached Original and Translated render surfaces.
- optional in-flight OCR/translation request handles for request coalescing.

Rules:

- Opening/closing OCR, switching Original/Translated, copying text, scrolling the panel and annotation actions do not create a new revision.
- A committed rectangle change invalidates OCR, translation and translated-surface cache for the old revision immediately.
- Async results may update UI only if both the capture revision and the capture-window lifetime token still match.
- Toolbar `译` and OCR-side-panel translation must join the same in-flight request for one revision/language instead of uploading the same screenshot twice.

## Gate 1 - stable Gemini transport

- Keep the selected Gemini model behavior unchanged from the user's current configuration.
- Restore a realistic inference receive ceiling (60 s) while keeping connect/send timeouts short.
- Split diagnostics into send failure vs waiting-for-response failure, with elapsed milliseconds.
- Make `Test connection` use a cheap model metadata request rather than an inference request.
- Keep CI as verification only; do not patch production Gemini code during the build.
- Do not change proxy behavior unless a new diagnostic proves a proxy-specific failure.

Acceptance: test connection reports success/failure quickly and a real screenshot translation cannot fail only because the client imposed the old 25 s ceiling.

### Gate 1 implementation boundary

Apply the transport fix as one isolated source commit before any UI/cache/rendering changes:

1. Keep `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`; do not introduce another proxy path in this gate.
2. In the real `generateContent` path, use short resolve/connect/send limits and a 60 s receive limit.
3. Split `WinHttpSendRequest` and `WinHttpReceiveResponse` into separate checks. Preserve the Windows error code and record elapsed milliseconds for each phase.
4. Change `Test connection` to `GET /v1beta/models/{model}` with the same API host, API key header and WinHTTP access type as production requests. It must not consume inference tokens.
5. The test result should include the selected model and elapsed milliseconds on success, so a user screenshot can distinguish API/model problems from slow inference.
6. Keep transport diagnostics out of the normal toolbar; surface them only in Settings/test status and failure text.
7. Do not alter OCR prompts, schemas, translation prompts, paragraph mapping or rendering in this commit.

Transport regression cases:

- invalid API key -> fast HTTP/API error, not a 60 s wait;
- valid key/model -> metadata test returns quickly without generating text;
- slow but valid screenshot inference -> allowed to exceed 25 s without client-side 12002 solely from the old receive ceiling;
- send failure and receive timeout -> visibly different messages with elapsed time;
- existing Windows automatic-proxy behavior -> unchanged.

After this source commit passes CI, tag its artifact as the new Gemini transport test baseline. Only then begin the shared capture-state work.

## Gate 2 - OCR geometry as the single layout source

- Treat v0.8.23 multiscale Windows OCR geometry as the canonical local text-region map.
- Merge line boxes into paragraph groups locally before translation rendering.
- Gemini should translate text/paragraphs; it should not be asked to regenerate the screenshot.
- Keep original OCR text, paragraph bounds, reading order and translation text as separate cached data.
- Prefer text-only translation when local OCR is already available and its geometry passes confidence/coverage checks; use image-to-text Gemini only as a fallback for OCR-poor screenshots.

Acceptance: the same screenshot produces stable paragraph bounds across Original/Translated toggles and no second OCR pass is needed when only the language view changes.

## Gate 3 - WeChat-like screenshot interaction

- First click on `译` may perform OCR/translation; subsequent Original/Translated toggles are local and instantaneous.
- Changing the capture rectangle invalidates translation/OCR cache for that capture revision immediately.
- Do not open a separate translation window from the screenshot toolbar.
- While a request is pending, keep the screenshot usable and show a small in-place progress state rather than blocking the capture UI.
- If the user adjusts the rectangle while translation is pending, hide the pending translated surface immediately and silently discard the late result for the old revision.

Acceptance: `截图 -> 译 -> 原文 -> 译` stays in the same capture surface, and the second/third toggles perform no network request.

## Gate 4 - OCR side panel isolation

- The OCR panel owns mouse-down/move/up, wheel, selection and copy gestures inside its bounds.
- Panel interaction must never arm or move the screenshot selection rectangle underneath it.
- Opening/closing the panel must not invalidate the current capture revision.
- The panel and toolbar translation view must consume the same OCR/translation cache.
- Keyboard focus inside the text panel owns Ctrl+C/Ctrl+A and text navigation; capture-window hotkeys resume only when focus leaves the panel.

Acceptance: drag-selecting text, scrolling and Ctrl+C in the panel never change the capture rectangle; toolbar and panel show the same source/translated text for one capture.

## Gate 5 - local translated-image rendering

- Remove source text locally using region-aware background sampling/inpainting; do not generate a replacement image with Gemini.
- Sample background primarily outside the glyph area, not by averaging the whole text box.
- Repaint translated text with adaptive font size, wrapping and alignment constrained by the paragraph box.
- Keep rendered Original and Translated surfaces cached for instant toggling.
- Start with deterministic edge sampling + fill for flat UI backgrounds; only add heavier inpainting for regions where edge-color variance indicates a non-flat background.

Acceptance: translated text stays aligned to the original paragraph positions, backgrounds avoid obvious solid rectangles on simple UI screenshots, and toggling has no visible recomputation delay.

## Performance instrumentation

Measure these phases separately instead of treating “translation time” as one number:

1. capture snapshot copy / PNG encode,
2. local Windows OCR,
3. paragraph grouping,
4. Gemini request send,
5. Gemini response wait,
6. response parse / mapping,
7. local background repair and text rendering.

Keep these timings in debug/status output only; they should not clutter the normal screenshot toolbar.

For transport builds, record `send_ms` and `wait_ms` independently before adding broader phase timing. This gives a clean before/after comparison without conflating Gemini inference latency with local OCR or drawing time.

## Regression test matrix

Each functional build should cover at least:

- small English UI text,
- Chinese + English mixed screenshot,
- multiple font sizes in one screenshot,
- dense chat/message screenshot,
- flat-color UI background,
- photo/background image with text,
- selection changed while Gemini request is pending,
- OCR side-panel text selection + wheel + Ctrl+C,
- repeated `译 -> 原文 -> 译` toggles with no second network request,
- secondary monitor / negative virtual-desktop coordinates when available.

## Immediate implementation order

1. Restore stable Gemini transport in `GeminiClient.h` on the v0.8.23 baseline, using the isolated Gate 1 boundary above.
2. Build and test that transport-only commit before touching screenshot state.
3. Add the shared capture-state/cache keyed by capture revision.
4. Route the OCR side panel and toolbar `译` action through that shared state and coalesce duplicate requests.
5. Add local paragraph grouping and translated-image rendering.
6. Add the remaining phase timings and run the regression matrix before further latency tuning.

Do not mix network changes with rendering changes in the same commit; each gate should remain independently testable.