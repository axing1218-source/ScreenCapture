# Third-Party Notices

StarCap is independently maintained by **阿星**. The project contains modified code, inherited code, or implementation ideas from third-party open-source projects.

This file exists to preserve attribution and license information required by those projects. Third-party attribution does **not** imply that upstream authors endorse StarCap, its branding, or later modifications.

## xland/ScreenCapture

StarCap was originally developed from the public open-source project `xland/ScreenCapture` and has since received substantial product, UI, OCR, translation, clipboard, and workflow changes.

- Upstream repository: `https://github.com/xland/ScreenCapture`
- Upstream license: MIT
- Historical Git commit authorship is retained.

The upstream repository's license notice must remain with copies or substantial portions of the inherited software. The root `LICENSE` file is therefore preserved while the licensing inventory is being reconciled for StarCap.

## Ling

The current native UI/build line depends on the Ling GUI framework.

- Repository: `https://github.com/xland/Ling`

The dependency and its license remain subject to the terms published by that project.

## ClipboardManager

The clipboard-manager work in the pre-StarCap code line adapted interaction and visual ideas from the public project `ZiuChen/ClipboardManager`.

- Repository: `https://github.com/ZiuChen/ClipboardManager`
- License: Apache License 2.0

StarCap's clipboard implementation is a native Win32/C++ implementation rather than the original Vue/JavaScript runtime.

## Other bundled components

The source tree also contains or references additional third-party components, including QR, GIF, media, and Windows-related libraries. A complete per-component licensing inventory is part of the v0.9.7 open-source cleanup.

When a component's license requires redistribution of a copyright notice, license text, or NOTICE file, StarCap will preserve it.
