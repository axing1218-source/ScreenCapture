# StarCap

<p align="center">
  <img src="Assets/Branding/starcap-logo-master-1024.png" alt="StarCap" width="180">
</p>

<p align="center">
  <strong>轻量、现代的 Windows 截图与视觉效率工具</strong>
</p>

<p align="center">
  中文 | <a href="./Doc/ReadMe.en-US.md">English</a>
</p>

## 关于 StarCap

**StarCap** 是一个面向 Windows 的开源截图与视觉效率工具。它以快速截图为核心，并整合标注、滚动长截图、录屏、OCR、AI 翻译、二维码识别和剪贴板管理等能力。

StarCap 从 **v0.9.7** 开始采用新的项目名称、品牌与维护路线。

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

> v0.9.7 正处于项目独立化阶段。部分内部工程文件名和兼容路径仍可能保留历史名称，后续版本会逐步完成迁移。

## 构建

StarCap 当前为 Windows C++ 项目，使用 Visual Studio / MSBuild 构建。项目仍依赖 Ling 等第三方组件；可重复构建环境与依赖整理是 v0.9.7 的重点工作之一。

## 开源与第三方软件

StarCap 是独立维护的开源项目，同时包含或改造自其他开源项目的代码与设计实现。我们保留法律要求的原始版权、许可证和第三方声明。

详情请查看：

- [`LICENSE`](./LICENSE)
- [`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md)

保留这些声明不代表原上游作者对 StarCap 的后续修改、发布或品牌提供背书。

## Logo

StarCap 的标志是一枚由五个不同颜色星角组成的五角星，各色区域之间使用透明分隔。

Logo 原件与 Windows 图标资源位于：

[`Assets/Branding`](./Assets/Branding)

## 项目状态

`v0.9.7` 是 StarCap 品牌与开源项目身份正式建立的第一个开发版本。当前阶段优先完成：

- StarCap 品牌统一
- 原项目宣传与作者联系方式清理
- 许可证与第三方声明整理
- 程序名称、资源、构建产物统一
- GitHub 独立项目化
- 在不破坏 v0.9.6 已有功能的前提下继续开发

## 贡献

欢迎通过 GitHub 提交 Issue、建议与 Pull Request。

---

**StarCap · 阿星**
