# Lumina · 方案审查交接包

Review v1 · 2026-10-12 · 当前为设计阶段

本包交给 Astra 进行产品、UI、技术栈与架构审查。没有生产编辑器或软件安装包；不要把浏览器原型和性能预算当作已实现能力。

## 开始审查

1. 先读 [审查入口](ASTRA-REVIEW.md)，确认用户需求、工程假设与当前状态。
2. 读 [待决策表](DECISIONS.md)，重点判断 UI 框架、RAW 显影范围、首版/P0 范围、颜色与存储。
3. 将 [审查提示词](REVIEW-PROMPT.md) 与本包一起交给 Astra。
4. 如只方便读取单个文本，读 [完整汇总稿](ALL-IN-ONE.md)。

## 包内内容

| 路径 | 用途 |
|---|---|
| `docs/01-product.md` | 产品流程、功能分期与验收 |
| `docs/02-stack.md` | 候选技术栈、替代路线与开源建议 |
| `docs/03-architecture.md` | 文档模型、RAW/颜色、渲染/缓存、存储与 AI 协议 |
| `docs/04-roadmap.md` | 开发路线、待测预算与验证 |
| `docs/05-sources.md` | 已查一手来源与未解决的问题 |
| `docs/06-ui-design.md` | 第二版界面取舍与设计约束 |
| `prototype/lumina-studio-preview.html` | 可直接在现代浏览器打开的第二版交互预览 |
| `prototype/lumina-studio-fragment.html` | 原型片段源码，需预览容器提供相应环境 |
| `prototype/screenshots/` | 两种布局的深浅主题截图及窄屏截图 |
| `prototype/assets/` | AI 生成的人像样片、压缩预览和最终提示词 |
| `qa/report.json` | 本轮原型检查的实际结果与未测范围 |
| `qa/verify-prototype.cjs` | 可配置依赖路径的检查脚本 |
| `MANIFEST.json` | 每个文件的大小与 SHA-256；不包含自身哈希 |
| `verify-integrity.py` | 校验文件是否完整、是否被改动 |

查看界面优先打开预览或截图。样片内嵌；图标与提示定位依赖 CDN，断网可能缺图标。原型的菜单、曲线、人像修饰/液化与 AI 部分含设计示意，详情见审查入口。

## 内容取舍与验证

只保留当前第二版原型。第一版插画线框、旧截图、临时修订脚本和带本机绝对缓存路径的旧检查脚本不进入包。

打包前重新运行原型交互/布局检查，重新生成截图；打包后检查 ZIP CRC、每个文件的 SHA-256 和相对文件链接。报告证明的范围限于浏览器原型；没有生产程序的性能、颜色准确性、RAW 或 AI 测试。

解压后可用 Python 标准库校验：

```text
python verify-integrity.py
```

原型检查复现说明见 [qa/README.md](qa/README.md)。正式开源许可证、模型/依赖的分发审核和项目名称检索仍需敲定。本包仅为审查材料，不包含 API 密钥、模型权重或生产二进制。
