# WeShot v0.8.24 — Gemini transport gate

## Why this gate comes first
The v0.8.23 OCR geometry work is now the stable baseline. Before changing side-panel behavior, shared cache ownership, or WeChat-like rendering, restore deterministic Gemini transport behavior in production source so latency/failure results are meaningful.

## Verified current-source gap
`Src/GeminiClient.h` on `weshot-v0.8.23` still uses a 25 s receive timeout and collapses send/receive failures into one generic WinHTTP error. That makes Windows error 12002 ambiguous and can abort a valid Flash response before it returns.

## Gate T1 — transport only
Change only `GeminiClient.h` and connection-test code:

1. Keep `gemini-3.7-flash` as the default model.
2. Use supported low thinking for OCR/translation; do not send `minimal`.
3. Keep normal screenshot vision resolution at medium.
4. Keep connect/send timeouts short, but restore a 60 s receive ceiling for real inference.
5. Split `WinHttpSendRequest` and `WinHttpReceiveResponse` results so diagnostics say whether failure happened while sending or while waiting for Gemini, with elapsed milliseconds.
6. Make Settings → Test connection use `GET /v1beta/models/{model}` rather than inference, while preserving the same WinHTTP routing policy as real requests.
7. Do not add another proxy path in this gate. The existing automatic WinHTTP route has already reached Google successfully in prior builds; 12002 alone is not evidence of a proxy fault.
8. Keep all network calls off the UI thread and preserve the original screenshot/local OCR on every failure.

## Non-goals for T1
Do not modify OCR geometry, paragraph grouping, the OCR side panel, capture revision/cache ownership, translated-image rendering, long-screenshot tiling, annotation, or proxy UI in this commit. Those changes belong to later gates so a transport regression remains easy to isolate.

## Gate T1 acceptance
- Settings connection test returns model metadata quickly and never invokes generation.
- A real screenshot translation can wait up to the inference ceiling without the UI freezing.
- Errors distinguish `send` from `wait-response` and include elapsed milliseconds.
- HTTP/API errors still surface the Google response body/message.
- Windows/local OCR remains usable if Gemini fails.
- Existing v0.8.23 OCR placement and paragraph grouping are unchanged.

## Next gates after T1
T2 introduces one capture-scoped session containing revision, immutable pixels, OCR geometry, translation result, rendered overlay and in-flight request state. T3 makes the toolbar Translate action and OCR side panel share that session/request. T4 tightens side-panel mouse/keyboard isolation. R2 then improves local background reconstruction and DirectWrite fitting so translated text resembles WeChat's in-place translation without asking Gemini to generate images.

## Test sequence
Use the same screenshot for each build: Settings → Test connection; Screenshot → Translate and record first-response time; repeat Original/Translation toggles to verify zero extra requests; then run OCR on the same screenshot and verify v0.8.23 text boxes/paragraphs are unchanged.
