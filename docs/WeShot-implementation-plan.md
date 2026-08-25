# WeShot implementation plan

## Goal
Turn the current ScreenCapture fork into a portable WeChat-like screenshot utility with built-in OCR and Gemini translation while preserving the existing screenshot, long-capture, recording, QR, save, and clipboard flows.

## Interaction target
- Keep the current full-screen capture surface, window snapping, drag-to-select, adjustable selection, magnifier, double-click copy, Enter copy, and Esc cancel behavior.
- Preserve the existing floating toolbar near the selection.
- Add OCR/Translate as an in-process flow instead of launching the external ImageReader plugin.
- When OCR is invoked, keep the captured selection visible and open a compact results panel docked to the right side of the selection when space allows; otherwise dock left or inside the work area.

## OCR + translation architecture
1. Capture the selected BGRA pixels through the existing `WinCap::getCutPixels` path.
2. Encode the selection as PNG in memory with WIC.
3. Send the PNG directly to Gemini's multimodal API. This avoids a separate OCR dependency: Gemini is instructed to first transcribe visible text faithfully and then translate it.
4. Parse a small structured response with two sections: recognized text and translation.
5. Render the result in a native Ling side panel with selectable/copyable text and actions for Copy OCR, Copy translation, Retry, and Close.

## Gemini configuration
- No localhost service and no machine-specific dependency.
- Read the API key from a user-editable config value, with environment-variable fallback (`GEMINI_API_KEY`).
- Never commit an API key to the repository.
- Keep the Gemini endpoint/model isolated behind a small client class so model names can be changed without touching screenshot UI code.
- Network calls run off the UI thread and marshal results back through the Ling dispatcher queue.

## Failure handling
- Missing API key: show a clear configuration prompt rather than failing silently.
- HTTP/network errors: keep the panel open and show Retry.
- Invalid/empty model response: display the raw error text and do not close the screenshot automatically.
- Large selections: downscale only when necessary for request size, while keeping the original screenshot untouched for save/copy.

## Implementation order
1. Baseline Windows x64 CI build and downloadable test artifact. **Done.**
2. Add `GeminiClient` + in-memory PNG encoding and configuration lookup.
3. Add `WinOcrPanel` side panel using Ling `TextBox`/`Label`/`Button` controls.
4. Replace `WinCap::startOcr()` external-plugin launch with the integrated panel flow.
5. Add language/translation preferences and API-key UI to Settings.
6. Polish WeChat-like spacing, toolbar behavior, loading/error states, and keyboard shortcuts.
7. Build artifact and Windows 10 22H2 smoke-test pass.

## Current baseline
The `weshot-v0.1` branch now builds successfully in GitHub Actions on Windows x64 and produces a `WeShot-Windows-x64` artifact. This is the clean baseline before the integrated Gemini/OCR changes.
