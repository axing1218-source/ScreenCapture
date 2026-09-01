# StarCap

<p align="center">
  <img src="../Assets/Branding/starcap-logo-master-1024.png" alt="StarCap" width="180">
</p>

<p align="center">
  <strong>A lightweight, modern screenshot and visual productivity tool for Windows.</strong>
</p>

<p align="center">
  <a href="../ReadMe.md">简体中文</a> | English
</p>

## About StarCap

**StarCap** is an open-source Windows screenshot and visual productivity tool. It starts with fast screen capture and brings annotation, scrolling capture, screen recording, OCR, AI-assisted translation, QR recognition, and clipboard workflows into one application.

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

> v0.9.7 is currently in the project-independence transition. Some internal project filenames and compatibility paths may temporarily retain historical names and will be migrated incrementally.

## Building

StarCap is currently a Windows C++ project built with Visual Studio / MSBuild. The codebase still depends on third-party components such as Ling. Reproducible builds and dependency cleanup are part of the v0.9.7 roadmap.

## Open source and third-party software

StarCap is independently maintained, while parts of the codebase and implementation lineage come from other open-source projects. Copyright notices, license texts, and required attribution are preserved.

See:

- [`LICENSE`](../LICENSE)
- [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)

Preserved attribution does not imply endorsement of StarCap's later modifications, releases, or branding by upstream authors.

## Logo

The StarCap mark is a five-point star made of five separately colored points, divided by transparent separators.

Official logo and Windows icon assets are stored in:

[`Assets/Branding`](../Assets/Branding)

## Project status

`v0.9.7` is the first development version establishing StarCap as a distinct project identity. Current priorities are:

- Unify StarCap branding
- Remove legacy promotional and personal-contact material
- Organize licensing and third-party notices
- Unify application naming, resources, and build artifacts
- Complete GitHub project independence
- Continue development without regressing the existing v0.9.6 feature set

## Contributing

Issues, suggestions, and pull requests are welcome through GitHub.

---

**StarCap · 阿星**
