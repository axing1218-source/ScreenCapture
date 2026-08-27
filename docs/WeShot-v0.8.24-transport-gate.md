# WeShot v0.8.24 — Gemini transport gate

## Why this gate comes first
The v0.8.23 OCR geometry work is the stable baseline. Before changing side-panel behavior, shared cache ownership, or WeChat-like rendering, restore deterministic Gemini transport behavior in production source so latency/failure results are meaningful.

## Verified current-source gap
`Src/GeminiClient.h` on `weshot-v0.8.23` still has four production-source mismatches:

- `postGenerate()` uses a 25 s receive timeout and merges send/receive failures into one generic WinHTTP error.
- `addFastThinking()` can send `minimal` for Flash instead of the supported low-latency `low` setting used by the validated build path.
- `makeBaseRequest()` does not explicitly request medium image resolution for screenshot requests.
- `testConnection()` performs generation (`Reply with exactly OK`) instead of a lightweight model-metadata request.

These are independent of the newer Windows OCR geometry code and should be fixed without touching that OCR path.

## Gate T1 — transport only
Change only `GeminiClient.h` and the Settings connection-test behavior.

### T1.1 `addFastThinking()`
- Keep the existing Gemini 2.5 `thinkingBudget=0` compatibility branch.
- For non-2.5 models, send `thinkingLevel="low"`.
- Do not special-case Flash to `minimal`.

### T1.2 `makeBaseRequest()`
- Keep structured JSON response/schema behavior unchanged.
- When `png != nullptr && !png->empty()`, add `mediaResolution="MEDIA_RESOLUTION_MEDIUM"` to `generationConfig`.
- Do not add image-resolution configuration to text-only `translateOcrBlocks()` requests.
- Preserve current max-output-token limits so this gate measures transport/model latency rather than changing output behavior at the same time.

### T1.3 `postGenerate()`
Keep the existing `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY` route. Do not introduce a second proxy implementation in this gate.

Use separate timeout intent:
- resolve: about 8 s
- connect: about 8 s
- send: about 15 s
- receive/inference: 60 s

Split the call sequence so the error identifies the actual stage:
1. time `WinHttpSendRequest()`;
2. if it fails, report `Gemini 发送请求失败（Windows 错误 X，Y ms）`;
3. only when send succeeds, time `WinHttpReceiveResponse()`;
4. if that fails, report `等待 Gemini 响应失败（Windows 错误 X，Y ms）`;
5. only parse status/body after both stages succeed.

This keeps Windows error 12002 useful: it will show whether the timeout occurred while sending or while waiting for Gemini rather than being mislabeled as a proxy fault.

### T1.4 `testConnection()`
Do not call `postGenerate()` and do not run model inference.

Use the same host and automatic WinHTTP routing policy as real inference, but issue:

`GET /v1beta/models/{modelId}`

with the same `x-goog-api-key` header. On HTTP 2xx, show a compact success result containing the selected model and elapsed milliseconds. On non-2xx, reuse `getApiError()` so invalid/restricted keys expose Google's actual API message.

The connection test may use a shorter receive ceiling than inference because it is metadata-only; it must still distinguish send vs wait-response failure.

## T1 invariants
The T1 commit must not change:

- Windows OCR recognition or multiscale geometry;
- paragraph grouping/reading-order logic;
- OCR side-panel layout or mouse/keyboard routing;
- toolbar Translate UI/state machine;
- capture revision/cache ownership;
- translated-image background repair/text fitting;
- long-screenshot tiling;
- annotation behavior;
- proxy UI/settings.

Keeping these frozen makes regressions attributable to Gemini transport rather than UI or OCR changes.

## Gate T1 acceptance
- Settings connection test returns model metadata quickly and never invokes generation.
- Invalid API keys fail through HTTP/API diagnostics rather than a fake inference result.
- A real screenshot translation can wait up to the 60 s inference ceiling without freezing the UI thread.
- Errors distinguish `send` from `wait-response` and include elapsed milliseconds.
- HTTP/API failures preserve Google's response message.
- Screenshot requests use low thinking + medium image resolution; text-only OCR-block translation uses low thinking without unnecessary image-resolution config.
- Windows/local OCR remains usable if Gemini fails.
- Existing v0.8.23 OCR placement and paragraph grouping are byte-for-byte behaviorally unchanged.

## Next gates after T1
### T2 — capture-scoped session
Introduce one capture-scoped object containing revision, immutable BGRA pixels, OCR geometry/text, translation result, translated overlay cache, and in-flight request state. Selection changes invalidate derived state once per committed revision.

The session should be owned by the active capture window rather than a process-global singleton. A minimal shape is:

- `revision`: monotonic capture identity;
- `snapshot`: immutable BGRA pixels + width/height + absolute virtual-screen origin;
- `ocr`: local OCR blocks/paragraphs for the revision;
- `translation`: translated text + geometry for the revision;
- `overlay`: locally rendered translated bitmap/cache;
- `requestState`: none / OCR pending / translation pending / ready / error;
- `lifetimeToken`: lets background callbacks prove the capture window is still alive.

A selection resize/move should hide any translated overlay immediately for responsiveness, but increment revision only once when the new rectangle is committed. Derived OCR/translation/overlay state belongs to one revision and must never migrate to another.

### T3 — request reuse
Make toolbar Translate and the OCR side panel share the same capture session. If reliable local OCR already exists for the current revision, translation should prefer text-only `translateOcrBlocks()` instead of uploading the screenshot again. Concurrent callers for the same revision should join one in-flight request.

Request reuse key should include at least `(revision, targetLanguage, requestKind)`. A second caller should attach a waiter to the same future/result rather than start another HTTP request. A completed translation is cached until revision or target language changes.

Every async completion must pass both checks before touching UI:

1. `lifetimeToken` still valid;
2. callback revision equals current session revision.

If either fails, discard the result silently.

### T4 — OCR side-panel isolation
The OCR panel must own mouse-down through mouse-up for text selection/scrolling and consume relevant keyboard input (`Ctrl+C`, selection navigation, wheel), so no event reaches `CutMask::startAdjust()` or screenshot hotkeys while the side panel is handling it.

Interaction rules:

- entering the panel does not change capture revision;
- clicking/dragging text never arms screenshot move/resize;
- wheel scroll is consumed by the panel when the pointer is over it;
- `Ctrl+C` copies selected OCR/translation text only;
- `Esc` first clears panel-local selection/focus when appropriate, and only then falls back to capture cancel behavior;
- switching Original/Translated text tabs is local state only and never triggers a new Gemini call;
- closing/reopening the panel for the same revision reuses cached OCR/translation data.

### R2 — WeChat-like local rendering
Gemini returns text and geometry only. WeShot reconstructs translated imagery locally: sample background around text-region edges rather than averaging the entire text box, remove/cover source glyphs, estimate source foreground contrast, fit DirectWrite text by box size/line count, and keep Original/Translation switching as a local zero-network operation.

Rendering pipeline:

1. expand each OCR/text rectangle slightly and sample a thin ring just outside it;
2. estimate background from robust edge statistics (median/cluster), not the glyph-filled center;
3. fill/repair the source text region locally;
4. estimate foreground luminance/contrast from the original text neighborhood;
5. lay out translated text with DirectWrite using the original box width, paragraph alignment, and line-count target;
6. shrink font size only when measured layout overflows; do not stretch text horizontally;
7. cache the finished overlay bitmap per revision so Original/Translated toggles are instant.

For complex/photo backgrounds, R2 should prefer a conservative soft fill/blur over an aggressive inpainting algorithm at first; correctness and speed matter more than perfect reconstruction. A later R3 can add stronger local inpainting if needed.

## WeChat-like interaction contract
The screenshot window should behave as one continuous surface rather than opening a second translation workflow:

- toolbar `译` starts translation in-place and changes to a clear pending state;
- translation success replaces only detected text regions while preserving the rest of the screenshot exactly;
- after success, the same control toggles Original ↔ Translated locally;
- OCR side panel and toolbar always reflect the same revision and translation state;
- moving/resizing the screenshot immediately returns the visible image to Original and invalidates old derived results;
- annotation tools remain usable on the currently visible image, but annotation changes must not alter OCR/translation cache identity unless the product explicitly decides to OCR annotated pixels in a later version.

## Performance instrumentation
Do not measure Gemini as one opaque duration. Record compact stage timings so latency tuning remains evidence-based:

- snapshot/PNG encode;
- request construction/base64;
- WinHTTP send;
- wait for response headers/model inference;
- response read/JSON parse;
- local overlay render.

Only show concise user-facing timing such as total translation duration; keep stage timings in debug/log output. This will separate network/model latency from local rendering cost without adding visual noise.

## Test sequence
Use one fixed screenshot for A/B comparison:

1. Settings → Test connection; record displayed model and elapsed ms.
2. Screenshot → Translate; record first-response time and any stage-specific failure.
3. Toggle Original/Translation repeatedly; verify zero extra requests.
4. Run OCR on the same screenshot; verify v0.8.23 small-text boxes, paragraph grouping, and reading order are unchanged.
5. Test one invalid API key; confirm the API error is explicit and local OCR still works.
6. Open the OCR side panel and drag-select/copy/scroll text; verify the screenshot rectangle never moves or resizes.
7. Start translation, then resize the capture before Gemini returns; verify the old callback is discarded and never paints on the new revision.
8. On the same revision, trigger toolbar Translate and side-panel Translate close together; verify only one Gemini request is sent.
9. Test mixed-DPI/negative-coordinate multi-monitor placement; translated boxes must remain aligned with the physical-pixel capture.
10. Compare a flat UI background and a photographic background; verify R2 never damages non-text content outside the detected text regions.
