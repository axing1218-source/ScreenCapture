# WeShot session / OCR panel state machine

This document turns the implementation plan into an explicit state contract for the first code patch and the later Gemini translation patch.

## Principle

`CaptureSession` is the only source of truth. The OCR panel and translation overlay are views of the current session; they must not maintain independent business state.

A session represents exactly one committed screenshot selection snapshot. Its `revision` never changes after creation. A new OCR snapshot creates a new session/revision.

## Session states

Use two independent async states plus one snapshot validity flag:

```cpp
enum class AsyncState { Idle, Running, Ready, Failed, Cancelled };

struct CaptureSession {
    uint64_t revision{};
    bool stale{false};

    AsyncState ocrState{AsyncState::Idle};
    AsyncState translationState{AsyncState::Idle};
    // image / OCR / translation payloads...
};
```

`stale` means the selected rectangle no longer matches the pixels stored in the session. A stale session may remain visible in the panel for user context, but it cannot be translated or painted as an overlay.

## OCR button transitions

### No session

`OCR click -> getCutPixels once -> create revision -> ocrState=Running -> show panel Loading -> dispatch OCR`

### OCR Running, same selection/session

`OCR click -> focus/show existing panel`

Do not call `getCutPixels()` again and do not start a duplicate OCR worker.

### OCR Ready, same selection/session

`OCR click -> focus/show existing panel with current text`

No duplicate OCR.

### OCR Failed, same selection/session

A new OCR click is an explicit retry. Create a new snapshot/revision rather than mutating the failed request in place. This keeps every async request tied to one immutable revision and simplifies late-callback rejection.

### Session stale because selection changed

`OCR click -> getCutPixels once -> create new revision -> replace canonical session -> show Loading -> dispatch OCR`

The old session may be discarded once the panel has switched to the new revision.

## Selection commit transition

Only a committed physical-pixel rectangle change invalidates the current snapshot.

`Adjust mouse down -> remember integerized start rect`

`Adjust mouse move -> existing CutMask behavior only`

`Adjust mouse up -> compare integerized final rect`

If changed and a session exists:

1. set `session.stale=true`;
2. cancel/abandon active OCR request for that revision;
3. cancel/abandon active Gemini request for that revision;
4. hide translation overlay immediately;
5. panel renders Stale for that same revision;
6. do not call `getCutPixels()` yet.

If the rectangle is unchanged, do nothing.

## Panel state derivation

The panel renders directly from the current session:

- no session: hidden or empty;
- `stale=true`: Stale (`Selection changed - run OCR again`), Copy may remain available for already-recognized text, Translate disabled;
- `ocrState=Running`: Loading;
- `ocrState=Failed`: OCR error + Retry;
- `ocrState=Ready`, translation Idle: recognized text + Copy + Translate;
- translation Running: recognized text + translating indicator, Translate disabled;
- translation Failed: recognized text + translation error + Retry Translate;
- translation Ready: recognized text + translated text.

The panel must ignore any notification whose revision does not equal the canonical `CaptureSession::revision`.

## Translation button transitions

Translation is legal only when all are true:

- current session exists;
- `stale == false`;
- `ocrState == Ready`;
- OCR blocks/plain text are non-empty;
- no translation request is already Running for this revision.

`Translate click -> translationState=Running -> Gemini request using stored OCR data`

Gemini must never call OCR or `getCutPixels()`.

Repeated Translate clicks while Running only keep/focus the panel; they do not create duplicate network requests.

A retry after Failed may reuse the same revision because the source OCR snapshot is immutable and still valid. Before retry, replace/cancel the old Gemini request context and transition Failed -> Running.

## Worker completion contract

Every OCR/Gemini worker captures immutable request input plus:

```cpp
{ captureLifetime, revision, requestId }
```

Worker completion posts result data to the existing UI dispatcher. The UI lambda publishes only if:

1. capture lifetime is still valid;
2. `WinCap` can still be resolved;
3. a current session exists;
4. `session.revision == completion.revision`;
5. the request ID still matches the active request context;
6. the session is not stale for results that require geometry/translation publication.

Otherwise the completion is a no-op.

Each request context has exactly one terminal transition: `Ready`, `Failed`, or `Cancelled`.

## Close transition

At the start of `WinCap::onClosed()`:

1. invalidate the lifetime token;
2. cancel OCR and Gemini request contexts;
3. detach/close OCR panel;
4. hide/clear translation overlay references;
5. clear the canonical session;
6. continue existing ScreenCapture teardown.

Late worker completions are permitted but cannot publish after lifetime validation fails.

## First-build acceptance matrix

The first testable build, before Gemini, should pass these cases:

| Case | Expected result |
| --- | --- |
| Click OCR once | Screenshot stays open; panel shows Loading then recognized text |
| Click OCR repeatedly while Running | One OCR request only; panel is focused |
| Click OCR after Ready | Existing result shown; no new OCR |
| Drag selection after Ready | Existing panel switches to Stale; overlay absent |
| Resize selection after Ready | Same stale behavior; no OCR during drag |
| Click OCR after selection change | One new snapshot/revision and one OCR request |
| Close screenshot during OCR | No crash; late callback does nothing |
| Mixed DPI monitors | OCR boxes remain aligned because stored geometry is selected-image physical pixels |

Gemini should be added only after this matrix passes, so screenshot/UI regressions can be separated from network/translation failures.
