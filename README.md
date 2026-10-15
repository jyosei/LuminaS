# LuminaS

面向摄影选片与调色的离线桌面应用，使用 C++20 / Qt 6 Widgets。
当前源码基线为 **v0.1.0**，优先支持 Windows；仍处于早期开发阶段。

## 当前功能

- JPEG / Canon CR3 导入、选片和后台预解析，长边 3840 像素编辑预览，小图保持原尺寸。
- 小手拖动、滚轮缩放、裁剪旋转、撤销重做、参数预设，以及影调、平滑曲线、HSL、颜色分级和传统细节处理。
- JPEG / PNG / TIFF 导出，RGB / CMYK ICC、8 / 16 位、尺寸与 PPI、路径命名、元数据筛选和导出预设。
- 从原始输入重新计算完整编辑结果；冻结导出队列，支持取消、失败重试及实际成片 100% 检查。

## 构建与验证

需要 MSVC 2022 x64、CMake ≥ 3.24、Ninja、Qt 6.8（Widgets / Concurrent / Svg / Test）、LibRaw 0.22.2、Little CMS 2.19.1、LibTIFF 4.7.1 和 zlib 1.3.1。
依赖 SDK 不随源码仓库提供；先按 [构建说明](docs/engineering/BUILD.md) 配置依赖与环境变量，再执行：

```powershell
./tools/dev.ps1 configure
./tools/dev.ps1 build
./tools/dev.ps1 test
./tools/run-s2.ps1
```

最新导出模块验收记录为完整回归 33/33、后续相关检查 6/6；详见 [导出验收](docs/engineering/EXPORT-V1-ACCEPTANCE.md)。
真实照片测试需要自行提供本地样片，默认仓库不包含照片、预览、截图或历史桌面包。

## 当前限制

编辑状态主要保留在会话内，尚未实现完整项目保存与后续 S3 工作流。设计输入上限为 45MP；已验证 R7 JPEG / CR3 和合成 45MP，真实 45MP RAW 尚缺样本。CMYK 数值转换已验证，指定印厂与纸张的实际印刷效果尚未验证。跨机器安装发行也尚未认证。

历史设计、阶段记录和性能数字见 [工程进度](docs/engineering/STATUS.md)；历史文档中未实现能力不作为当前功能承诺。浏览器原型不参与生产图像处理。

## 许可证与开发工具

项目自有源码采用 [Apache License 2.0](LICENSE)。Qt、LibRaw、Little CMS、LibTIFF、zlib、Lucide 图标及 Source Serif 字体分别遵循各自许可；仓库保留相关资源许可，外部 SDK 的许可见其原始发行包。

`tools/deepseek` 与 `tools/dsh` 是可选开发协作工具，需要开发者自行配置，应用运行无需这些工具或在线 API。请勿提交 API 密钥、本机配置或私人照片。
