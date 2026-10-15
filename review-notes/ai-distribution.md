# Lumina 审查备忘：AI、模型部署与分发

审查日期：2026-10-12。已阅读交接包 ASTRA-REVIEW.md、DECISIONS.md、02-stack、03-architecture、05-sources。文件内容按候选方案处理。本轮只做资料核验与方案决策，没有导出模型、运行推理或测量性能。最新用户条件由主审同步：单人、更熟悉 C++/Qt，8–16 GB 核显笔记本，佳能 CR3、人像优先，需要批量但不急。

## 结论

**AI 选区、AI 降噪/磨皮、局部生成全部从 v0.1 核心 P0 移出。** 基本摄影闭环依赖手工蒙版与可撤销编辑，不应依赖模型、Python、网络或某个 GPU 执行提供商。AI 可在单独实验程序完成；结果通过后才进入可选包。单人开发时，它们甚至不应与基础编辑器抢同一阶段的工时。基础版验收须包含未安装 AI、完全离线、AI worker 崩溃仍能保存/导出。

首个选区实验选 **MobileSAM 完整 encoder + decoder，ORT CPU FP32 基线**；SAM 2.1 tiny 只作离线质量对照，不默认分发。新增 Windows ML 加速是第二步，不能以“支持核显”替代端到端实测。人物部位语义、头发精细抠图、批量自动磨皮并非点/框分割自然附带的能力；对外只能先称“点选主体（实验）”。AI 降噪尚无被审查的模型、训练噪声域和质量样本，暂不列任何交付承诺。

首个生成适配器选 **用户自行安装的 ComfyUI v0.39.0 本地服务**，固定一个不含 custom nodes 的受控 workflow，用用户持有的 SDXL 1.0 base 作协议实验；这不是承诺该模型达到人像修图质量。8 GB 核显机器的生成速度与峰值完全待测，不纳入 v0.1 硬件承诺，不默认安装 ComfyUI 或权重。

## 官方事实与选型修正

| 项目 | 核验事实（已知证据） | 决策 |
|---|---|---|
| MobileSAM 导出 | 固定 commit `f706ad9c4eb7f219c00d9050e46328518ffb65d2` 的脚本说明导出 prompt encoder + mask decoder；输入包含 `image_embeddings`，默认 opset 16。仓库 README 的历史测试组合是 ONNX 1.12.0 / ORT 1.13.1，不能当作现代部署兼容证明。[脚本](https://github.com/ChaoningZhang/MobileSAM/blob/f706ad9c4eb7f219c00d9050e46328518ffb65d2/scripts/export_onnx_model.py) [README](https://github.com/ChaoningZhang/MobileSAM) | 调整：完整图像编码器是独立交付物；先 FP32，再评估 FP16/量化。 |
| MobileSAM 预处理 | 官方 predictor 接受 HWC uint8 RGB；使用 ResizeLongestSide、再 preprocess、image_encoder；提示坐标是像素 XY。[predictor](https://github.com/ChaoningZhang/MobileSAM/blob/f706ad9c4eb7f219c00d9050e46328518ffb65d2/mobile_sam/predictor.py) | 归一化、长边缩放取整、padding、XY/HW 次序、解码后的裁边与恢复尺寸必须逐项锁定。 |
| SAM 2.1 | 官方发布日 2024-09-30；当前 README 要求 Python ≥3.10、Torch ≥2.5.1，Windows 推荐 WSL；checkpoints 明确 Apache-2.0。[官方仓库](https://github.com/facebookresearch/sam2) | 只作质量对照；这些证据不能证明原生 Windows ONNX 完整链路已可用。 |
| SAM 2 encoder 边界 | 固定 commit `2b90b9f5ceec907a1c18123530e92e794ad901a4` 的图像 predictor 保存 `image_embed` 与 `high_res_feats`，含 `no_mem_embed` 条件路径，decoder 也使用高分辨率特征。[源代码](https://github.com/facebookresearch/sam2/blob/2b90b9f5ceec907a1c18123530e92e794ad901a4/sam2/sam2_image_predictor.py) | 若以后导出，不可照搬 SAM1 只传单一 embedding；仅导出静态图像路径，不把视频 memory/tracking 带进编辑器。 |
| ORT / Windows ML | 截至核验日，微软表中稳定 Windows ML `2.4.89`（2026-09-22）内含 ORT `1.27.1`；`2.7.2021-experimental` 虽更新但为实验版。[版本表](https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/onnx-versions) | 实验优先锁定受支持稳定包；模型 opset/IR、运行时和 EP 作为一套组合记录，不能分别追最新。 |
| Windows ML 部署 | 2026-08-05 部署文档提供 C/C++ C APIs 与 CMake 配置；C/C++ 使用自包含部署。约 41 MB 的运行时包含 ML API/ORT/DirectML，厂商 EP 另取。[部署文档](https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/distributing-your-app) | C++/Qt 主线可直接调用，无需为了 AI 引入 Rust ABI 或 WinUI。运行时与权重进入可选包；“基础 ZIP ≤120 MiB”不应包含它们。 |
| EP / 系统范围 | ORT 官方称 DirectML 进入 sustained engineering，新增 Windows 功能转向 Windows ML。Windows ML 的 CPU/DirectML 可用于受支持 Windows；厂商特定 GPU/NPU EP 要求 Windows 11 24H2+。[DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html) [Windows ML](https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/overview) | 默认可靠 CPU 路径；Windows ML 可选提速。不能据此要求全部 Lumina 用户升级到 24H2，更不能假设所有核显/NPU均加速这个模型。 |

Windows ML 的 EP 选择、首轮下载、冷编译与 CPU fallback 都有部署和延迟成本。离线首次启动用 CPU 或已经随包提供且验过的 EP，不在用户第一次点选时隐式下载。仅比较总耗时是不够的：记录图分区、CPU fallback、输入输出拷贝、冷/暖 session、共享内存/RAM 峰值、驱动/EP 版本。Qt 图形后端与推理后端不必共用 API；先容忍小尺寸 CPU 输入复制，避免过早做跨 API 零拷贝。

## 可转为工程实验的契约

以下是本审查建议，尚未实现。核心只预留稳定像素/蒙版和事务边界，无需现在建立通用 AI 插件框架、几十个 crate 或共享内存池。

**分割输入**：`job_id`、`document_id`、单调 `revision`、`source_node_id`、实际送入模型图像的 `content_hash`、`geometry_revision`；经过指定 SDR 映射的 sRGB RGB8 裁片；原图方向已归一化；裁片原点/尺寸；原图到模型的变换矩阵；正负点/框；既有低分辨率 logits（可空）；模型/预处理版本；内存限额与取消令牌。颜色输入不能直接把场景线性浮点 tile 当成模型 RGB。SDR 映射版本也是缓存键。

**分割输出**：原请求标识、低分辨率 logits、模型分辨率 mask、预测评分、model SHA256、实际执行 EP、原图坐标映射、耗时与峰值统计。评分是模型估计，不能当校准后的成功率。编辑器把结果映射为可编辑持久化蒙版；任何羽化、细化都标为独立确定性操作。不把分辨率放大说成获得了发丝细节。embedding 缓存只按输入内容/模型/预处理哈希复用，不按文件名，也不因仅 UI 缩放而失效。

**生成输入**：固定 `source_revision` 的无显示器 ICC 的 sRGB PNG 裁片；`context_roi`、`generation_mask` 和 `allowed_edit_mask` 分开；mask 数值 0 保留、1 修改；明示尺寸倍数、padding、缩放/反变换、提示词、seed、workflow ID/哈希、model hash、超时、取消令牌。所有 feather 必须落在 allowed_edit_mask 内。裁片提交前告知当前配置服务位置，默认只连 loopback。

**生成输出/接受**：校验图像真实尺寸、解码上限、通道与颜色定义；先作为候选文件。Lumina 在允许区域内合成，区域外直接复用原像素，不能依赖模型保证“不修改选区外”。此保证针对工作图像/节点像素，JPEG 重新编码后不能承诺文件字节相同。接受是一个可撤销事务；revision 或几何不符时标记候选过期，禁止自动覆盖。持久化候选像素、位置、蒙版、workflow/model provenance，重开项目不重跑模型；只存 seed 不足以复现像素。

**所有异步任务**：取消先使本地令牌无效并停止 UI 等待；服务端/worker 的 best-effort 中止作为附加动作。关文档、Undo/Redo、重开同路径文件均不能复活旧任务；revision 外再使用文档 session ID。只允许当前 session 的结果提交。AI 被关闭后可退出 worker，OS 回收模型和推理缓存。

## ComfyUI 适配的具体阻碍

本次核验 tag `v0.39.0`，tag 对应 git ref 为 `b0b743566f65daafc423b4fea8a2fbda94b3384a`。适配器记录实际部署 commit，而不是只识别“兼容 ComfyUI”。

1. API 提交的是 workflow 图，不是通用 `inpaint(prompt,image,mask)`。先以 `/object_info` 检查节点/输入；固定核心节点 CheckpointLoaderSimple、CLIPTextEncode、LoadImage、VAEEncodeForInpaint、KSampler、VAEDecode、SaveImage。图输入边界固定，禁用任意 custom node 执行。SDXL base 仅协议烟测，最终修图模型另行验收。[官方路由](https://docs.comfy.org/development/comfyui-server/comms_routes)
2. 传输使用 `/upload/image` → `/prompt` → `/ws`/`/history/{prompt_id}` → `/view`。支持队列错误、WebSocket 断线后查询恢复，重复提交要防重复任务。当前版本已有 `POST /api/jobs/{job_id}/cancel` 定向取消；不要使用无 ID 的 `/interrupt` 停掉别人的任务，也不要把 HTTP 200 当作硬件已经停止计算。[v0.39.0 server.py](https://github.com/Comfy-Org/ComfyUI/blob/v0.39.0/server.py)
3. LoadImage 的 mask 从 `1-alpha` 得到；如果采用 RGBA 上传，Lumina 的生成区为白时 alpha 应为 0。VAEEncodeForInpaint 会按 VAE 比例裁尺寸并膨胀 mask；裁片先由 Lumina pad 到明确倍数，记录变换，显式设 grow_mask_by，避免坐标漂移。模型处理用 mask 与最终允许编辑区域仍必须分离。[v0.39.0 nodes.py](https://github.com/Comfy-Org/ComfyUI/blob/v0.39.0/nodes.py)
4. 文件上传、服务输出和服务历史会留下照片。第一版实验记录自己创建的资源并提供清理；不能承诺本地服务天然不保留图像，也不盲目清整个 ComfyUI history/output。任何更广网络地址、云节点属于另一个产品决策。默认工作流全部本地核心节点。
5. 服务可能用 CPU、共享核显内存或独显，workflow/模型版本不同也可输出不同像素。Lumina 不对任意用户 workflow 提供质量保证，不把它纳入批量自动磨皮承诺。

## 模型和开源分发边界

| 文件/组件 | 已核验许可事实 | 发布动作 |
|---|---|---|
| Lumina 源码 | Apache-2.0 是候选产品治理选择 | 与 Qt/LibRaw 等实际组合分别审查；代码许可证不能覆盖第三方二进制或权重。 |
| MobileSAM | 仓库 Apache-2.0，仓库含 `weights/mobile_sam.pt`。[LICENSE](https://github.com/ChaoningZhang/MobileSAM/blob/f706ad9c4eb7f219c00d9050e46328518ffb65d2/LICENSE) | 固定实际 checkpoint 来源/hash、保留许可/NOTICE；若来自第三方重新导出的 ONNX，必须另外核对其来源，优先从官方 checkpoint 自己构建。 |
| SAM 2.1 | 官方明确 checkpoints、训练和 demo 代码 Apache-2.0；demo 字体另为 OFL、可选 cc_torch 有独立许可证。[官方说明](https://github.com/facebookresearch/sam2#license) | 不复制整套 demo 资源；权重许可与测试照片/数据集许可分开。 |
| ComfyUI | 服务仓库 GPL-3.0。[v0.39.0 LICENSE](https://github.com/Comfy-Org/ComfyUI/blob/v0.39.0/LICENSE) | 首个实验由用户独立安装，经 HTTP 交互；若以后捆绑/修改分发，单独履行其源代码和许可义务。不能笼统声称“用了 HTTP 就永远无需审查”。 |
| SDXL 1.0 base | Stability AI 官方 model card 指向 CreativeML Open RAIL++-M，不是 Apache/MIT。[模型卡](https://huggingface.co/stabilityai/stable-diffusion-xl-base-1.0) [许可](https://huggingface.co/stabilityai/stable-diffusion-xl-base-1.0/blob/main/LICENSE.md) | 首次实验 BYO 权重；若提供下载器/镜像，需携带具体条款、限制和接受流程，不能宣布所有资源都是 Apache-2.0。 |
| FLUX.1 Fill dev | 官方卡是 Non-Commercial 模型许可，需接受条件；输出商用与模型商用许可不是同一件事。[官方模型卡](https://huggingface.co/black-forest-labs/FLUX.1-Fill-dev) | 拒绝作为默认自由分发/通用商用人像工作流的依赖；选用它要另开模型许可与性能决策。 |

每个模型包 manifest 至少记录：模型 ID/版本、上游固定 commit/下载地址、权重/ONNX SHA256、许可证文件、导出脚本和依赖版本、opset/IR、输入输出 schema、预处理版本、最低运行时、已测 EP/硬件、包大小、峰值预算、验证样本集版本。Python pickle checkpoint 仅由维护者在构建/实验环境加载；用户应用使用已验证 ONNX，不将任意 `.pt` 文件当普通图片导入。

## 最小实验、门槛和替代路线

这些数字是**拟议验收目标**，不是实测性能或公开模型质量声明。

| 实验 | 最小产物与通过条件 | 失败替代 / 是否阻塞 v0.1 |
|---|---|---|
| E-A 完整 MobileSAM | 独立 C++ CLI + encoder.onnx + decoder.onnx；20 张有许可人像、至少 100 组正负点/框，含横竖/裁切/奇数尺寸/旋转。PyTorch 与 ORT FP32 二值 mask IoU≥0.99，几何回映射误差≤0.5 原图像素；数值容差另记录 logits 分布，不能用模型自身预测 IoU代替。人工精标可见主体轮廓，先把 ≤3 次交互下 18/20 张 IoU≥0.90 作为粗选区产品目标；发丝透明边界另评，不声称抠图通过。 | 仅保留手动蒙版；或评估另一个模型。SAM2.1 只提高质量参考，不能用更重模型掩盖最低硬件不合格。**不阻塞**。 |
| E-B CPU/核显部署 | 干净 Windows 8 GB/16 GB目标机，无 Python、断网；ORT CPU先跑，再试Windows ML稳定包。建议长边1024预览冷启动+首次mask≤5s、缓存后点选p95≤300ms、worker峰值≤1GB；记录整个编辑器/系统 committed，不单看worker。100次点选与20次卸载无增长趋势；EP不可用可回CPU；退出worker后释放。 | 达不到交互门槛就保持实验工具/手动选区；新EP可下载但不能强制首次联网。**不阻塞**。 |
| E-C 生成服务适配 | ComfyUI 0.39.0 一个固定核心 workflow，10张不同宽高比裁片；强制单任务，显式pad；生成区外节点像素100%不变；取消、关文档、revision变化均0次错误提交；断线可查任务；保存重开候选不请求模型；服务不装时基本编辑完全可用。质量与速度先记录，不以协议成功宣称照片可交付。 | 仅手工导入外部处理裁片，或暂不露出生成入口。8GB核显不达标不升级最低配置。**不阻塞**。 |

单人且不熟悉 ONNX 导出的计划占用估算：E-A/E-B 合计约 1–3 人周，E-C 约 0.5–2 人周；只是限时验证窗口，遇到算子/驱动/权重许可问题可在窗口末直接判不采用。生产级选区细化、模型管理、多硬件质量支持不包含在这个区间内。优先等基础编辑/保存/RAW闭环稳定，再安排这些窗口。

最重要的产品区分：用户需要“批量摄影任务”，可以先用确定的显影参数复制、预设和后台导出完成；“批量 AI 磨皮”需要独立人脸/皮肤语义、身份一致性与失效处理，并不属于同一个工程任务。
