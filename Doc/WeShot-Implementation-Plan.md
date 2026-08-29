# WeShot implementation plan

## Goal

Preserve xland/ScreenCapture's existing WeChat-like selection interaction and add an OCR/translation workflow with minimal changes to `WinCap`.

## Confirmed integration point

`WinCap::startOcr()` already calls `getCutPixels()` and receives a stable, tightly packed, top-down BGRA buffer for the current selection. WeShot should branch immediately after `getCutPixels()` succeeds instead of reopening or recapturing the screen.

The existing `Select -> Adjust` interaction, `CutMask` hit-testing, drag/move/8-direction resize, toolbar placement, magnifier and other capture behavior should remain unchanged.

## CaptureSession

Introduce a session object owned by `WinCap` while the capture window is alive.

Suggested minimum model:

```cpp
struct OcrBlock {
    D2D1_RECT_F rectPx;      // coordinates in capture-image physical pixels
    std::wstring text;
    float confidence{0.f};
};

enum class AsyncState { Idle, Running, Ready, Failed, Cancelled };

struct CaptureSession {
    uint64_t revision{0};
    int width{0};
    int height{0};
    std::vector<BYTE> bgra;

    AsyncState ocrState{AsyncState::Idle};
    std::vector<OcrBlock> blocks;
    std::wstring plainText;

    AsyncState translationState{AsyncState::Idle};
    std::vector<std::wstring> translatedBlocks;
    std::wstring translatedPlainText;
};
```

`revision` is the stale-result guard. Any operation that creates a new selected-image snapshot increments it. Every OCR/Gemini request captures the current revision; a completion may update UI/state only if its revision still matches.

Do not keep pointers into temporary pixel buffers. `CaptureSession::bgra` owns the selected image for the whole OCR/translation lifetime.

## Selection mutation and invalidation

The screenshot selection remains editable after OCR opens, so selection changes must explicitly invalidate OCR/translation state.

- Cache the cut rectangle (or a monotonically increasing selection generation) when the session snapshot is created.
- Any committed move/resize that changes the selected rectangle marks the current session stale immediately.
- Do not recapture continuously during mouse move. Keep xland's interaction hot path untouched; only invalidate on the committed selection change.
- A stale panel may remain visible, but it must switch to a `Selection changed - run OCR again` state and must not offer Translate for old blocks.
- The next OCR click performs exactly one new `getCutPixels()` snapshot, increments `revision`, cancels/abandons old OCR and Gemini work, and reuses the existing panel window.
- Translation overlay is hidden as soon as the source selection becomes stale.

This prevents old text/geometry from being painted over a newly moved or resized selection while avoiding expensive screenshot copies during drag.

## `startOcr()` target flow

1. Require `CapStage::Adjust` and a valid selection.
2. If the existing session still matches the current selection and OCR is `Running` or `Ready`, focus/show the existing side panel and do not duplicate work.
3. Otherwise call `getCutPixels()` exactly once.
4. Create/replace the session snapshot and increment `revision`.
5. Keep `WinCap` alive; do not call `close()`.
6. Open/show the OCR side panel in a Loading state.
7. Start one whole-image OCR job using the session BGRA data.
8. On matching-revision completion, store `blocks + plainText` once and refresh the side panel.
9. Translation uses those same OCR blocks; it must never launch a second OCR pass.

The old external `Util::openWithImageReader()` path can remain behind a compatibility/fallback flag until the new OCR path is proven stable.

## Async lifetime and UI-thread rule

Background OCR and Gemini work must never retain or dereference a raw `WinCap*` after dispatch.

- Worker requests capture only immutable request data plus `{window/session token, revision}`.
- Completion is posted/marshaled back to the capture UI thread before mutating `CaptureSession`, panel state, or invalidating paint.
- On the UI thread, completion first verifies that `WinCap` is still alive, the session token still matches, and `revision` is current.
- `WinCap::close()`/destruction cancels active request contexts, invalidates the lifetime token, and detaches/closes the OCR panel before capture resources are released.
- Late worker callbacks after close/cancel are allowed to arrive, but must become no-ops after token/revision validation.
- Exactly one terminal state may be published by each OCR/Gemini request.

This keeps HWND/D2D/UI objects single-threaded and removes use-after-free risk when a screenshot is dismissed while network/OCR work is still running.

## Side panel behavior

The OCR panel should be a separate top-level/tool window associated with `WinCap`, not a child hit-test layer over `CutMask`.

Requirements:

- panel scrolling/text selection only consumes mouse input inside the panel window;
- the screenshot host keeps all existing selection hit-testing behavior;
- closing the panel returns to the normal Adjust state without destroying the screenshot selection;
- while OCR is running, repeated OCR clicks should focus/show the existing panel rather than start duplicate work for the same revision;
- panel UI state is derived from `CaptureSession`, not kept as an independent source of truth;
- when the screenshot selection changes, panel content is marked stale rather than silently continuing to represent the previous rectangle.

Recommended first layout: original recognized text in one scrollable column with Copy and Translate actions. Translation can either replace a secondary column or appear beneath each source block after the basic panel is stable.

## Translation overlay

The overlay must consume the same `OcrBlock::rectPx` geometry produced by the single OCR pass.

- store geometry only in selected-image physical pixels;
- map to screenshot-window coordinates only at paint time using the current selection origin/scale;
- do not persist DPI-scaled rectangles;
- overlay should be non-interactive by default so it never steals selection drag/resize input;
- hide the overlay immediately when the session is invalidated by a selection change;
- if source and translation block counts do not match, fall back to panel-only translated text rather than guessing geometry.

## DPI and multi-monitor rule

There is one geometry truth: physical pixels of the selected image. OCR boxes and translation boxes remain in that coordinate system. Window/DPI conversion happens only at the rendering boundary.

Regression coverage should include 100%, 125%, 150% and 200% scaling plus a mixed-DPI two-monitor arrangement.

## Gemini request lifecycle

Translation transport must use a per-request context with a single terminal completion path.

The deadline covers the complete lifecycle: send request -> receive headers -> read body. A 60-second deadline actively cancels/closes the request handle. Late callbacks after cancellation are ignored. Exactly one terminal state (`Ready`, `Failed`, or `Cancelled`) may be published for a request.

All callbacks also carry the originating `CaptureSession::revision`; late results for an older revision are discarded before touching UI.

## Incremental implementation order

1. Add `CaptureSession` model, selection-generation tracking, and ownership to `WinCap` without changing visible behavior.
2. Add session invalidation on committed selection move/resize; no image copy on mouse-move hot path.
3. Change `startOcr()` to snapshot the selected BGRA image into the session and keep `WinCap` open.
4. Add a minimal OCR panel shell showing Loading/Failed/Ready/Stale states.
5. Wire one whole-image OCR pass and populate `blocks/plainText`, with UI-thread completion marshaling and lifetime/revision guards.
6. Add Copy and Translate actions.
7. Wire Gemini translation with full-request deadline/cancellation and the same lifetime/revision guards.
8. Add non-interactive translated-block overlay.
9. Run DPI/multi-monitor, selection-stale, window-close, duplicate-click, and cancellation regression tests.

## First testable milestone

The first useful build should stop before Gemini: click OCR -> screenshot remains open -> side panel appears -> one OCR pass fills recognized text -> selection drag/resize behavior still matches the original capture tool -> changing the selection marks the old OCR result stale instead of reusing it.

This milestone isolates screenshot/UI regressions from Gemini networking issues and verifies the lifetime/invalidation model before translation is added.

## Concrete `WinCap` hooks for the first code patch

The current mouse flow gives us a cleaner invalidation hook than observing every `CutMask::adjust()` call:

1. In `WinCap::onDown()` while `stage == CapStage::Adjust`, copy `cutMask->maskRect` into an `adjustStartRect` member immediately before `cutMask->startAdjust(pos)`.
2. Leave `WinCap::onMove()` unchanged apart from the existing visual/tool positioning work. In particular, do not increment revisions, cancel workers, copy pixels, or notify the OCR panel from this hot path.
3. In `WinCap::onUp()` while `stage == CapStage::Adjust`, compare the final `cutMask->maskRect` with `adjustStartRect`. Only if the physical-pixel rectangle actually changed, call a small `invalidateCaptureSessionForSelectionChange()` helper.
4. That helper marks the current snapshot stale, cancels/abandons OCR and Gemini request contexts, clears/hides translated overlay state, and tells the already-open panel to render Stale. It does **not** call `getCutPixels()` and does not create the next revision yet.
5. The next `startOcr()` call is the sole place that snapshots the new pixels and increments the revision.

Use exact integerized rectangle edges for the comparison because `maskRect` represents physical screenshot pixels in this window. This avoids stale invalidation from insignificant floating-point formatting differences.

### Recommended ownership surface

Keep the first patch small and explicit in `WinCap`:

```cpp
std::unique_ptr<CaptureSession> captureSession;
std::unique_ptr<WinOcrPanel> ocrPanel;
D2D1_RECT_F adjustStartRect{};
uint64_t nextCaptureRevision{1};
std::shared_ptr<CaptureLifetime> captureLifetime;
```

`CaptureLifetime` should contain only thread-safe cancellation/liveness primitives; it must not expose HWND, D2D objects, `CutMask`, or raw `WinCap*` to workers.

The panel should receive UI-thread notifications such as `showLoading(revision)`, `showReady(revision)`, `showFailed(revision, message)`, and `showStale(revision)`. These are view updates only; the canonical OCR/translation data stays in `CaptureSession`.

### UI completion dispatch

Use the application's existing UI dispatcher (`Ling::App::get()->dq.TryEnqueue(...)`) as the single completion bridge for both OCR and Gemini. A worker completion should enqueue a lambda containing immutable result data plus the lifetime token/revision; the lambda then resolves the live `WinCap` through the global owner only after checking the lifetime token, and finally re-checks `captureSession->revision` before publishing results.

This matches the existing delayed-disposal pattern already used by `WinCap::onClosed()` and avoids introducing a second window-message or dispatcher mechanism solely for WeShot.

### Close ordering for WeShot

At the beginning of `WinCap::onClosed()`:

1. invalidate the capture lifetime token;
2. cancel active OCR/Gemini contexts;
3. close/detach `ocrPanel`;
4. clear translation overlay/session UI references;
5. then continue the existing `capVideo`, `capLong`, `toolCap`, and deferred `winCap.reset()` path.

With that order, an in-flight worker may still finish its own cleanup, but it cannot publish into a window that is being destroyed.
