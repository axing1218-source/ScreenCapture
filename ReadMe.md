# StarCap

<p align="center">
  <img src="Assets/Branding/starcap-logo.svg" alt="StarCap" width="180">
</p>

<p align="center">
  <strong>轻量、现代的 Windows 截图与视觉效率工具</strong>
</p>

<p align="center">
  中文 | <a href="./Doc/ReadMe.en-US.md">English</a>
</p>

## 关于 StarCap

**StarCap** 是一个面向 Windows 的开源截图与视觉效率工具。它以快速截图为核心，并整合标注、滚动长截图、录屏、OCR、AI 翻译、二维码识别和剪贴板管理等能力。

StarCap 从 **v0.9.7** 开始采用新的项目名称、品牌与独立维护路线。

- 项目：**StarCap**
- 作者 / 维护者：**阿星**
- 当前开发版本：**v0.9.7**
- 平台：Windows
- 联系方式：GitHub `axing1218-source`

## 功能

- 区域截图与截图标注
- 矩形、圆形、箭头、线条、文本、编号、马赛克与橡皮擦
- 钉图 / 图像置顶
- 滚动长截图
- GIF / MP4 屏幕录制
- OCR 文字识别
- Gemini 辅助翻译
- 二维码识别
- RGB / HEX / CMYK 取色
- 剪贴板历史与快速查看
- 撤销、重做、保存与复制
- 多语言支持
- 支持命令行直接进入指定功能

## 快捷启动参数

```text
StarCap.exe --auto-quit=true
StarCap.exe --enter=pin
StarCap.exe --enter=long
StarCap.exe --enter=video
StarCap.exe --enter=ocr
StarCap.exe --enter=qr
StarCap.exe --enter=tray
```

## 工程与构建

StarCap 当前为 Windows C++20 项目，使用 Visual Studio / MSBuild 构建。

正式工程入口已经统一为：

- 解决方案：`StarCap.slnx`
- Visual Studio 项目：`Src/StarCap.vcxproj`
- Release 可执行文件：`StarCap.exe`

示例：

```text
msbuild Src\StarCap.vcxproj /m /p:Configuration=Release /p:Platform=x64
```

项目目前仍依赖 Ling 等第三方组件。GitHub Actions 会从仓库源码直接构建 StarCap，并额外检查活跃源码中是否重新出现已迁移的 WeShot / ScreenCapture 工程标识。

为了旧版本升级兼容，个别历史数据、配置或迁移标识可能仍会保留；这些兼容标识不再作为 StarCap 的当前产品或工程命名使用。

## 开源与第三方软件

StarCap 是独立维护的开源项目，同时包含或改造自其他开源项目的代码与设计实现。法律和许可证要求保留的原始版权、许可证与第三方声明会继续保留。

详情请查看：

- [`LICENSE`](./LICENSE)
- [`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md)
- [`AUTHORS.md`](./AUTHORS.md)

保留这些声明不代表原上游作者对 StarCap 的后续修改、发布或品牌提供背书。

## Logo 与 Windows 图标

StarCap 的标志是一枚由五个不同颜色星角组成的五角星，各色区域之间使用透明分隔。

品牌母版为：

- `Assets/Branding/starcap-logo.svg`

Windows 主图标与托盘图标分别位于：

- `Src/Res/logo.ico`
- `Src/Res/tray.ico`

图标生成脚本位于 `tools/`，避免再次依赖手工导出的不一致或损坏二进制品牌资源。

## 项目状态

`v0.9.7` 是 StarCap 品牌与开源项目身份正式建立的第一个开发版本。目前已经完成的独立化工作包括：

- StarCap 品牌、版本信息与运行时名称统一
- OCR、翻译、诊断和文本布局等内部模块迁移到 `StarCap*` 命名
- Visual Studio 工程迁移到 `StarCap.slnx` / `StarCap.vcxproj`
- Windows 主图标和托盘图标重新生成并验证
- CI 增加源码身份与最终 EXE 品牌检查
- 上游归属、作者和第三方声明分离整理

接下来的重点是继续完善第三方许可清单、历史开发文档归档，以及 GitHub 仓库层面的最终独立化。

## 贡献

欢迎通过 GitHub 提交 Issue、建议与 Pull Request。

---

**StarCap · 阿星**
