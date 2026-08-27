# WeShot v0.8.24 execution checklist

This checklist turns the current transport/session/rendering plan into buildable, independently testable slices. The goal is to keep the v0.8.23 Windows OCR geometry baseline stable while moving toward WeChat-like in-place translation.

## Current source audit — 2026-08-27

The active `weshot-v0.8.23` branch still has the old Gemini transport behavior in `Src/GeminiClient.h`: Flash models select `thinkingLevel=minimal`, inference uses a 25 s receive timeout, send/receive failures collapse into one generic WinHTTP error, and Settings tests connectivity by running a small inference that asks Gemini to reply `OK`.

The branch itself is build-clean: Windows x64 CI run 160 completed successfully at commit `a2485f5`. Therefore Slice 1 should be treated as a transport-only source change against a known-good OCR/UI baseline; no screenshot media-resolution or OCR geometry changes should be mixed into it.

### Slice 1 implementation order

Apply the transport changes in this order so failures remain attributable:

1. change non-2.5 `thinkingLevel` to `low` only;
2. restore inference receive timeout to 60 s while leaving resolve/connect/send limits unchanged;
3. split `WinHttpSendRequest` and `WinHttpReceiveResponse` diagnostics and record elapsed ms for each stage;
4. replace Settings inference-based `OK` test with `GET /v1beta/models/{modelId}` using the same host, API-key header, and automatic-proxy policy;
5. compile and run the fixed regression sequence before changing image resolution or API family.

Do not introduce proxy overrides, `MEDIA_RESOLUTION_*`, Interactions API, capture-session work, or renderer changes in the same commit.

## Slice 1 — Gemini transport only

Change `Src/GeminiClient.h` and Settings connection-test behavior only.

- Keep `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY` for both test and inference.
- Non-2.5 models use `thinkingLevel=low`; keep the existing 2.5 `thinkingBudget=0` compatibility path.
- Real inference timeouts: resolve ~8 s, connect ~8 s, send ~15 s, receive 60 s.
- Report send and wait-response failures separately with elapsed milliseconds.
- Replace inference-based connection test with `GET /v1beta/models/{modelId}` using the same host, API-key header, and routing policy.
- Do not change screenshot media resolution in this slice.
- Do not migrate `generateContent` to Interactions in this slice.

### Build gate
A build is testable only if:

1. Windows x64 CI compiles without a workflow-time source patch.
2. Existing local Windows OCR still opens and returns v0.8.23 geometry.
3. Settings connection test can distinguish invalid-key HTTP errors from WinHTTP send/wait failures.
4. Screenshot translation can remain pending beyond 25 s without being killed by the old timeout.

## Slice 2 — capture-scoped session

Add one session owned by the active capture window, not a process-global singleton.

Minimum state:

- monotonic `revision`;
- immutable BGRA snapshot, dimensions, and absolute virtual-screen origin;
- OCR blocks/paragraphs for that revision;
- translation result for that revision;
- translated overlay cache;
- request state and in-flight request handle;
- lifetime token for async callbacks.

### Revision rules

- Opening/closing OCR does not change revision.
- Original/Translated toggles do not change revision.
- Dragging a selection hides translated overlay immediately but does not increment repeatedly.
- Mouse-up commits one new revision only when the selection rectangle actually changed.
- Any async result is accepted only when both lifetime token and revision still match.

### Build gate
The slice is testable when starting translation and then resizing/moving the capture never allows the old result to paint on the new rectangle.

## Slice 3 — request reuse

Toolbar Translate and OCR-side-panel Translate must use the same session.

- Cache completed translation by at least `(revision, targetLanguage, requestKind)`.
- Concurrent callers for the same key join the same in-flight request.
- If reliable local OCR exists, prefer text-only `translateOcrBlocks()` instead of uploading the screenshot again.
- Reopening OCR for the same revision reuses OCR/translation cache.
- Original/Translated toggles are always zero-network operations.

### Build gate
Trigger toolbar Translate and side-panel Translate almost simultaneously. Exactly one Gemini request should be issued for the same revision/key.

## Slice 4 — OCR side-panel isolation

The side panel must behave like a real text surface and never accidentally arm screenshot adjustment.

- Capture mouse-down through mouse-up while selecting text.
- Consume wheel scrolling when pointer is over the panel.
- `Ctrl+C` copies panel selection only.
- Local text-tab switching never changes revision or starts Gemini.
- Panel focus/selection handles `Esc` locally first; capture cancel remains the fallback.
- No panel gesture reaches `CutMask::startAdjust()`.

### Build gate
Drag-select, scroll, copy, and switch Original/Translated inside the panel while watching the screenshot rectangle. The rectangle must not move or resize.

## Slice 5 — WeChat-like local translated image

Gemini returns text/geometry only. WeShot owns rendering.

First-pass R2 pipeline:

1. expand detected text rectangle slightly;
2. sample a thin ring outside the glyph region;
3. estimate background using a robust median/cluster rather than averaging glyph-filled pixels;
4. cover/repair only the text region;
5. estimate foreground contrast from the original neighborhood;
6. fit translated text with DirectWrite using original width/alignment and a target line count;
7. shrink font only on measured overflow;
8. cache the completed translated overlay per revision.

For photographic/complex backgrounds, use a conservative local soft fill/blur before attempting stronger inpainting. Never modify pixels outside detected text regions in the first R2 pass.

### Build gate
On one flat UI screenshot and one photographic screenshot:

- non-text content remains unchanged;
- translated boxes align with the source regions;
- Original/Translated toggling is instant and performs no request;
- small text remains legible without horizontal stretching.

## Performance log contract

Record debug timings separately for:

- snapshot/PNG encode;
- request/base64 construction;
- WinHTTP send;
- wait-response/model inference;
- body read/JSON parse;
- local overlay render.

User-facing UI should show only a compact total duration. This prevents network/model latency from being confused with local rendering cost.

## Fixed regression sequence

Use the same small-text, mixed Chinese/English screenshot for every build:

1. Settings → Test connection; record model + elapsed ms.
2. Screenshot → Translate; record total time and stage failure if any.
3. Toggle Original/Translated repeatedly; verify zero network calls.
4. Open OCR; verify v0.8.23 reading order/boxes remain stable.
5. Use an invalid API key; verify explicit API error while local OCR remains usable.
6. Translate, then resize before completion; verify stale result is discarded.
7. Trigger toolbar and side-panel Translate together; verify request dedupe.
8. Drag/copy/scroll text in OCR panel; verify selection rectangle remains untouched.
9. Test mixed-DPI and negative-coordinate multi-monitor placement.
10. Only after transport/session gates pass, A/B default vs Medium vs High image resolution.
11. Only after that, A/B `generateContent` vs Interactions API with all other variables fixed.

## Slice ownership map and diagnostics contract

To keep the WeChat-like interaction path debuggable, each slice now has a single owner and a strict error boundary:

- `GeminiClient.h` owns HTTP transport, model request construction, response parsing, and transport timings only. It must not know about screenshot-window lifetime, OCR panel focus, or translated-overlay visibility.
- `WinCap` owns capture identity: current selection rectangle, immutable snapshot, virtual-screen origin, and monotonic `revision`.
- The OCR result window owns text selection, scrolling, copy, and Original/Translated text presentation, but it never owns capture identity.
- The translated-image renderer owns background repair, text fitting, and the cached overlay bitmap; it never starts network requests.

Transport errors should be classified before they reach UI:

1. `send_failed` — `WinHttpSendRequest` failed before a response could be awaited;
2. `wait_failed` — request was sent but `WinHttpReceiveResponse` failed or timed out;
3. `http_error` — Google returned a non-2xx status such as invalid key/model;
4. `parse_error` — transport succeeded but the response body could not be parsed into the expected schema;
5. `stale_result` — response was valid but capture lifetime/revision no longer matches, so it is silently discarded rather than shown as a user-facing error.

For the OCR side panel, mouse input is considered panel-owned from the initial button-down until the matching button-up. Once a gesture begins inside the panel, moving outside the panel during text selection must still not fall through to `CutMask::startAdjust()`. This mirrors normal desktop text-surface behavior and removes a subtle selection-resize race.

For WeChat-like translation rendering, the first production pass should intentionally prefer a slightly imperfect local background fill over destructive inpainting. R2 should modify only detected text regions; stronger photo inpainting is a later opt-in refinement after flat UI screenshots are stable.

## Slice 1 transport result contract

The current `HttpResult` only carries `status`, `body`, and a formatted error string. Before changing UI messages, Slice 1 should first make the transport result machine-readable so Settings and screenshot translation consume the same diagnostics instead of parsing text.

Recommended minimal shape:

- `TransportErrorKind errorKind` with values `none`, `send_failed`, `wait_failed`, `http_error`, `read_failed`, `parse_error`;
- `DWORD win32Error` for WinHTTP failures;
- `DWORD status` for HTTP status;
- `ULONGLONG sendMs` and `waitMs`;
- response `body` unchanged;
- optional human-readable `error` produced only at the UI boundary.

`postGenerate()` and the lightweight model-info connection test should share one small request helper for WinHTTP session creation, API-key headers, timeout policy, send/wait timing, status extraction, and body reading. The helper may accept method/path/body and a receive-timeout argument; it must not know whether the caller is OCR, translation, or Settings.

This avoids two likely regressions:

1. Settings connection test accidentally using a different proxy/header/timeout path than real inference;
2. later Interactions A/B work duplicating another ad-hoc WinHTTP implementation.

For body reads, a `WinHttpReadData` failure after a successful response must be classified separately from model wait timeout. A partial JSON body must never fall through as a generic parse error without preserving the original Win32 read failure.

### Slice 1 acceptance logging

A debug build should emit one compact line per request, for example conceptually:

`gemini method=POST model=... status=200 send=12ms wait=1840ms read=3ms total=1860ms error=none`

Do not log API keys, request bodies, screenshot base64, OCR text, translated text, or file-identifying data. This keeps timing diagnostics useful without turning the screenshot tool into a content logger.

## Priority rule

Do not combine transport, API migration, image-resolution tuning, session/cache ownership, and local rendering in one commit. Each slice must produce a separately attributable Windows x64 build before the next slice starts.
