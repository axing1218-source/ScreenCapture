# WeShot implementation plan

## Goal
Turn the current ScreenCapture fork into a portable WeChat-like screenshot utility with built-in OCR and Gemini translation while preserving the existing screenshot, long-capture, recording, QR, save, and clipboard flows.

## Interaction target
- Keep the current full-screen capture surface, window snapping, drag-to-select, adjustable selection, magnifier, double-click copy, Enter copy, and Esc cancel behavior.
- Preserve the existing floating toolbar near the selection.
- Long screenshot should enter capture mode immediately. Manual wheel/trackpad scrolling is the default; automatic scrolling remains available as an explicit toolbar action.
- OCR must stay in-process and must not close or resize the screenshot selection.
- The OCR/result panel is docked directly beside the selection, aligned to the selection top edge and matching its height when screen space permits; fall back inside the work area only when necessary.
- Annotation interaction should converge toward Snipaste-style behavior: predictable tool toggling, secondary controls for width/color/fill/opacity, clean undo/redo, and no accidental selection resizing while interacting with tools or side panels.

## Current OCR state
- Integrated local Windows OCR is implemented and keeps the capture session alive.
- OCR text appears in an in-process Ling side panel and can be copied.
- Panel hit-testing is isolated from selection resize/drag logic so clicking Copy, selecting text, or closing the panel must not modify the capture rectangle.
- This local OCR path is useful as a fast/offline fallback, but Gemini remains the planned primary path for OCR + translation when an API key is configured.

## Gemini OCR + translation architecture
1. Capture the selected BGRA pixels from the current `WinCap` selection without altering the original screenshot buffer.
2. Encode the selection as PNG in memory with WIC.
3. Send the PNG directly to Gemini's multimodal API over HTTPS; no localhost bridge and no separate Baidu OCR dependency.
4. Ask Gemini for structured output containing faithful recognized text plus translation.
5. Parse the response into distinct OCR and translation fields.
6. Render both results in the existing docked Ling side panel with actions for Copy OCR, Copy translation, Retry, and Close.
7. If Gemini is unavailable or no key is configured, fall back to Windows OCR for recognition and clearly indicate that translation requires Gemini.

## Gemini configuration
- No localhost service and no machine-specific dependency.
- Read the API key from a user-editable config value, with environment-variable fallback (`GEMINI_API_KEY`).
- Never commit an API key to the repository.
- Keep endpoint/model selection behind a small client abstraction so model changes do not affect capture UI code.
- Network calls run off the UI thread and marshal results back through the Ling dispatcher queue.
- API-key entry belongs in Settings; the program should remain portable across Windows 10/11 machines.

## Long screenshot quality
- Preserve original captured pixels in the stitched result; do not upscale merely to make the file larger.
- Save losslessly as PNG.
- Preview scaling is independent of final image quality and may be lower resolution for responsiveness.
- If user testing shows blur in the saved PNG, investigate DPI virtualization, capture coordinates, and stitching boundaries before adding any artificial scaling.

## Failure handling
- Missing Gemini API key: keep local OCR available and show a clear translation configuration prompt.
- HTTP/network errors: keep the panel open and show Retry; never discard the capture.
- Invalid/empty model response: preserve any available local OCR text and show the Gemini error separately.
- Large selections: downscale only the network request image when necessary; never replace the original screenshot used for save/copy.
- OCR panel interaction must never propagate into capture-resize input handling.

## Implementation order
1. Windows x64 CI build and downloadable artifact. **Done.**
2. In-process OCR side panel replacing the external ImageReader flow. **Done (local Windows OCR baseline).**
3. OCR panel docking, full-height alignment, and input isolation. **Done; pending user smoke test.**
4. Long screenshot manual-first mode plus explicit Auto Scroll toolbar control. **Done; pending user smoke test.**
5. Configurable selection border thickness in Settings. **Done; pending user smoke test.**
6. Snipaste-style annotation refinement: tool behavior, line/brush feel, color, width, opacity/fill, mosaic, text, and selection/edit affordances. **In progress / next UI milestone.**
7. Add `GeminiClient`, in-memory PNG request encoding, structured OCR+translation response, API-key Settings UI, and Windows-OCR fallback. **Next integration milestone.**
8. Windows 10 22H2 smoke-test pass covering manual/auto long capture, OCR panel, copy interaction, annotation, and Gemini translation.

## Testable baseline
The `weshot-v0.1` branch currently has a successful Windows x64 build containing the manual-first long screenshot flow, docked OCR panel fixes, copy/input isolation, and configurable selection border thickness. This is the current v0.2-style test baseline. Gemini translation is not yet integrated into this build.
