# Third-Party Notices

StarCap is independently maintained by **阿星**. Unless a file states otherwise, current StarCap project code is distributed under the repository's root `LICENSE`. This project also contains inherited, bundled, linked, or referenced third-party open-source work whose original notices and license requirements are preserved below.

Third-party attribution does **not** imply endorsement of StarCap, its branding, or later modifications by upstream authors.

## Upstream project lineage: xland/ScreenCapture

StarCap was developed from the public open-source project `xland/ScreenCapture` and has since received product, UI, OCR, translation, clipboard, branding, build, and workflow changes.

- Upstream repository: `https://github.com/xland/ScreenCapture`
- License: MIT
- The root `LICENSE` remains the license text distributed by the upstream repository.
- Historical Git commit authorship remains preserved in repository history.
- Inherited resources that remain byte-for-byte identical to upstream, such as `Src/Res/iconfont.ttf`, remain part of this upstream lineage.

Preserving upstream attribution does not make the upstream authors maintainers or endorsers of StarCap.

## quirc

StarCap bundles the quirc QR-code recognition library under `Src/quirc/`.

- Upstream repository: `https://github.com/dlbeer/quirc`
- Copyright: Copyright (C) 2010-2012 Daniel Beer <dlbeer@gmail.com>
- License: ISC
- License copy: `licenses/quirc-ISC.txt`

The bundled `Src/quirc/` tree corresponds to quirc's library source tree.

## cgif

StarCap bundles cgif source under `Src/Win/cgif/` for GIF encoding.

- Upstream repository: `https://github.com/dloebl/cgif`
- Copyright: Copyright (c) 2021-2026, Daniel Löbl <dloebl.2000@gmail.com>
- License: MIT
- License copy: `licenses/cgif-MIT.txt`

StarCap contains local integration changes around the encoder; the cgif license remains applicable to cgif-derived source.

## Ling

The Windows build currently clones and links the Ling GUI framework.

- Upstream repository: `https://github.com/xland/Ling`
- Copyright: Copyright (c) 2025 liulun
- License: MIT
- License copy: `licenses/Ling-MIT.txt`

The StarCap CI applies small build-compatibility patches to the checked-out Ling source before compilation. Those patches do not remove Ling's original license obligations.

## Yoga

Ling contains and builds Yoga layout-engine source that is linked into the Windows build.

- Upstream project: Yoga (`https://github.com/react/yoga`)
- Copyright: Copyright (c) Facebook, Inc. and its affiliates. / Meta Platforms, Inc. and affiliates
- License: MIT
- License copy: `licenses/Yoga-MIT.txt`

The Yoga source included by Ling identifies itself as MIT-licensed Meta/Facebook code.

## ClipboardManager reference

Pre-StarCap clipboard-manager work used interaction and visual ideas from the public `ZiuChen/ClipboardManager` project.

- Upstream repository: `https://github.com/ZiuChen/ClipboardManager`
- License: Apache License 2.0

StarCap's current clipboard implementation is native Win32/C++ rather than the original Vue/JavaScript runtime. This notice is retained to document implementation lineage and prior design reference.

## Microsoft platform APIs

StarCap uses Windows platform APIs and SDK components including Win32, Direct2D/Direct3D, Windows Imaging Component, Media Foundation, audio APIs, and related system libraries. These are platform dependencies rather than source code bundled in this repository and remain subject to Microsoft's applicable terms.

## License copies

Third-party license texts that correspond to bundled or linked open-source components are collected in [`licenses/`](./licenses/).

When a third-party component requires preservation of a copyright notice, license text, or NOTICE material, StarCap will retain it in source distributions and applicable binary distributions.
