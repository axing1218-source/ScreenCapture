# WeShot v0.8.24 integration plan

## Current baseline

Use `weshot-v0.8.23` as the integration baseline. It already contains the newest multiscale Windows OCR geometry work and a successful Windows x64 CI artifact. Do not restart from the older v0.8.1 planning branch.

One regression must be handled explicitly: the current `Src/GeminiClient.h` on v0.8.23 is older than the previously validated diagnostics implementation. It still uses a 25 s receive timeout and a single generic WinHTTP failure path. Preserve the v0.8.23 OCR/layout work, but restore the previously validated Gemini transport behavior directly in source before further translation tuning.

## Gate 1 - stable Gemini transport

- Keep the selected Gemini model behavior unchanged from the user's current configuration.
- Restore a realistic inference receive ceiling (60 s) while keeping connect/send timeouts short.
- Split diagnostics into send failure vs waiting-for-response failure, with elapsed milliseconds.
- Make `Test connection` use a cheap model metadata request rather than an inference request.
- Keep CI as verification only; do not patch production Gemini code during the build.
- Do not change proxy behavior unless a new diagnostic proves a proxy-specific failure.

Acceptance: test connection reports success/failure quickly and a real screenshot translation cannot fail only because the client imposed the old 25 s ceiling.

## Gate 2 - OCR geometry as the single layout source

- Treat v0.8.23 multiscale Windows OCR geometry as the canonical local text-region map.
- Merge line boxes into paragraph groups locally before translation rendering.
- Gemini should translate text/paragraphs; it should not be asked to regenerate the screenshot.
- Keep original OCR text, paragraph bounds, reading order and translation text as separate cached data.

Acceptance: the same screenshot produces stable paragraph bounds across Original/Translated toggles and no second OCR pass is needed when only the language view changes.

## Gate 3 - WeChat-like screenshot interaction

- First click on `译` may perform OCR/translation; subsequent Original/Translated toggles are local and instantaneous.
- Changing the capture rectangle invalidates translation/OCR cache for that capture revision immediately.
- Do not open a separate translation window from the screenshot toolbar.
- While a request is pending, keep the screenshot usable and show a small in-place progress state rather than blocking the capture UI.

Acceptance: `截图 -> 译 -> 原文 -> 译` stays in the same capture surface, and the second/third toggles perform no network request.

## Gate 4 - OCR side panel isolation

- The OCR panel owns mouse-down/move/up, wheel, selection and copy gestures inside its bounds.
- Panel interaction must never arm or move the screenshot selection rectangle underneath it.
- Opening/closing the panel must not invalidate the current capture revision.
- The panel and toolbar translation view must consume the same OCR/translation cache.

Acceptance: drag-selecting text, scrolling and Ctrl+C in the panel never change the capture rectangle; toolbar and panel show the same source/translated text for one capture.

## Gate 5 - local translated-image rendering

- Remove source text locally using region-aware background sampling/inpainting; do not generate a replacement image with Gemini.
- Sample background primarily outside the glyph area, not by averaging the whole text box.
- Repaint translated text with adaptive font size, wrapping and alignment constrained by the paragraph box.
- Keep rendered Original and Translated surfaces cached for instant toggling.

Acceptance: translated text stays aligned to the original paragraph positions, backgrounds avoid obvious solid rectangles on simple UI screenshots, and toggling has no visible recomputation delay.

## Immediate implementation order

1. Restore stable Gemini transport in `GeminiClient.h` on the v0.8.23 baseline.
2. Add a shared capture/translation cache keyed by capture revision.
3. Route the OCR side panel and toolbar `译` action through that shared cache.
4. Add local paragraph grouping and translated-image rendering.
5. Measure PNG encode, network wait, response parse and local render separately before further latency tuning.

Do not mix network changes with rendering changes in the same commit; each gate should remain independently testable.