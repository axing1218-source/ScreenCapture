# WeShot Gate A source-parity preflight

This note refines the next implementation step after reviewing the current `Src/GeminiClient.h` on `weshot-v0.8.1-fix`. It is intentionally limited to request/transport parity so OCR-panel and translated-image UI changes remain independently testable.

## Confirmed source mismatches

The current source still differs from the already-tested v0.8.3 diagnostics behavior in four important places:

1. `addFastThinking()` selects `minimal` for Flash models; the next source patch should use `low` for Gemini 3.x while leaving the Gemini 2.5 `thinkingBudget=0` compatibility path unchanged.
2. `postGenerate()` uses a 25-second receive timeout and collapses send + receive failures into one message. The source patch should use the diagnostics baseline of a 60-second inference receive ceiling and distinguish `WinHttpSendRequest` failure from `WinHttpReceiveResponse` timeout/failure.
3. `testConnection()` performs a real `generateContent` call with `Reply with exactly OK`. It should instead make an authenticated metadata GET to `/v1beta/models/{modelId}` using the same WinHTTP access policy as inference.
4. `makeBaseRequest()` does not yet apply `MEDIA_RESOLUTION_MEDIUM` to image-bearing screenshot requests.

## Narrow patch shape

Keep `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`, the Google host, API-key header, and `generateContent` endpoint unchanged in Gate A. We already have evidence that this route reaches Google, so proxy experiments would make a regression harder to isolate.

### `addFastThinking()`

- Gemini 2.5 branch: unchanged.
- Gemini 3.x branch: always `thinkingLevel=low` for this screenshot OCR/translation client.
- Do not add model-family guessing beyond the existing 2.5 compatibility condition in this patch.

### `makeBaseRequest()`

- Detect whether `png != nullptr && !png->empty()`.
- Only in that image-bearing case, add `mediaResolution=MEDIA_RESOLUTION_MEDIUM` to `generationConfig`.
- Text-only `translateOcrBlocks()` must not inherit an image-specific resolution option.
- Keep response schema, MIME type and token limits unchanged.

### `postGenerate()`

- Keep connection and send timeouts short enough to fail quickly on genuine routing errors.
- Restore inference receive timeout to 60 seconds.
- Time `WinHttpSendRequest` and `WinHttpReceiveResponse` separately with `GetTickCount64()`.
- Emit distinct messages for `send` and `wait for response` failures. Error 12002 should be reported as a timeout at the failing stage, not as proof of a proxy problem.
- Do not change response parsing in this gate.

### `testConnection()`

- Use `GET /v1beta/models/{modelId}`.
- Use the same host, x-goog-api-key header, TLS flag and `AUTOMATIC_PROXY` access policy as production inference.
- Use a shorter receive timeout than inference because this endpoint does not run model generation.
- Return model name + elapsed milliseconds on success.
- Keep the existing `TestResult` public shape so Settings UI call sites do not need to change yet.

## Build/CI transition

The first build after the source patch should still leave the existing CI rewrite in place only long enough to confirm the source and patch produce identical behavior. Immediately after that passes, replace the CI rewrite with assertions only.

CI should fail if any of these regress:

- Gemini 3.x Flash path contains `minimal`.
- `testConnection()` contains the `Reply with exactly OK` prompt.
- image-bearing requests lack `MEDIA_RESOLUTION_MEDIUM`.
- send and wait-response diagnostics are not separate.
- workflow still mutates the production Gemini request code.

## Regression boundary

Do not modify the OCR side panel, capture revision cache, toolbar Translate state, background reconstruction or text layout in the Gate A commit. If the next binary has a network/API regression, we want only `GeminiClient.h` and CI parity to be in scope.

## Next binary acceptance order

1. Settings -> Test connection: should return the selected model and elapsed time without inference.
2. Screenshot -> Translate: record first-request total time; a 25-second self-timeout should no longer occur.
3. Confirm a real timeout message says whether it happened during send or while waiting for Gemini.
4. Confirm translation completes and Original/Translation switching remains local after the first result.
5. Only after the above passes, proceed to the shared capture-revision cache and OCR side-panel isolation work.
