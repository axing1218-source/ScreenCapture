# StarCap Development Documents

当前 `docs/` 目录用于保存 StarCap 的开发说明与历史设计记录。

## 仓库身份

- Repository: `axing1218-source/StarCap`
- Default branch: `main`
- Product: `StarCap`
- Repository status: independent repository (`fork: false`)

StarCap 已完成仓库重命名，并已从原 `xland/ScreenCapture` fork network 中独立。新的源码、文档、CI、Issue 和 Pull Request 均应以独立的 `axing1218-source/StarCap` 仓库为准。

## Git 历史策略

正式 `main` 从 StarCap 独立产品状态重新建立根提交，避免默认开发线继续把上游项目作者统计为 StarCap 当前贡献者。

- 当前干净主线根提交：`Initial StarCap release`。
- `archive/full-history-pre-clean-main`：保存历史重建前的完整 Git 提交链，包含 StarCap、WeShot 和上游 ScreenCapture 的全部可追溯开发历史。
- `archive/legacy-main-pre-starcap`：保存切换默认代码线前的旧 `main` 快照。
- 许可证、第三方 notices、AUTHORS 与 `docs/history/` 均继续保留；重建默认分支历史不改变任何原始版权或许可证义务。

新的功能开发只在干净的 `main` 上继续。需要追溯旧实现、提交作者或历史差异时，应使用 archive 分支，而不是把旧提交重新合并回 `main`。

## Release 与自动更新

StarCap 的用户更新通道以本仓库的 **GitHub Latest Release** 为唯一来源。

- `Src/Version.h` 与 `Doc/version.json` 必须保持同一版本号。
- 正式 Release 固定包含 `StarCap.exe`、`version.json` 和 `SHA256SUMS.txt`。
- 客户端只读取 `axing1218-source/StarCap` 的 Latest Release；主分支上尚未发布的提交不会触发用户更新提示。
- 客户端下载新版后会再次核对 `StarCap.exe` 自身的嵌入版本号，版本不一致时不会覆盖当前程序。
- `.github/workflows/starcap-release.yml` 会在 `main` 上检测当前版本是否已经存在正式 Release；同一版本只发布一次。

以后发布新版本时，应先同步更新 `Src/Version.h` 和 `Doc/version.json`，再合入 `main`。Release workflow 会为新的版本号构建并发布正式资产。

## 当前分支

- `main`：StarCap 当前默认开发线。
- `starcap-v0.9.7`：StarCap v0.9.7 对应的正式开发分支。
- `starcap-source-cleanup`：v0.9.7 独立化与源码清理过程中使用的工作分支。
- `archive/full-history-pre-clean-main`：历史重建前的完整提交链，只用于追溯。
- `archive/legacy-main-pre-starcap`：切换默认代码线前保存的旧 `main` 快照，只用于历史追溯。

当前功能开发应以 `main` 为准，不应从归档分支恢复旧产品命名或已淘汰的构建方式。

## 历史 PR 快照

在 StarCap 脱离原 ScreenCapture fork network 前，仓库中的 4 个历史 PR 元数据均已保存为 Markdown：

- [`history/pull-requests/PR-1-WeShot-v0.8.23-source-paragraph-layout.md`](history/pull-requests/PR-1-WeShot-v0.8.23-source-paragraph-layout.md)
- [`history/pull-requests/PR-2-WeShot-v0.8.23-build-trigger.md`](history/pull-requests/PR-2-WeShot-v0.8.23-build-trigger.md)
- [`history/pull-requests/PR-3-WeShot-v0.9.0-core-foundation.md`](history/pull-requests/PR-3-WeShot-v0.9.0-core-foundation.md)
- [`history/pull-requests/PR-4-WeShot-v0.9.1-clipboard-prototype.md`](history/pull-requests/PR-4-WeShot-v0.9.1-clipboard-prototype.md)

这些快照只用于保留开发过程信息，不是当前 StarCap 的实现规范。

## 历史文档

`history/` 中的文件来自 StarCap 更名前的 WeShot 开发阶段，以及 v0.9.0-v0.9.5 的阶段性设计工作。

这些文档被保留用于：

- 追踪功能设计与实现演进
- 理解 OCR、翻译、截图交互等功能的历史决策
- 保留项目开发记录和 Git 历史

它们**不是当前 StarCap v0.9.7 的规范文档**。其中出现的 `WeShot`、旧模块名、旧版本号、旧路径或旧 UI 描述都应视为历史上下文，不应直接用于新的代码或产品命名。

当前项目名称、构建入口和品牌信息请以根目录 [`ReadMe.md`](../ReadMe.md)、`StarCap.slnx`、`Src/StarCap.vcxproj` 以及当前源码为准。

---

The files under `history/` are preserved pre-StarCap design and implementation records. They are historical references, not current StarCap v0.9.7 specifications. Legacy names found there should not be reintroduced into active product or source naming.
