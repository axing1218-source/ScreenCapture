# StarCap Development Documents

当前 `docs/` 目录用于保存 StarCap 的开发说明与历史设计记录。

## 当前分支

- `main`：StarCap 当前默认开发线。
- `starcap-v0.9.7`：StarCap v0.9.7 对应的正式开发分支。
- `starcap-source-cleanup`：v0.9.7 独立化与源码清理过程中使用的工作分支。
- `archive/legacy-main-pre-starcap`：切换默认代码线前保存的旧 `main` 快照，仅用于历史追溯。

当前功能开发应以 `main` / `starcap-v0.9.7` 为准，不应从归档分支恢复旧产品命名或已淘汰的构建方式。

## 历史 PR 快照

在 StarCap 脱离原 ScreenCapture fork network 前，以下已关闭的历史 PR 元数据已保存为仓库内 Markdown：

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
