# LuminaS 调研：可集成的人像 AI 开源工具

调研日期：2026-10-13（Asia/Shanghai）。用户确认前置条件：**LuminaS 定位非商用开源软件**。

本文是资料核验与选型建议，不是实测报告：本轮没有下载权重、导出 ONNX、加载模型或测量推理性能。文中任何尺寸/耗时/显存数字都来自官方资料并标注来源；没有来源的数字不写。许可结论只针对实际读到的官方文件并附 URL；同一仓库的**代码许可不等于权重许可**，两者分开记录，未核实项见第五节。

## 0. 结论（先读）

1. 人像 AI 依赖可以拆成三层，投入产出顺序是：**本地小模型（检测/关键点/抠图/修补/超分/降噪/景深）→ 自研确定性算子（磨皮、液化位移场、肤色保护）→ 用户自装生成服务（重打光、换脸、生成式改图）**。第三层最贵、最难保证可复现，也是唯一必须依赖外部服务的部分；与 `ai-distribution.md` 的「生成适配器/用户自装服务」结论一致。
2. **没有许可干净、可随包的「AI 自动磨皮/美颜」模型**：皮肤质感重建类模型基本受 CelebA / CelebAMask-HQ 系训练数据与非商用条款约束。LuminaS 的正确做法是：模型只负责**脸部/皮肤/五官区域蒙版**，磨皮本身用自研确定性算子（频率分离/保边滤波 + 皮肤保护），与 `01-product.md`「皮肤优化应保留纹理、默认低强度、可比较、可撤销」一致。
3. 非商用定位解锁了一批候选（CodeFormer、RMBG-2.0、Depth Anything V2 Base 等 NC 权重可以合法用于非商业目的的再分发），但仍有两类**不能随包**：仅限**研究**用途（InsightFace 全部模型、InstantID checkpoint）、以及**禁止再分发衍生数据**（CelebAMask-HQ 系列解析权重）。这些一律走「用户自行下载 + 显式许可确认」，不进安装包。
4. **第一批只建议做三件事**：①人脸检测（YuNet，顺带支撑人脸感知液化与皮肤区域近似）；②人像/头发抠图（MODNet，换背景、虚化、语义蒙版）；③局部修补（MI-GAN/LaMa，去瑕疵的实际像素来源）。这三件都只需 CPU、都是宽松许可、都能走现有 `ai-worker` + ONNX Runtime 结构。超分/降噪/景深放在第二批；人脸修复与重打光最后。
5. 与本项目已有结论不冲突：AI 仍走独立 `ai-worker` 进程 + ONNX Runtime C API，AI 包独立下载，不进基础 ZIP ≤120 MiB 的预算；沿用 `review-notes/ai-distribution.md` 的 manifest 字段、任务/revision/取消契约，不新建通用插件框架。

### 0.1 推荐的第一批（A 档：宽松许可，可随包分发）

| 能力 | 首选 | 许可 | 关键事实 |
|---|---|---|---|
| 人脸检测（含 5 点） | YuNet（OpenCV Zoo） | MIT（模型目录）/ Apache-2.0（zoo） | 官方 ONNX、动态 shape、CPU 毫秒级；后处理需自实现 |
| 人脸密集关键点（液化用） | MediaPipe Face Landmarker（许可待确认）或先用 YuNet 5 点做初版 | 代码 Apache-2.0 / 权重待核实 | 478 点 + iris；走 ORT 需社区 ONNX 转换 |
| 人像 / 头发抠图 | MODNet | Apache-2.0（README 明确含 **models**） | 官方 ONNX 转换脚本；fp32 25.9 MB / fp16 13.0 MB / int8 6.6 MB |
| 小区域去瑕疵 | MI-GAN | MIT | 移动端 inpainting，需自行导出 ONNX |
| 大区域修补（备选） | LaMa | Apache-2.0（代码） | 权重许可需单独确认后再进包 |
| AI 降噪（首个可选包） | Intel OIDN | Apache-2.0，随库分发权重 | 原生 C/C++ API，最容易集成，CPU 优化 |
| 超分 / 放大 | Real-ESRGAN：x4plus、realesr-general-x4v3 | BSD-3-Clause | 官方提供 ncnn 可执行；权重白名单必须写死 |
| 人像虚化（单目景深） | Depth Anything V2 **Small** | Apache-2.0（Small） | 24.8 M 参数、默认输入 518；Base/Large 是 CC-BY-NC-4.0 |
| 脸部磨皮 / 眼牙增强 | **无第三方权重**：自研确定性算子 + 关键点几何蒙版 | — | 见 1.3 节；这是本项目必须自研的部分 |

B 档（非商用可用、但整包随之只能非商用，且必须携带许可文本）：CodeFormer（S-Lab 1.0）、GFPGAN（仓库自称 Apache-2.0，LICENSE 内含 NVIDIA 非商用条款与 DFDNet CC BY-NC-SA 4.0）、Depth Anything V2 Base/Large（CC-BY-NC-4.0）、BRIA RMBG-2.0（CC BY-NC 4.0）/ RMBG-1.4（门控非商用）。

C 档（不随包，只能用户自取或另行授权）：InsightFace 全部预训练权重与 inswapper、InstantID 已发布 checkpoint、300W/ibug 系关键点权重（PIPNet 98 点、dlib 68 点）、CelebAMask-HQ 派生的人脸解析权重、RVM（GPL-3.0，与当前 Apache-2.0 治理冲突）、`4x-UltraSharp`/`4x-AnimeSharp`（非商用）。

## 1. 能力地图与候选（按 LuminaS 的工序分组）

> 本节的「可商用/可分发」按非商用开源产品口径评估：**A 档**=宽松许可（Apache/MIT/BSD），随包分发无障碍；**B 档**=非商用可用但需携带许可文本、明示限制，且可能限制二次分发方式；**C 档**=研究用途/禁止再分发/来源不清，只能用户自取、不随包。
>
> 表中许可结论均为本轮直接读取官方 `LICENSE`/`README` 原文或 GitHub API 的 license 字段得到，来源 URL 随行标注；凡是未能读到原文的一律写明「未核实」，并汇总在第五节。凡是「体积/算力」栏带来源的即为官方或仓库数据，其余标注未核实。

### 1.1 人脸检测 / 关键点 / 部位解析（所有「按人脸区域作用」功能的前置）

| 候选 | 许可（一手核实） | ONNX / C++ | 体积 / 输入 | 判断 |
|---|---|---|---|---|
| YuNet（OpenCV Zoo） | 模型目录 **MIT**（[LICENSE](https://github.com/opencv/opencv_zoo/blob/main/models/face_detection_yunet/LICENSE)），zoo 整体 Apache-2.0；上游 [libfacedetection.train](https://github.com/ShiqiYu/libfacedetection.train) 为 BSD-3 | **官方 ONNX**（2023mar 静态、2026may 动态 shape），可用 `cv::FaceDetectorYN` 或 ORT C API（后处理 priors/解码需自实现） | 轻量（本轮未逐一核实具体字节数，按模型目录文件为 KB–MB 级），建议长边缩到人像尺度再推理 | **首选**：本轮唯一「代码 + 权重双许可干净 + 官方 ONNX」的检测模型；官方说明其训练方案覆盖约 10×10–300×300 像素的人脸，45MP 原图必须先按人脸尺度下采样 |
| libfacedetection | BSD-3-Clause；模型烘进源码 | 纯 C++，无官方 ONNX | 极小 | 备选（不走 ONNX 运行时时可用） |
| MediaPipe（Face Detector / Face Landmarker / Selfie Multiclass） | 代码 **Apache-2.0**；**模型卡许可本轮无法核实**（`mediapipe.page.link` 与 `ai.google.dev` 不可达）；第三方 [高通 AI Hub manifest](https://github.com/qualcomm/ai-hub-models) 标 apache-2.0 | 官方有 C/C++ Tasks API 与 `.task` 包（TFLite 体系）；走 ORT 只有社区 ONNX 转换（PINTO_model_zoo 等） | Selfie Segmenter 256×256 / 447 KB / 106 K 参数（第三方 manifest）；Face Landmarker 体积未核实 | 能力最贴需求（478 点 + iris + blendshape；face-skin / hair / clothes 等 6 类分割）。**权重许可确认前不作为随包默认**；技术验证可以先做 |
| InsightFace（SCRFD / RetinaFace / buffalo_l） | 代码 MIT；**预训练模型明确「仅限非商用研究」**，商业授权需联系官方（[model_zoo](https://github.com/deepinsight/insightface/blob/master/model_zoo/README.md)） | 官方 ONNX + 导出脚本，社区有 ORT C++ 实现；SCRFD-500M 仅 0.57 M 参数 | buffalo_l 326 MB | C 档：不随包。想要 SCRFD 的效果，必须换成自训或取得授权 |
| PIPNet | 代码 MIT；权重训练于 300W / WFLW / COFW / AFLW / LaPa，而 300-W 系模型的商用限制有公开记载（[dlib-models 说明](https://github.com/davisking/dlib-models)） | 社区 ONNX；关键点回归需自实现 RoI 对齐后处理 | ResNet18 主干，WFLW 98 点 | 高风险，不随包 |
| dlib | 代码 Boost；68 点与 GTX 模型作者明确「trained on ibug 300-W … can't be used in a commercial product」 | 无官方 ONNX | — | 不纳入（5 点/检测称 public domain，但能力弱于 YuNet） |
| 人脸部位解析（face parsing） | `zllrunning/face-parsing.PyTorch` 代码 MIT，但权重训练于 **CelebAMask-HQ**；`facexlib` parsing 同源；`jonathandinu/face-parsing`（SegFormer）同样是 CelebAMask-HQ 派生 | 有社区 ONNX | 512×512 | **都不能随包**（见红线表）。皮肤/五官蒙版在「许可干净」路线下没有现成权重 |
| DML-CSR / PaddleSeg face_parse 等 | 未核实（无 LICENSE / 权重许可段落） | — | — | 不纳入，直到补齐证据 |

**这一节的直接后果**：皮肤、眼睛、嘴唇、头发等**部位语义蒙版**在 A 档里没有现成模型。可行的三条路：① 用关键点 + 几何区域近似（YuNet 5 点或 MediaPipe 密集点，自研、无权重风险）；② 用 Apache-2.0 的 BiSeNet/PaddleSeg 代码在自有或已授权掩码上重训（属模型开发，工作量大，需单独评估）；③ 接受 B 档： CelebAMask-HQ 派生权重由用户自取、不随包、不对质量作承诺。

### 1.2 去瑕疵与局部修补（v0.2 点式去瑕疵的像素来源）

| 候选 | 许可（一手核实） | 权重/训练数据 | 集成判断 |
|---|---|---|---|
| MI-GAN | **MIT**，仓库活跃（[API license=mit](https://github.com/Picsart-AI-Research/MI-GAN)） | 权重随仓库 | **首选**：面向移动端的轻量 inpainting，适合痘印/小杂物；ONNX 需自行导出，须连同预处理/尺寸一起锁版本 |
| LaMa | **Apache-2.0**（Copyright 2021 Samsung Research，[LICENSE](https://github.com/advimman/lama/blob/main/LICENSE)） | 权重未单列许可；训练用 Places2 等数据集另有条款 | 大区域修补备选；导出 ONNX 后按「可选包」分发，权重许可需在打包前单独确认 |
| AOT-GAN | **Apache-2.0**（[researchmm/AOT-GAN-for-Inpainting](https://github.com/researchmm/AOT-GAN-for-Inpainting)） | 权重随仓库 | 备选；先与 MI-GAN 做同图对比再决定是否纳管 |
| ZITS | 代码 Apache-2.0，但依赖 GLaMa 权重、许可不明确 | — | **不纳入**：依赖链许可不清 |
| MAT | 仓库 license=Other/NOASSERTION，非商用 | — | 仅用户自用实验，不随包、不进文档承诺 |
| PowerPaint / SD inpaint | 代码 MIT，但依赖扩散底座 | SD 系许可未核实 | 归入「本地生成服务」路线，不做本地小模型集成 |
| IOPaint (lama-cleaner) | Apache-2.0，但仓库**已归档**（archived=true，最后 push 2025-04） | — | **不作为依赖**；只当 ONNX 导出脚本/参考实现来源 |

### 1.3 皮肤优化（磨皮）与眼睛/牙齿

这一项必须先说结论：**不存在许可干净、可直接随包的开源「AI 美颜/磨皮」权重**。

- CelebAMask-HQ 官方协议原文：数据集「available for **non-commercial research purposes** only」，并明确「not to further copy, publish or distribute any portion of the CelebAMask-HQ dataset」。[官方协议](https://mmlab.ie.cuhk.edu.hk/projects/CelebA/CelebAMask_HQ.html) → 用它训练出的解析权重属于衍生数据，**无论商用与否都不能随包再分发**。
- 直接后果：`face-parsing.PyTorch`（BiSeNet）代码是 MIT，但权重训练于 CelebAMask-HQ，进不了安装包；BeautyGAN/PSGAN/SSAT 一类同样受 CelebA 系数据与非商用条款封锁（本轮的仓库查询里 MAT/SSAT 均为非标准许可，SSAT 官方 LICENSE 路径 404）。
- 可行的工程路线：模型只提供**脸部/皮肤/五官区域蒙版**，磨皮用自研确定性算子（频率分离/保边滤波 + 皮肤权重 + 纹理保留），眼睛/牙齿用「区域蒙版 + 局部亮度和饱和度控制」近似。这与 `01-product.md`「皮肤优化应保留纹理、默认低强度、可比较、可撤销」和「不用低分辨率掩膜假装精确」一致。
- 可参考实现：`opencv_contrib`（Apache-2.0，双边/导向滤波等，自研主线已决定不默认引入整个 OpenCV）；`Thin-Plate-Spline-Motion-Model`（MIT）只作 TPS 数值参考。
- RetouchGPT（MIT，AAAI 2025）依赖扩散底座 + LLM，8–16 GB 核显笔记本不现实，本轮不纳入。

### 1.4 人脸感知液化

- 关键点来源：MediaPipe（代码 Apache-2.0，[仓库](https://github.com/google-ai-edge/mediapipe)）的 Face Landmarker/Face Mesh，提供密集人脸关键点；其模型卡许可在本轮抓取失败，列为未核实（第五节）。
- 形变数学（MLS/TPS 位移场）自研，直接落在现有 `WarpNode`/位移场设计里，不需要额外模型权重；「人脸关键点辅助」在 `01-product.md` 里本就标为需单独验证。

### 1.5 肖像抠图 / 主体分割 / 头发级 matting

| 候选 | 许可（一手核实） | 体积/算力（官方或仓库数据） | 判断 |
|---|---|---|---|
| MODNet | 仓库 README 明确「The code, **models**, and demos … are released under the Apache License 2.0」；[LICENSE](https://github.com/ZHKKKe/MODNet/blob/master/LICENSE)；HF `onnx-community/modnet-webnn` 卡 license=apache-2.0 | 预转 ONNX：fp32 25.9 MB / fp16 13.0 MB / int8 6.6 MB；PaddleSeg 版 6.5 M 参数、15.7 GFLOPs、512 输入 68.4 FPS；512×512 输入，核显 <1 GB | **默认引擎**：许可最干净、官方自带 [ONNX 导出脚本](https://github.com/ZHKKKe/MODNet/blob/master/onnx/README.md)、免 trimap、单帧、人像专用、全离线 |
| BiRefNet | 代码 **MIT**（[LICENSE](https://github.com/ZhengPeng7/BiRefNet/blob/main/LICENSE)）；`BiRefNet-portrait` 权重卡 license=mit；官方 release 提供 ONNX | lite(SwinT) pth 177.8 MB / ONNX 224 MB，lite-2K ONNX 331 MB，SwinL ONNX 973 MB；1024 输入 FP16 约 3.45 GB 显存、4090 约 17 FPS，ONNX 比 PyTorch 慢约 75%(SwinT)/90%(SwinL) | 许可最好的高质量档，但**不能默认**：只用 lite 或降到 512，且必须作为可选下载；ONNX 延迟要实测 |
| PaddleSeg PP-MattingV2-512 | 仓库 Apache-2.0；PP-HumanSeg README 明确「can be deployed to products at zero cost」 | 8.95 M 参数 / 7.51 GFLOPs；PP-Matting-512 24.5 M；PP-HumanSegV1-Lite 2.3 MB | 备选：算力极低、许可干净，代价是要自建 Paddle2ONNX 转换与回归（官方 onnxruntime C++ 示例只覆盖 bisenetv2） |
| BRIA RMBG-2.0 | 模型卡 **CC BY-NC 4.0**（非商用），GitHub 仓库 license=NOASSERTION | 221 M 参数、1024×1024 | 非商用项目**可用**但整包随之变成「仅非商用」，且要逐条接受 BRIA 条款；本轮未读到条款原文，列为待确认后再决定 |
| BRIA RMBG-1.4 | 门控自定义许可（license_name=bria-rmbg-1.4），明确 non-commercial、需接受表单 | 44.1 M 参数 | 只能 BYO 自取，不随包 |
| RVM (RobustVideoMatting) | 代码与权重 **GPL-3.0**（[API license=gpl-3.0](https://github.com/PeterL1n/RobustVideoMatting)）；有官方 ONNX | mobilenetv3 fp32 15.0 MB / fp16 7.5 MB | **不纳入**：与当前 Apache-2.0 治理冲突（分发即要求整包 GPL 兼容），且是时序模型，照片编辑用不上记忆状态 |
| MatAnyone | S-Lab License 1.0（非商用可再分发，商业需授权） | — | 属 B 档而非禁用；但为视频/时序设计，照片场景收益有限，不优先 |
| Sapiens | CC BY-NC 4.0（非商用可再分发，需署名） | — | B 档；作为人体基础模型体积大，暂不纳入 |
| IndexNet | license=NOASSERTION（权利真空） | — | 不采用 |
| SAM 1 / SAM 2.1 / MobileSAM | Apache-2.0（SAM2.1 权重卡亦 apache-2.0） | — | 保留为交互选区（点/框），输出是二值 mask，**不是 alpha matte**；不要用来说「发丝级抠图」 |
| 其他边界项 | BackgroundMattingV2 MIT（需额外拍背景图）；GCA/FBA MIT（需 trimap，已停更）；Matting Anything MIT（依赖 SAM，2023-11 后停更）；InSPyReNet MIT | — | 都不适合作为普通照片的默认抠图引擎 |

### 1.6 人脸修复 / 超分 / AI 降噪 / 单目景深 / 重打光

| 方向 | 候选 | 许可（一手核实） | 体积/依据 | 判断 |
|---|---|---|---|---|
| 人脸修复 | RestoreFormer | 代码 Apache-2.0 | 未给官方耗时 | 许可最干净的人脸修复候选；权重许可未见独立声明，需复核 |
| 人脸修复 | VQFR | 代码 Apache-2.0，第三方清单无 NC 组件 | — | 与 RestoreFormer 并列的干净候选 |
| 人脸修复 | GFPGAN | 仓库自称 Apache-2.0，但同一 LICENSE 文件列出 DFDNet（CC BY-NC-SA 4.0）与 NVIDIA StyleGAN2（仅非商用研究/评估）；[原文](https://github.com/TencentARC/GFPGAN/blob/master/LICENSE) | — | 非商用项目可用；**不得对外称「Apache-2.0 组件」**，需携带完整许可与 NOTICE |
| 人脸修复 | CodeFormer | **S-Lab License 1.0**（[原文](https://github.com/sczhou/CodeFormer/blob/master/LICENSE)）：允许非商业目的的源码/二进制再分发（保留版权声明），商业用途需联系作者 | — | 非商用可以随包（附许可），但整包因此只能非商用；商业化路径需另行授权 |
| 人脸修复 | SUPIR / OSDFace | SUPIR 自定义许可明确限制；OSDFace 仓库无 LICENSE（raw 404），依赖 SD2.1 基座 OpenRAIL++ 条款 | SUPIR 为 SDXL 级 | 均不纳入 |
| 超分 | Real-ESRGAN | 仓库 **BSD-3-Clause**（[LICENSE](https://github.com/xinntao/Real-ESRGAN/blob/master/LICENSE)）；官方提供 ncnn 可执行 | x4plus / x2plus / realesr-general-x4v3 见 [model_zoo](https://github.com/xinntao/Real-ESRGAN/blob/master/docs/model_zoo.md) | 核显首选；权重许可未见独立文件，anime 系列是否另有条款**未核实**；其人脸增强路径会引入 GFPGAN 的限制 |
| 超分 | SwinIR / BSRGAN / KAIR | Apache-2.0 / Apache-2.0 / MIT | — | 备选（SwinIR 更重） |
| AI 降噪 | Intel OIDN | **Apache-2.0**，随库分发权重；**原生 C/C++ API** | 深度学习降噪器（U-Net），CPU 优化 | 工程上最容易集成、完全离线；适合作为「AI 降噪」第一个可选包，先与传统降噪做同图对比 |
| AI 降噪 | NAFNet / SCUNet | NAFNet 仓库 LICENSE 文本含 MIT 与 BasicSR 的 Apache-2.0（GitHub 识别为 Other，需人工复核）；SCUNet Apache-2.0 | — | 备选；均在 RGB 域，RAW 域无清晰可用的权重许可 |
| AI 降噪 (RAW) | PMRID | **Apache-2.0**（[MegEngine/PMRID](https://github.com/MegEngine/PMRID)） | 面向移动端的 RAW 域降噪 | 与 LuminaS 的 RAW 工作域概念最贴合；权重与模型接口需自行导出验证 |
| AI 降噪 | Restormer / ELD / nind-denoise | **未取得一手许可证据**（raw LICENSE 404 或 README 无 License 段） | — | 不纳入，直到补齐证据 |
| 单目景深 | Depth Anything V2 | 仓库 Apache-2.0；官方 README 明确 **Small=Apache-2.0，Base/Large/Giant=CC-BY-NC-4.0** | 参数 24.8 M / 97.5 M / 335.3 M，默认输入 518（官方表格） | 非商用项目里 Small 是 A 档、Base 是 B 档（NC）；8–16 GB 核显优先 Small |
| 单目景深 | MiDaS | MIT | — | 备选；权重许可未核实 |
| 单目景深 | Apple DepthPro | 代码为 Apple 自定义许可；权重许可未核实 | — | 暂不纳入 |
| 重打光 | IC-Light | 代码 Apache-2.0（[LICENSE](https://github.com/lllyasviel/IC-Light/blob/main/LICENSE)）；其 README 自述流水线里 RMBG-1.4 为非商用、商用需换 BiRefNet | 权重基于 SD1.5 → OpenRAIL-M 类条款 | 只能走「本地生成服务 + BYO 权重」路线；不要随包 |
| 重打光 | SwitchLight | 未找到官方开源仓库证据 | — | 不纳入 |

### 1.7 本地生成服务生态（重打光 / 换脸 / 生成式改图）

结论：与本项目已有适配决策（`/upload/image → /prompt → /ws → /history → /view`，禁用任意 custom node）**原生匹配的只有 ComfyUI**：按本轮核实的资料，重打光（IC-Light）、放大、人脸修复都有节点实现；但「究竟是核心节点还是必须装扩展」**必须在锁定版本时用 `/object_info` 逐项确认**（`ai-distribution.md` 已把这一步列为适配器前置），本报告不把「核心内置」当已验证事实。备选是 IOPaint 的服务模式（Apache-2.0、没有 workflow 概念、没有版本漂移，但仓库已归档、能力面窄）。

| 服务/扩展 | 能力 | 代码许可 | 权重/依赖许可 | 固定 workflow 可用？ |
|---|---|---|---|---|
| ComfyUI 本体（锁定版本） | 生成、重打光、放大、人脸修复 | GPL-3.0（[LICENSE](https://github.com/comfyanonymous/ComfyUI)）；进程外 HTTP 调用是否构成传染需按实际分发形态复核，不能只凭「用了 HTTP」下结论 | 无自带权重 | **能**（仅核心节点） |
| FaceRestore 节点（核心与否待 `/object_info` 确认） | 人脸修复 | 同 ComfyUI（若为扩展则另计） | GFPGAN（仓库自称 Apache-2.0，见红线表）、CodeFormer（S-Lab 非商用）、RestoreFormer 权重条款不明 | 能 |
| Upscale 节点 | 超分 | 同 ComfyUI | Real-ESRGAN 系列 BSD-3；`4x-UltraSharp`/`4x-AnimeSharp`（Kim2091）为非商用 | 能，但必须**限定权重白名单**，不能只写「超分」 |
| [ComfyUI-IC-Light](https://github.com/kijai/ComfyUI-IC-Light) | 重打光 | Apache-2.0 | 官方 IC-Light 权重卡本轮未读到（HF 不可达） | 能，但已并入核心节点，无需装 |
| [ComfyUI_IPAdapter_plus](https://github.com/cubiq/ComfyUI_IPAdapter_plus) | 风格/人脸参考 | GPL-3.0 | IP-Adapter 权重多为 Apache-2.0；IP-Adapter-FaceID 引 InsightFace → 非商用 | 需扩展 |
| [ComfyUI-ReActor](https://github.com/Gourieff/ComfyUI-ReActor) | 换脸 | GPL-3.0 | inswapper_128 / buffalo_l **仅非商用研究**，且 inswapper 现需向官方申请授权 | 需扩展 |
| [ComfyUI-FaceAnalysis](https://github.com/cubiq/ComfyUI_FaceAnalysis) | 人脸检测/相似度打分 | Apache-2.0 | 依赖 InsightFace 模型 → 非商用 | 需扩展 |
| [FaceFusion](https://github.com/facefusion/facefusion) | 换脸/修复 | OpenRAIL-AS（许可细则页多次抓取失败，**未核实**） | 权重系谱含换脸模型 | **不能**：无稳定 HTTP API，只有 CLI 子命令 |
| [InstantID](https://github.com/instantX-research/InstantID) | 单图 ID 保持生成 | Apache-2.0 | README 明示已发布 checkpoint **仅限研究** | 需扩展 |
| [PuLID](https://github.com/TheToBeginning/PuLID) | 无微调 ID 定制 | Apache-2.0 | 依赖 InsightFace；权重条款不明；官方称 FLUX 版需 16 GB 显存 | 需扩展 |
| [ComfyUI-Impact-Pack](https://github.com/ltdrdata/ComfyUI-Impact-Pack) | 人脸检测+细节重绘 | GPL-3.0 | 依赖 ultralytics（**AGPL-3.0**）+ SAM + 修复模型 | 需扩展；依赖链许可最杂，不进首个适配器 |
| stable-diffusion-webui / Forge / reForge | 全功能 WebUI + API | **AGPL-3.0**（含第 13 条网络条款） | 用户自装 | 能，但合规成本与 fork 依赖风险高 |
| Fooocus | 简化 SDXL 生成 | GPL-3.0 | 用户自装 SDXL | 能，细粒度控制弱 |
| SwarmUI | 前端 + API | MIT | 后端仍是 ComfyUI | 能，多一层安装门槛 |
| [IOPaint](https://github.com/Sanster/IOPaint) 服务模式 | 擦除/修补/抠图/超分/人脸修复 | Apache-2.0（**仓库已归档**） | LaMa 等擦除模型宽松；扩散修补模型各自条款 | 能（`iopaint start --model=lama --port=8080`，CPU 可跑） |

分界线（工程判断）：

- **只适合走扩散服务**：重打光/换光照、换脸、生成式皮肤重绘、生成式换背景/修补、ID 保持生成。
- **更适合本地 ONNX 小模型（随包更划算）**：人脸检测与关键点、抠图与前景分割、去瑕疵/擦除、超分、景深。理由是低显存、离线、无安装门槛、无隐私外发、无 GPL/AGPL 传染。

## 2. 许可红线（我已逐字读到的官方原文）

这些是本轮最容易被误传的点，先单独列出。

| 组件 | 实际读到的内容 | 对 LuminaS 的含义 |
|---|---|---|
| GFPGAN | `LICENSE` 写明「GFPGAN is licensed under the Apache License Version 2.0 **except for the third-party components listed below**」，随后列出 StyleGAN2（NVIDIA 许可，其 §3.3 Use Limitation 明确“only may be used or intended for use non-commercially… for research or evaluation purposes only”）与 DFDNet（CC BY-NC-SA 4.0）。[LICENSE](https://github.com/TencentARC/GFPGAN/blob/master/LICENSE) | **不能对外宣称「GFPGAN 是 Apache-2.0」**。非商用项目可以使用，但要随包携带完整许可/NOTICE，注意 CC BY-NC-SA 的 ShareAlike，且其「research or evaluation」措辞比「非商用」更窄，需要用户自行确认使用场景。 |
| InsightFace 全部模型 | 模型库首页开头即声明 “**ALL models are available for non-commercial research purposes only.**”（SCRFD、RetinaFace、buffalo_l 等都在此页）。[model_zoo](https://github.com/deepinsight/insightface/blob/master/model_zoo/README.md) | 这是**研究用途**限制，不等于「非商用产品可用」。默认不随包；若要使用，走用户自装/自下 + 明确提示，并在文档里保留该声明。人脸检测/关键点应优先找许可更干净、且能直接给 ONNX 的替代（见第一节）。 |
| Real-ESRGAN | 仓库为 BSD 3-Clause（Copyright 2021 Xintao Wang）。[LICENSE](https://github.com/xinntao/Real-ESRGAN/blob/master/LICENSE) | 代码/仓库许可干净；但仍要**逐权重**核对（不同 x4 模型的训练数据与来源不同），且它的人脸增强路径会依赖 GFPGAN，等于引入上一行的限制。 |
| MODNet | Apache-2.0；README 明确「code, **models**, and demos … are released under the Apache License 2.0」。[LICENSE](https://github.com/ZHKKKe/MODNet/blob/master/LICENSE) | A 档候选，可随包；官方未公开训练数据来源，manifest 里如实记为「未公开」，不要写成「已核实训练数据许可」。 |
| BiRefNet | 代码 MIT（Copyright 2024 ZhengPeng）。[LICENSE](https://github.com/ZhengPeng7/BiRefNet/blob/main/LICENSE)；`BiRefNet-portrait` 权重卡 license=mit（经镜像读取） | A 档候选；训练数据集（如 DIS5K 等）各自条款要单独核，权重选择限制在 lite 规格。 |
| LaMa | Apache-2.0（Copyright 2021 Samsung Research）。[LICENSE](https://github.com/advimman/lama/blob/main/LICENSE) | A 档代码许可；`big-lama` 权重的再分发条款须单独确认，不能拿仓库许可当权重许可。 |
| Depth Anything V2 | 仓库 Apache-2.0。[LICENSE](https://github.com/DepthAnything/Depth-Anything-V2/blob/main/LICENSE)；官方 README 明确 **Small=Apache-2.0，Base/Large/Giant=CC-BY-NC-4.0** | 只能随包 Small；更大模型属 B 档，需携带许可并保持非商用。 |
| IC-Light | 仓库 Apache-2.0。[LICENSE](https://github.com/lllyasviel/IC-Light/blob/main/LICENSE) | 代码干净；权重是 SD1.5 生态衍生物，许可随基座模型，需单独核对。 |
| OpenCV Zoo | Apache-2.0。[LICENSE](https://github.com/opencv/opencv_zoo/blob/main/LICENSE)；YuNet 目录单独声明 MIT。[模型说明](https://github.com/opencv/opencv_zoo/blob/main/models/face_detection_yunet/README.md) | Zoo 级许可；**每个模型条目另有自己的许可**，逐个确认后才能进包。 |
| CelebAMask-HQ（数据集） | 官方协议：仅限非商业研究，且「not to further copy, publish or distribute any portion of the CelebAMask-HQ dataset」。[协议原文](https://mmlab.ie.cuhk.edu.hk/projects/CelebA/CelebAMask_HQ.html) | 任何用它训练的解析权重（含 face-parsing.PyTorch 等）都属衍生数据，**不能随包分发**，与是否商用无关。人脸解析要么换训练来源、要么让用户自取。 |
| ComfyUI / SDXL / FLUX.1 Fill | 项目已核验：ComfyUI GPL-3.0；SDXL base 为 CreativeML Open RAIL++-M；FLUX.1 Fill dev 非商用且需接受条款。见 [ai-distribution.md](ai-distribution.md) | 生成侧继续按「用户自装服务 + BYO 权重」处理，不随包、不镜像权重。 |
| dlib 68 点 / GTX 关键点 | 模型作者明确说明这些模型「trained on ibug 300-W … can't be used in a commercial product」。[dlib-models](https://github.com/davisking/dlib-models) | 任何 300W/ibug 系关键点权重都带这个限制；PIPNet 等同样训练于 300W/WFLW，本轮一并按「不随包」处理。 |
| RVM (RobustVideoMatting) | 代码与权重均 **GPL-3.0**（[仓库](https://github.com/PeterL1n/RobustVideoMatting)，GitHub license=gpl-3.0） | 不是「能不能用」的问题，而是**治理冲突**：当前 LuminaS 计划 Apache-2.0，分发 GPL 组件会要求整包兼容 GPL。要么换模型，要么先改治理决策。 |
| CodeFormer | **S-Lab License 1.0** 原文：允许非商业目的的源码/二进制再分发（保留版权声明），商业用途需联系作者。[LICENSE](https://github.com/sczhou/CodeFormer/blob/master/LICENSE) | 非商用项目可以随包，但整包因此只能是非商用；文档必须写明，商业化前需取得授权。 |

### 2.1 依赖链与治理风险（不是权重条款，而是许可证传染）

| 组件 | 事实 | 影响 |
|---|---|---|
| stable-diffusion-webui / Forge / reForge | **AGPL-3.0**（含第 13 条网络交互条款） | 不宜作为 LuminaS 的目标服务；即便只是联动分发，合规成本也高于 ComfyUI 的 GPL-3.0 |
| ComfyUI（本体） | GPL-3.0 | 只经 HTTP 调用、不随包分发时风险低，但这条判断必须按**实际分发形态**复核，不能当作永久结论（`ai-distribution.md` 已有同样保留） |
| ComfyUI-Impact-Pack 链路 | GPL-3.0，且依赖 ultralytics（**AGPL-3.0**）+ SAM + 修复模型 | 依赖链许可最杂，不进首个适配器 |
| ComfyUI 扩展（IPAdapter_plus、ReActor 等） | 代码 GPL-3.0 | 与「只用核心节点」的既有决策冲突，不进首个 workflow |

一般性提醒（工程侧，不是法律结论）：

- **代码许可不能覆盖权重**；把 `.pt`/`.safetensors` 转成 ONNX 可能构成衍生作品，遇到 `NoDerivatives` 或「禁止修改后再分发」条款就不能自行转换分发。
- 训练集许可会传染到权重（CelebA/CelebAMask-HQ、Places2、DIV2K、DIS5K、danbooru 等各有限制）。
- 每个模型包按 `ai-distribution.md` 的 manifest 记录：模型 ID/版本、上游固定 commit、权重 SHA256、许可文件、导出脚本与依赖版本、opset/IR、输入输出 schema、预处理版本、最低运行时、已测 EP/硬件、包大小、峰值预算。

## 3. 集成设计：怎么接进现有架构

与 `docs/engineering/S3-ARCHITECTURE.md` 的 worker 设计保持一致，不新增第二套调度：

- **进程与生命周期**：所有模型推理放在独立 `ai-worker.exe`（与 `raw-worker.exe` 同款隔离：QProcess 异步启动、逐行 JSON 控制通道、会话私有临时文件传大 payload、Job Object 兜底、崩溃/超时不提交结果）。AI 关掉后进程退出，OS 回收模型与推理缓存；未安装 AI 包时基础编辑完全可用。
- **运行时**：ONNX Runtime C API 作为 CPU 基线（现有架构已定）；Windows ML 作为可选加速另测，不作为「支持核显」的替代证据。模型/opset/运行时/EP 作为**一套组合**记录，不分别追最新。
- **输入输出契约**：沿用 `ai-distribution.md` 的分割输入/输出契约（job_id、document session、单调 revision、source_node_id、content_hash、geometry_revision、经指定 SDR 映射的 sRGB RGB8 裁片、裁片原点/尺寸、原图↔模型变换矩阵、内存限额与取消令牌）。模型输出**只**映射为可编辑持久化蒙版或独立修饰层，任何羽化/细化都标为独立确定性操作。
- **必须在编辑器侧强制的保证**：AI 只能提供 mask/候选像素；选区外像素由 LuminaS 自己复用原像素，不依赖模型；接受是一个可撤销事务；revision 或几何不符时候选标记过期，禁止自动覆盖。
- **缓存键**：输入内容哈希 + 模型 SHA256 + 预处理版本 + SDR 映射版本；不按文件名，不因 UI 缩放失效。embedding/mask 缓存只按这些键复用。
- **模型分发**：AI 包独立下载，带 manifest 与许可文本；HF/官方直链 + SHA256 校验；不在用户第一次点选时隐式联网下载。
- **包体与内存**：基础 ZIP 仍 ≤120 MiB；模型包大小、常驻内存与峰值单独记账，纳入 `BudgetManager`，不与显示/RAW 预算混算。
- **服务侧（重打光 / 换脸 / 生成式改图）**：若只允许用户装一个服务，选**锁定版本的 ComfyUI**——只有它与既有的 `/upload/image → /prompt → /ws → /history → /view` 路线原生匹配，且重打光（IC-Light）、放大、人脸修复都有节点实现（是核心节点还是扩展，必须在锁定版本上用 `/object_info` 确认后再写进适配器）。轻量备选是 IOPaint 的服务模式（Apache-2.0、没有 workflow 概念、没有版本漂移，但仓库已归档、能力面窄）。FaceFusion 没有稳定 HTTP API（只有 CLI 子命令），**不作为被调用的服务**。
- **首个 AI 包建议内容**：YuNet（检测/5 点）+ MODNet（抠图）+ 可选 MI-GAN（小区域修补）。三者都是 A 档许可、CPU 可跑、总量在几十 MB 量级，正好落在「AI 包独立下载」的分发方式里。

工序级接入点（哪一步调用什么）：

| LuminaS 工序 | 需要模型提供什么 | 现有接口位置 |
|---|---|---|
| 局部选区（点/框、语义） | 主体/人物/头发 mask，可编辑 | 先按 `05-SOL-START` 的选区→蒙版流水线，模型结果只当初始 mask |
| 皮肤优化 | 皮肤 + 五官区域 mask（含眼睛/眉毛/嘴唇/头发排除） | 自研确定性算子吃 mask，不吃模型输出像素 |
| 去瑕疵 | 局部 inpainting 候选像素（笔刷覆盖区） | 修饰层 + 撤销事务；失败可退回仿制图章 |
| 液化 | 面部关键点坐标（68/106/468 点） | WarpNode 位移场，模型只给控制点与区域，形变数学自研 |
| 换背景/人像虚化 | 头发级 alpha + 单目深度 | 作为新图层/局部调整的蒙版源 |
| AI 降噪 / 修复 / 超分 | 整幅或裁片像素候选 | 独立可选包，输出**候选**再由编辑器合成 |
| 局部生成（重打光/改图） | 由本地服务处理 | 复用 `ai-distribution.md` 的生成输入/输出契约与 ComfyUI 适配阻碍清单 |

## 4. 建议的实验顺序与门槛（拟议目标，不是实测）

| 实验 | 最小产物 | 拟议通过条件 | 失败替代 |
|---|---|---|---|
| E-P1 人脸检测与关键点 | `ai-worker` + YuNet ONNX，框与 5 点回映射到原图坐标 | 有许可人像样本上框/点目视对齐；与参考实现数值一致；注意官方训练方案只覆盖约 10×10–300×300 像素人脸，需固定下采样策略；记录 CPU 冷启动、单次推理、峰值内存 | 退回手动框选；不接入模型 |
| E-P2 人脸区域近似蒙版 | 由框/关键点推导皮肤-五官-头发外区域蒙版，可手动画笔增删 | 多样肤色、侧脸、遮挡下有明确漏检说明；蒙版可编辑、可撤销；不声称语义精确 | 纯手动笔刷 + 现有局部蒙版 |
| E-P3 人脸感知液化控制点 | 关键点 → 控制点与保护区域 → `WarpNode` 位移场 | 位移场可重放、冻结蒙版语义一致；与 `01-product.md`「人脸关键点辅助需单独验证」对齐 | 纯手动液化（v0.3 已规划） |
| E-P4 人像 / 头发抠图 | MODNet ONNX（fp16 约 13 MB）+ alpha 细化 | 与手动蒙版对比可量化改善；发丝边界效果如实描述；4K 裁片耗时与内存记录 | 保留 SAM/手动选区；不声明发丝级抠图 |
| E-P5 去瑕疵修补 | MI-GAN（小区域）/ LaMa（大区域）ONNX + 笔刷覆盖区候选合成 | 选区外像素 100% 不变；接受可撤销；失败可退回仿制图章 | 只保留仿制图章/手动修饰 |
| E-P6 AI 降噪可选包 | Intel OIDN + manifest + 独立包 | 与传统降噪同图对比、给出质量/耗时区间；原图哈希不变；包体与内存单独记账 | 不发布该可选包 |
| E-P7 超分 / 人像虚化可选包 | Real-ESRGAN（x4plus / general-x4v3）与 Depth Anything V2 Small | 权重白名单写死；Small 之外的深度模型不随包；记录核显与 CPU 两条路径 | 不发布该可选包 |
| E-P8 人脸修复（B 档可选、默认关闭） | CodeFormer 或 GFPGAN + 完整许可文本 + 显式用户确认 | 输出只作为候选图层；许可与「非商用」限制在 UI 和文档中明示 | 不提供该功能 |

顺序理由：E-P1→E-P3 是所有「按人脸区域作用」功能的前置，而且**不需要部位解析权重**（用关键点几何近似即可开始）；E-P4/E-P5 对应 v0.2 去瑕疵与 v0.3 语义选区；E-P6→E-P8 属可选包，涉及画质与许可承诺，最后做。生成服务侧不重复列实验，沿用 `ai-distribution.md` 的 E-C 与 ComfyUI 适配阻碍清单。

## 5. 未核实与不做承诺

**本轮取证的环境限制**：本机无法访问 `huggingface.co`（含 `/api/models` 与模型卡）以及 `ai.google.dev` / `mediapipe.page.link`。凡是只能由这些域名承载的许可结论，一律标为**未核实**，没有凭记忆补写。原始许可结论来自官方 GitHub `LICENSE` / `README` 原文、GitHub API 的 license 字段或官方文档页。

**待补证据（补齐前不进包、不写进对外说明）**

1. Hugging Face 侧权重许可：IC-Light 权重、OSDFace 权重、Apple DepthPro 权重、RestoreFormer / VQFR / SwinIR / BSRGAN / SCUNet / NAFNet / PMRID / Intel OIDN 的权重条款、`big-lama` 是否附独立许可、SAM 2.1 社区 ONNX 导出来源。
2. MediaPipe 各模型卡许可原文（Face Detector / Face Landmarker / Selfie Multiclass）；目前只有第三方 manifest 标注 apache-2.0。
3. BRIA RMBG-2.0（CC BY-NC 4.0）与 RMBG-1.4（门控非商用）的条款原文与再分发条件。
4. 缺 LICENSE 文件的仓库：Restormer、OSDFace、ELD、nind-denoise、GPEN、DML-CSR，以及 PaddleSeg/PaddleHub 的 face_parse 权重。
5. Real-ESRGAN 的 anime 系列权重是否另有非商用条款；`4x-UltraSharp`（Kim2091）非商用声明的原文。
6. FaceFusion 的许可细则（是否要求授权/水印/署名，`docs.facefusion.io` 本轮多次抓取失败）与 inswapper 需向官方申请授权的页面原文。
7. 工程可行性尚未实测：PP-Matting / PP-HumanSeg 经 Paddle2ONNX 的转换成功率（官方 C++ 示例只覆盖 bisenetv2）、BiRefNet ONNX 在 8–16 GB 核显上的真实耗时、PIPNet 类关键点回归的 RoI 后处理在 ORT 里的实现成本。

**不做承诺**

- 本轮没有任何本机性能数据（冷启动、单次推理、峰值内存、核显加速效果）；不引用论文或第三方跑分当本机承诺。
- 不声称任何模型在肤色、侧脸、遮挡、逆光、密集发丝等条件下的质量；这些必须用有许可样本按第四节的实验实测。
- 本文不构成法律意见；发行前仍按 `docs/02-stack.md`「发布时分别记录应用源码、FFI 库、第三方代码、字体、模型权重、测试照片和训练来源」逐项审查。
