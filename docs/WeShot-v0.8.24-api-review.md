# WeShot v0.8.24 — Gemini API review and gate refinement

## Fresh API review
Reviewed the current Google Gemini documentation against `Src/GeminiClient.h` before implementing the transport gate.

### 1. Do not combine transport stabilization with an API-family migration
As of August 2026 Google documents the Interactions API as the default interface for new Gemini work, while `generateContent` remains supported but is considered legacy.

WeShot currently has substantial structured-output parsing and OCR/translation behavior built around `v1beta/models/{model}:generateContent`. Moving back to `/v1beta/interactions` in the same commit as timeout/error handling would make failures ambiguous.

Decision for v0.8.24:
- T1 keeps the existing `generateContent` request/response contract.
- T1 changes only transport diagnostics, timeout policy, connection testing, and known-invalid thinking configuration.
- Add a later T1.5 benchmark/migration gate for Interactions API after T1 is stable.
- T1.5 must preserve the same `OcrResult` / `TranslationResult` public interface so toolbar/OCR UI code does not care which Gemini endpoint is underneath.

### 2. `models.get` remains the correct connection test
Google's current Models API still exposes:

`GET https://generativelanguage.googleapis.com/v1beta/models/{model}`

with an empty body. This is the right Settings "Test connection" primitive because it verifies host reachability, API-key authorization, and model visibility without paying model inference latency.

Connection test acceptance:
- same host and WinHTTP automatic-routing policy as inference;
- `x-goog-api-key` header retained;
- report model id + total elapsed ms on 2xx;
- preserve Google's API error on non-2xx;
- distinguish send failure from wait-response failure.

### 3. Revisit medium image resolution as a performance choice, not a correctness rule
Current Gemini image guidance recommends high media resolution for most image-analysis tasks; medium uses fewer vision tokens and may be faster, but can reduce small-text OCR quality.

Therefore `MEDIA_RESOLUTION_MEDIUM` should not become an unconditional product invariant.

For v0.8.24:
- Keep `medium` as the low-latency candidate for ordinary desktop screenshots.
- Add an A/B test against `high` using the fixed v0.8.23 OCR test set (small UI labels, mixed Chinese/English, multiple font sizes).
- Prefer `medium` only if recognition/box quality is effectively unchanged.
- If small-text recall measurably drops, use `high` for image OCR/translation and recover speed through request reuse/text-only translation rather than lowering vision detail.
- Text-only `translateOcrBlocks()` never sets media resolution.

Suggested later adaptive policy:
- normal screenshot with already reliable local OCR -> text-only translation;
- image request with typical UI text -> medium initially;
- dense/small text or failed first OCR -> one high-resolution retry;
- never retry automatically if revision changed or the capture window is gone.

## Revised v0.8.24 order

### Gate T1 — deterministic transport
Implement in `GeminiClient.h` without changing endpoint family:
- `thinkingLevel=low` for non-2.5 models;
- 60 s receive/inference ceiling for real generation;
- separate send and wait-response timing/errors;
- `models.get` metadata connection test;
- preserve HTTP body/API diagnostics;
- no proxy redesign.

Do not change OCR geometry, side-panel routing, toolbar state, capture cache, or translated-image rendering in T1.

### Gate T1.1 — media-resolution A/B
After T1 compiles and runs:
- run the same screenshot set with medium and high;
- record total latency, OCR text recall, missed small labels, and box alignment;
- select the lowest resolution that does not materially regress OCR.

This prevents speed tuning from silently damaging the v0.8.23 OCR gains.

### Gate T1.5 — Interactions API spike
Only after T1/T1.1 are stable:
- implement Interactions behind the same internal Gemini client result types;
- compare first-response latency and structured-output reliability with `generateContent`;
- keep a compile-time or internal fallback during the experiment;
- migrate only if it is at least as reliable for screenshot OCR + structured translation.

### Gate T2 — shared capture session
One active capture owns one revision-scoped state object containing:
- immutable BGRA snapshot + dimensions + absolute virtual-screen origin;
- local OCR geometry/text;
- translation result;
- rendered translation overlay cache;
- in-flight request state;
- lifetime token.

Selection move/resize hides translated output immediately, but revision increments only once when the new rectangle is committed.

### Gate T3 — request reuse
Toolbar `译` and OCR side panel share the same capture session.

Priority:
1. if local OCR for current revision is reliable, call text-only `translateOcrBlocks()`;
2. otherwise send one image request;
3. callers with the same `(revision, targetLanguage, requestKind)` join the same in-flight request;
4. all callbacks validate lifetime token + revision before touching UI.

This is expected to improve real perceived speed more safely than reducing image resolution aggressively.

### Gate T4 — OCR side-panel isolation
Consume the complete panel gesture from mouse-down through mouse-up. Panel text selection, wheel scrolling, Ctrl+C, tab switching, and local focus must never leak into `CutMask::startAdjust()` or screenshot shortcuts.

Opening/closing the panel and Original/Translated switching do not change revision or trigger Gemini again.

### Gate R2 — WeChat-like local translated-image rendering
Gemini supplies text and geometry only. WeShot performs the visual reconstruction locally:
- sample a thin ring outside each source text region;
- estimate robust background color/texture from edge samples, not the glyph-filled center;
- conservatively cover/repair the source glyphs;
- estimate foreground contrast;
- fit translated text with DirectWrite, preserving box width/alignment and shrinking only on measured overflow;
- cache the finished overlay per revision;
- Original <-> Translated toggle is zero-network and immediate.

## Next implementation action
The next code change should be T1 only. In particular, do not yet switch `/generateContent` to `/interactions`, and do not lock medium resolution as final before the v0.8.23 OCR A/B test.

Once T1 builds, the first user test should be:
1. Settings -> Test connection: record model id and ms.
2. Screenshot -> Translate: record first response time.
3. If it fails, capture whether the message says send failure or wait-response failure.
4. After success, toggle Original/Translated several times and confirm no extra network request.
