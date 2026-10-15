# 技术来源与验证状态

检索日期：2026-10-12。只使用官方文档/项目源码作为技术事实依据；本项目的推荐、预算与架构是工程判断，并非来源的性能保证。实现阶段必须将 URL 与具体 tag / commit 绑定。

| 编号 | 一手来源 | 支撑内容 |
|---|---|---|
| S1 | [egui 官方仓库](https://github.com/emilk/egui) | 原生/网页、自绘即时 UI、egui-wgpu、AccessKit 与项目许可 |
| S2 | [wgpu Backends](https://docs.rs/wgpu/latest/wgpu/struct.Backends.html) / [官方仓库](https://github.com/gfx-rs/wgpu) | DX12、Vulkan、Metal 等后端；能力需要设备检查 |
| S3 | [LibRaw 文档](https://www.libraw.org/docs) / [About](https://www.libraw.org/about) / [API Overview](https://www.libraw.org/docs/API-overview.html) | RAW 读取与处理 API；LGPL-2.1 / CDDL 双许可；高质量显影仍需独立工程 |
| S4 | [Little CMS 官方站](https://www.littlecms.com/) | ICC 色彩管理、MIT 许可；付费优化插件与开源核心区分 |
| S5 | [ORT DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html) / [Windows ML](https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/overview) | DirectML 持续维护、新 Windows ML 路线；部署方式和 OS/硬件限制 |
| S6 | [MobileSAM 官方仓库](https://github.com/ChaoningZhang/MobileSAM) / [导出脚本](https://github.com/ChaoningZhang/MobileSAM/blob/master/scripts/export_onnx_model.py) | 轻量点/框选区候选；官方 ONNX 脚本实际导出提示编码与 mask 解码，不包含图像编码器 |
| S7 | [SAM 2 官方仓库](https://github.com/facebookresearch/sam2) | 分割质量对照；官方说明 checkpoints 与相关代码 Apache-2.0，部分附属资源另有许可 |
| S8 | [Qt Licensing](https://doc.qt.io/qt-6/licensing.html) | 模块许可不同，需按实际模块组合核查 |
| S9 | [Tauri 官方说明](https://v2.tauri.app/start/) | 采用系统 WebView 的桌面路线；不由此推导运行内存指标 |
| S10 | [Rayon 官方仓库](https://github.com/rayon-rs/rayon) | Rust CPU 数据并行 |
| S11 | [rusqlite 官方仓库](https://github.com/rusqlite/rusqlite) | SQLite 绑定、bundled / backup / blob 等特性 |

尚未验证：实际硬件性能、Windows 笔压输入、完整 MobileSAM ONNX 编码器、Windows ML 的部署/FFI、RAW 显影质量、图像编解码器的元数据保真、SQLite 大项目性能。当前无 benchmark 或算法已实现的声明。

尚未核实的扩展：ComfyUI 具体 API、AI 降噪模型、扩散模型与服务提供商、DCP/镜头配置数据、HEIF/AVIF、PSD 兼容、项目名称可用性。进入实现范围时再查官方协议和分发条件。
