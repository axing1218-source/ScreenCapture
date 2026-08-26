# WeShot next implementation checkpoints

This supplements `WeShot-implementation-plan.md` and turns the current v0.8.4 direction into small, reviewable changes.

## Current code audit

Gate A is complete: the validated Gemini runtime behavior now lives in production source and the normal Windows build succeeds without CI rewriting `GeminiClient.h`.

Verified baseline:

- `gemini-3.7-flash` uses supported low thinking in source;
- image requests use medium vision resolution;
- inference keeps a realistic receive ceiling and separates send vs wait-response failures;
- Settings connection testing uses the model metadata endpoint instead of generating `OK`;
- CI checks source behavior rather than defining it;
- the latest pure-source Windows x64 build succeeds.

The next risk is no longer Gemini transport. It is duplicated capture/OCR/translation ownership between the screenshot toolbar path and OCR result-window path. Gate B therefore starts by introducing one capture-scoped session before adding more UI or rendering changes.

## Code ownership map

Keep each concern in one place so WeChat-like interaction does not become coupled to Gemini transport details.

- `Src/GeminiClient.h`: Gemini REST transport, PNG payload construction, structured OCR/translation parsing, model metadata connection test, request-stage timing. It must not own capture UI state.
- `Src/Setting.cpp` / `Src/Setting.h`: API key, model selection, connection-test presentation.
- capture/toolbar code under `Src/Win`: owns active selection lifecycle and presentation of Original/Translation controls, but should not own a second OCR/translation cache.
- OCR result-window code under `Src/Win`: owns side-panel hit testing, focus, text selection/copy, Original/Translation presentation and Retry, but should not own canonical OCR/translation data.
- a capture-scoped `CaptureSession` object becomes the single owner of the selected source snapshot, revision, derived OCR/translation state and in-flight request identity.

## Migration gates

### Gate A — Gemini source parity — complete

Exit condition is satisfied: local and CI builds use the same Gemini production behavior and the workflow no longer needs to rewrite request logic.

Do not reopen proxy/model configuration work unless a reproducible failure provides new evidence. The next binary should change capture state only.

### Gate B — shared capture revision/session

Introduce one capture-scoped state object shared by toolbar Translate and the OCR side panel.

#### B1 — canonical snapshot first

The first Gate B patch should be intentionally small: unify ownership of the selected pixels and revision before moving request logic.

Suggested minimal shape:

```cpp
struct CaptureSnapshot {
    uint64_t revision{0};
    int width{0};
    int height{0};
    std::vector<BYTE> bgra;
};

struct CaptureSession {
    CaptureSnapshot source;
    // Derived fields are added in B2/B3 after snapshot ownership is proven stable.
};
```

Rules for B1:

- one function creates/replaces the canonical snapshot from the active selection;
- moving/resizing/replacing source pixels increments `revision` exactly once;
- opening/closing the OCR panel, toggling Original/Translation, copying text, moving the floating toolbar, or changing focus does **not** increment revision;
- toolbar and OCR window receive the same session/snapshot reference instead of each copying the selection independently;
- the canonical BGRA buffer is immutable for the lifetime of a revision; translated rendering always uses a separate overlay/bitmap;
- long-screenshot completion replaces the snapshot and increments revision once, rather than treating every intermediate stitched frame as a new translation target.

B1 deliberately does **not** deduplicate Gemini calls yet. Its exit condition is simply that both UI paths can prove they are looking at the same revision and same source dimensions/pixels.

#### B2 — shared derived cache

After B1 is stable, add derived data to `CaptureSession`:

```cpp
struct OcrCacheEntry {
    uint64_t revision{0};
    std::wstring model;
    std::wstring text;
    std::vector<GeminiClient::OcrBlock> blocks;
};

struct TranslationCacheEntry {
    uint64_t revision{0};
    std::wstring model;
    std::wstring targetLanguage;
    std::wstring translatedText;
    std::vector<GeminiClient::TranslationBlock> blocks;
};
```

Rules:

- cache keys include revision plus model; translation also includes target language;
- source snapshot is never invalidated by model/language changes; only derived entries are;
- closing/reopening the OCR panel reuses cached entries;
- Original/Translation toggles only change presentation state and never mutate cache identity.

#### B3 — one in-flight operation per equivalent request

Only after B1/B2, add request sharing. Use a small descriptor rather than a second networking layer:

```cpp
enum class CaptureRequestKind { OcrImage, TranslateImage, TranslateText };

struct InFlightCaptureRequest {
    uint64_t requestId{0};
    uint64_t revision{0};
    CaptureRequestKind kind{};
    std::wstring model;
    std::wstring targetLanguage;
};
```

Request policy:

- toolbar Translate on a fresh selection starts one combined image OCR+translation request;
- OCR-first then Translate reuses valid OCR blocks and sends text-only translation when possible;
- if toolbar and OCR panel request equivalent work for the same revision/model/language while a request is already running, the second UI consumer attaches to the existing operation instead of starting another Gemini call;
- completion updates `CaptureSession` only when both `requestId` and `revision` still match;
- a revision change invalidates the active derived view immediately; an older callback may finish but becomes a silent no-op;
- Retry creates a new request id for the current revision without destroying valid original pixels or cached local OCR.

Avoid storing UI pointers in the session. Notify views through weak callbacks/event dispatch so closing the OCR panel while a request is running cannot create a dangling callback.

**Gate B exit condition:** one canonical selection snapshot, one revision counter, one derived cache, at most one equivalent in-flight Gemini operation, and stale results cannot paint onto a newer selection.

### Gate C — OCR side-panel isolation

Treat the panel as a real input surface layered above capture hit testing.

- mouse down/move/up for text selection stays captured by the panel until the gesture ends;
- Ctrl+C copies panel text only while panel/text control owns focus;
- clicking Original, Translation, Retry or Close cannot move/resize the selection;
- opening/closing the panel does not increment capture revision;
- closing restores focus to capture without cancelling it;
- panel width is stable and text wraps inside the panel.

Additional acceptance rule after Gate B: panel creation/destruction must not own or destroy `CaptureSession`; the active capture surface owns the session lifecycle.

**Exit condition:** repeated text selection/copy and tab switching never changes the capture rectangle on Windows 10 22H2 at 100%, 125% and 150% display scaling.

### Gate D — WeChat-like local translation rendering R2

Keep Gemini image generation out of the pipeline. Use returned boxes and render locally.

For each text block:

1. expand the box by a small clamped margin;
2. sample narrow border strips rather than the glyph-filled interior;
3. reject strong gradients/high disagreement instead of painting an obviously wrong flat rectangle;
4. use median/trimmed-mean background estimation;
5. estimate foreground color from pixels that differ from the background;
6. measure DirectWrite layout and shrink/wrap until translated text fits;
7. preserve likely source alignment (left/center/right) instead of always centering.

Do not add inpainting until R2 is stable; gradient/textured blocks that fail confidence should retain source background and be marked for R3.

**Exit condition:** plain UI text and chat bubbles no longer show obvious rectangular color contamination from the source glyphs.

## Latency budget

For a normal desktop screenshot, record locally:

- PNG encode;
- request build;
- send;
- wait for first response;
- body read;
- JSON parse;
- local render;
- total.

Optimization priority is `waitResponseMs` first, then image payload size/encode. Do not trade correctness for an artificial short timeout. After TranslationReady, Original/Translation switching should be local and effectively immediate.

Gate B should reduce avoidable latency/cost by preventing duplicate image uploads; it should not attempt to make Gemini itself faster.

## Long screenshot checkpoint

Do not send the full-resolution stitched image blindly. Keep original pixels for save/copy, but create a network copy and tile vertically when needed. Tiles overlap enough to catch text crossing boundaries; returned boxes are remapped into global coordinates and duplicate lines are merged by overlap/text similarity.

This comes only after Gates B–D, so long-capture complexity cannot hide basic request/cache bugs.

## Test order for the next Gate B binary

1. Settings → Test connection: quick regression check only; Gate B should not change Gemini transport.
2. Screenshot → Translate: record first total time and verify translation still works.
3. Toggle Original/Translation repeatedly: confirm zero additional network requests.
4. Start Translate, then move/resize selection before response: old result must be discarded.
5. Open OCR panel before/after translation: verify it shows the same revision and reuses the same cached source/result.
6. Close/reopen OCR panel: no new OCR/translation request for an unchanged revision.
7. Trigger toolbar Translate and panel Translate close together: confirm one equivalent request, not two.
8. Repeat panel text-selection/copy tests on Windows 10 22H2 at 125% scaling before starting R2 rendering.

Only after these pass should the next binary add R2 rendering changes.