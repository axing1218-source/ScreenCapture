# StarCap

<p align="center">
  <img src="../Assets/Branding/starcap-logo.svg" alt="StarCap" width="180">
</p>

<p align="center">
  <strong>A lightweight, modern screenshot and visual productivity tool for Windows.</strong>
</p>

<p align="center">
  <a href="../ReadMe.md">简体中文</a> | English
</p>

## About StarCap

**StarCap** is an open-source Windows screenshot and visual productivity tool. It combines fast screen capture with annotation, scrolling capture, screen recording, OCR, AI-assisted translation, QR recognition, and clipboard workflows.

Starting with **v0.9.7**, the project adopts the StarCap name, branding, and an independent maintenance roadmap.

- Project: **StarCap**
- Author / Maintainer: **阿星**
- Current development version: **v0.9.7**
- Platform: Windows
- Contact: GitHub `axing1218-source`

## Features

- Region capture and image annotation
- Rectangle, ellipse, arrow, line, text, numbering, mosaic, and eraser tools
- Pin-to-screen image window
- Scrolling / long screenshots
- GIF / MP4 screen recording
- OCR text recognition
- Gemini-assisted translation
- QR code recognition
- RGB / HEX / CMYK color picking
- Clipboard history and quick viewing
- Undo, redo, save, and copy
- Multi-language support
- Command-line entry modes

## Command-line entry points

```text
StarCap.exe --auto-quit=true
StarCap.exe --enter=pin
StarCap.exe --enter=long
StarCap.exe --enter=video
StarCap.exe --enter=ocr
StarCap.exe --enter=qr
StarCap.exe --enter=tray
```

## Project and build

StarCap is a Windows C++20 project built with Visual Studio / MSBuild.

The active project entry points are now fully named for StarCap:

- Solution: `StarCap.slnx`
- Visual Studio project: `Src/StarCap.vcxproj`
- Release executable: `StarCap.exe`

Example:

```text
msbuild Src\StarCap.vcxproj /m /p:Configuration=Release /p:Platform=x64
```

The project currently depends on third-party components such as Ling. GitHub Actions builds StarCap directly from repository source and also checks active source files for migrated WeShot / ScreenCapture project identifiers so legacy internal naming does not re-enter the current codebase.

A small number of historical data, configuration, or migration identifiers may remain for compatibility with upgrades from older versions. Those compatibility identifiers are not used as StarCap's current product or project naming.

## Open source and third-party software

StarCap is independently maintained, while parts of the codebase and implementation lineage come from other open-source projects. Copyright notices, license texts, and attribution required by those licenses are preserved.

See:

- [`LICENSE`](../LICENSE)
- [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)
- [`AUTHORS.md`](../AUTHORS.md)

Preserved attribution does not imply endorsement of StarCap's later modifications, releases, or branding by upstream authors.

## Logo and Windows icons

The StarCap mark is a five-point star made of five separately colored points, divided by transparent separators.

The canonical brand source is:

- `Assets/Branding/starcap-logo.svg`

The Windows application and tray icons are stored at:

- `Src/Res/logo.ico`
- `Src/Res/tray.ico`

Generation scripts live under `tools/`, avoiding dependence on manually exported binary branding assets that can become inconsistent or corrupted.

## Project status

`v0.9.7` is the first development version establishing StarCap as a distinct project identity. Completed independence work includes:

- Unified StarCap branding, version metadata, and runtime naming
- Migrated OCR, translation, diagnostics, and text-layout internals to `StarCap*` naming
- Migrated the Visual Studio project to `StarCap.slnx` / `StarCap.vcxproj`
- Rebuilt and validated the Windows application and tray icons
- Added CI checks for source identity and final executable branding
- Separated current authorship from upstream and third-party attribution

Next priorities are completing the third-party licensing inventory, archiving historical development documents, and finishing repository-level GitHub independence.

## Contributing

Issues, suggestions, and pull requests are welcome through GitHub.

---

**StarCap · 阿星**
