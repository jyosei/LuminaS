# LuminaS GPU 加速实验第一轮结果（首轮关闭，结论：继续研究）

2026-10-15。本文记录 GPU 加速实验计划第一轮实际执行的设备、命令、数值与失败，结论为**继续研究**，不是"G0–G5 全部完成"，也不构成产品接入验收。计划与验收标准见 [GPU-ACCELERATION-EXPERIMENT-PLAN.md](GPU-ACCELERATION-EXPERIMENT-PLAN.md)（本文只改该文档首段状态，编号正文保持原样）。

## 1. 首轮关闭状态（按计划阶段逐项对照）

| 阶段 | 本轮实际状态 | 说明 |
|---|---|---|
| G0 基线与能力检查 | 部分完成 | 已记录实际设备/驱动/D3D11 能力，并采集受限 CPU 基线；未覆盖全场景基线 |
| G1 基础调色 | 部分完成（仅基础融合） | 仅实现曝光／RGB 平衡／对比度／饱和度的融合版本；曲线与 HSL 未实现，分步与融合对比未做 |
| G2 常驻预览 | 部分完成（隔离路径） | 仅隔离的 native/4K 基础路径通过，且仅 fixed sRGB；计时端点存在局限（见第 5 节） |
| G3 蒙版与局部合成 | 未实施 | 无实现、无外部验证 |
| G4 空间处理 | 未实施 | 无实现、无外部验证 |
| G5 跨设备总结 | 未实施 | 无 NVIDIA 实机结果，不宣称 A／N 兼容认证 |

明确：没有 24MP／45MP 的 GPU 实测，也没有真实照片的 GPU 实测；本地 4K 往返已因不足 20% 收益而停止，6 GiB 级机器未采纳完整源图 GPU 路径。这些缺口不得标注为"通过"，也不得声称已覆盖计划中的全部方法。

**结论：继续研究。**常驻（resident）4K 路径有潜力；CPU↔GPU 全往返路径暂不迁移；生产默认路径不变（实验构建选项默认关闭）。

## 2. 环境与能力事实（G0）

- Qt 6.8.3，MSVC，C++20。
- AMD Ryzen 5 5500U，12 个逻辑核心。
- GPU：AMD Radeon(TM) Graphics，deviceId 5708，vendor 4098；驱动 31.0.21912.3005，驱动日期 2024-06-06。
- 系统可见物理总内存 6297477120 字节（约 5.86 GiB），不是测量时的空闲内存。
- D3D11 feature level 0xb000；compute、RGBA32F sample/storage、timestamps 均为 true；最大纹理尺寸 16384。
- 新增构建依赖 qsb（QtShaderTools 6.8.3），经 aqt 官方 Qt 模块安装，并固定 Qt 私有 API 用法；仅在实验选项开启时需要。

## 3. 本轮产物与构建

- 实际新增文件：`tools/gpu-probe/main.cpp`、`presentation.inc`、`basic.comp`、`present.vert`、`present.frag`、`tools/run-gpu-experiment.ps1`；`CMakeLists.txt` 新增默认 OFF 的 `LUMINA_BUILD_GPU_EXPERIMENT` 选项。
- 配置：`cmake --preset dev -DLUMINA_BUILD_GPU_EXPERIMENT=ON`；构建：常规 `tools/dev.ps1` build。
- 构建日志：`gpu-configure.txt`（首次配置）、`gpu-build-final.txt`（最终 exit 0）。
- 采集产物不入库：仓库内不提交本轮生成的实验产物与截图。

## 4. 命令与实际计时

最终命令（均实际执行）：

- `tools/run-gpu-experiment.ps1 -ReportDirectory build/private-qa/gpu-final-small -Size small`
- `tools/run-gpu-experiment.ps1 -ReportDirectory build/private-qa/gpu-final-4k -Size 4k`
- `tools/run-gpu-experiment.ps1 -ReportDirectory build/private-qa/gpu-final-native -Present`

上述三次均为 5 次预热后 100 次测量，实际 exit 0；墙钟耗时分别为 1258 ms、46627 ms、14294 ms。

样本：全部为合成数据，257×131 与 3840×2560 两种尺寸的 float32 Rec.2020，含负值／HDR／alpha 0／近零值／0.25 等取值；四种配方为 identity、mixed、desat、dark/bright 极端。

## 5. 数值正确性（G1）

- 容差：RGB 满足 `绝对误差 ≤ 1e-5 + 1e-4×|参考值|`，alpha 误差 ≤ 1e-6。
- small 与 4k 两组、四种配方全部用例：通过 4／4，失败 0；最大 RGB 绝对误差 4.76837158203125e-7，最大 alpha 误差 0。
- 未覆盖：曲线与 HSL 算子、分步与融合的对照、G3/G4 的蒙版与空间算子。

## 6. 性能数据（4K，合成样本）

- CPU `renderLinear`：中位 174.045 ms，p95 194.8337 ms。
- GPU 上传＋计算＋同步＋读回：中位 199.4766 ms，p95 229.8956 ms（比 CPU 慢约 14.6%）。
- 常驻（resident）仅计算：中位 11.0432 ms，p95 15.7647 ms。
- 上述常驻仅计算数字不含第 7 节的呈现路径，不是端到端显示延迟。

## 7. G2 隔离呈现路径

- 既有 CPU 融合 `renderFrame` 到 `paintEvent` 绘制完成：中位 86.7564 ms，p95 96.0226 ms。
- GPU 路径滑块更新→QRhiWidget.frameSubmitted：中位 19.0148 ms，p95 24.7614 ms，100 个采样。
- 两端点定义不同，明确**不能**当作物理意义上的输入到光子／扫描输出延迟，也不能作为 FPS 认证。

- 输入只上传 1 次；共渲染 110 帧（含首帧、预热、采集与 100 次测量）。
- native 路径为 fixed sRGB：使用 LCMS 生成的矩阵＋65536 项 8bit 编码表；3840×2560 实际读回与 CPU 对比最大 1 字节差，超过 1 字节的通道数为 0，premultiplied alpha 保持。

## 8. 内存与资源

- 4K 往返：峰值 private 1436913664 字节，峰值 WS 1002184704 字节，结束时 DXGI local usage 647393280。
- native GPU 释放前：private 906457088，DXGI local 478941184。
- 整段顺序执行的 native 进程：峰值 private 1057148928 字节，峰值 WS 653795328 字节（包含 CPU 参考与读回，不是 GPU 单独占用）。
- DXGI UMA 的 local／non-local 不是物理独显／共享显存分配峰值，且只是最后一段的快照。

## 9. 已知失败与修复记录（均保留）

- 首次缺少 qsb，后经 aqt 安装 QtShaderTools 解决。
- 编译期 NOMINMAX 与 QCommandLineOption 修复。
- wrapper 的 `Split-Path -LiteralPath` 与 `-Parent` 参数错误，修复后实际运行成功。
- G2 初期显示检查器双重预乘、以及资源生命周期崩溃（QRhi 资源比 QRhi 窗口存活更久），已修复。
- 最终一轮 exit 0。

## 10. 工作器与审计

- DSH wrapper 草稿会话 `5d8c1a67-98f8-4b5f-af77-8566554e4df3` 已审计 9 个 provider 步骤：输入 6771／输出 5228／缓存读取 70784；期间读取了 2 个未在批准范围内的公开脚本 `tools/check-native.ps1`、`tools/run-optimized.ps1`，未发现读取私人照片或凭据。
- 该草稿由 GPT 复核、修正并实际运行。直接 API 复核尝试因包含非法参数范围被拒绝，未产生任何复核结果，也未自动重试。
- C++ 着色器与核心实现由 GPT 编写；工作器仅产出 wrapper 草稿。
- 本文草稿会话 `e4638bd9-f559-4c9b-af8d-cfadf8a40ab8` 实际审计 8 个 provider 步骤：输入 10274／输出 4250／缓存读取 89856。除指定文档读写外，多列举了工程文档目录名；未发现读取私人照片或凭据。GPT 根据实测结果纠正了内存口径和适用性推断。

## 11. 与 S3 的关系

S3 从 A0 的 CPU 正确性基线开始；GPU 未被采纳为 S3 的生产依赖。本轮结论不改变 S3 的 CPU 路径与顺序。

## 12. 状态引用

本文为 GPU 实验的状态引用来源；计划文档首段状态与本文第 1 节保持一致，计划编号正文未改动。

## 13. 遗留与后续条件

- 曲线／HSL、分步与融合对照、G3 蒙版与局部合成、G4 空间处理、G5 跨设备对照均未开始。
- 常驻 4K 路径需要完整色彩与呈现验收后才有资格重评；CPU↔GPU 全往返路径在本机 4K 已判不足 20% 收益。本轮据此停止扩展整图往返；未测 24MP／45MP，不能推断其实际性能或兼容性。
- 无 24MP／45MP 与真实照片 GPU 数据；后续如重启实验，须另行取数，不能沿用本轮结论。
