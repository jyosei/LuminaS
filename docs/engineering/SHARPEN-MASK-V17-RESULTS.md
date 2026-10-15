# 独立锐化边缘蒙版

## 概述

本文档记录 V17 已实现的功能与验证结果。所有数据来自给定证据，未新增未验证结论。

## 已实现功能

- **Recipe 字段**：`Recipe.sharpenMask` 取值 `[0,1]`，默认 `0`。
- **JSON 兼容与校验**：旧 JSON 缺省视为默认值；非数字、非 finite、超出范围一律拒绝。
- **UI 与历史**：预设往返、细节卡片 slider 与复位、历史记录均可用。

## 算法（由 GPT 规定并已实现）

- 以几何采样后不可变中性 `RGBA32F` 的 Rec2020 线性 `Y` 构建 alpha 归一 Gaussian 近似低通，`sigma = 2 / sourceScale`。
- dx/dy为相邻低通像素的未归一差分，下面公式按源像素尺度归一。
- `score = clamp(hypot(dx,dy) / (2*scale*0.08*(abs(Ylow)+0.18)), 0, 1)`。
- `m` 为控制量，`t = clamp((score - 0.8*m) / 0.2, 0, 1)`。
- 权重 `= (1-m) + m*t*t*(3-2*t)`；仅乘已有 USM 软阈值后的增量。
- `m=0` 旁路；强度为 0 时不构建边缘基底。
- 该蒙版与阈值/USM 半径独立；阈值不是蒙版。

## 内存占用

最终版本将[0,1]边缘系数round到uint16，读取除65535；源图及其他算子仍float32。边缘基底最多新增 `2 bytes/pixel`：R7 4K 为 18.75 MiB，方形 28.125 MiB，45MP 为 85.83 MiB。按需准备，改 `m` 时复用。不宣称 `0→非 0` 首次切换无准备成本。

## 验证结果

uint16 Qt native 最终 **6 passed / 0 failed / 716 ms**，覆盖：噪声抑制随 `m` 单调、强边缘保留、中性严格等价、尺度 1/2 与 alpha 0.4、透明像素、LCMS ≤1 码值、缓存复用、JSON 预设历史。最终 build 0。

真实 RAW 9579：完整 probe 0、`sourceUnchanged=true`、预览 3840×2560、完整 6960×4640。

## 待完成 / 未实现 / 未认证

- 浮点边缘图版本RAW/JPEG/45MP probe均退出0、源不变，完整尺寸6960×4640/4640×6960/9000×5000；峰值提交1856.52/1857.02/2585.77MiB，45MP超过暂定2.5GiB目标。旧float输出保留为量化对照。uint16最终构建0、三条probe均0，峰值提交1794.77/1795.57/2498.83MiB；4K及完整尺寸保持，三份源不变。六对PNG对照3 passed、7109ms，RAW/JPEG预览/完整最大差1码值，45MP两图差0，alpha完全一致。只认证此配方/样本，不代表全部空间同开或实际45MP RAW。
- 首版RAW真实四控制反馈3 passed、24149ms，每2秒强度12帧、半径7帧、阈值9帧、蒙版12帧；uint16最终RAW/JPEG反馈均3 passed、23326/21836ms；每2秒强度11/8、半径7/5、阈值9/9、蒙版11/9帧；一手势历史/Undo、4K与实际paint核对通过。阈值输入i/4仅10个不同值，9帧不代表吞吐上限。最终完整CTest退出0、3/3 passed、133.33秒（S2 129.72/S1 .26/S0 3.33）。12原片manifest SHA无差异，LuminaS --smoke-test原生启动退出0；最终JPEG/RAW卡片截图已本地查看。默认CTest跳过可选私人PNG对照，以上对照与真实拖动均单独实际运行。
- 未认证或未实现：摄影质量、p95≤100ms、45MP 真实 RAW、8GB 目标机、Alt 灰阶可视化。按用户追加要求，30/60FPS仅额外成就，未达到不阻断验收；拖动持续反馈与精确提交仍验证。

## 参考与协作

- 官方参考：[Adobe Camera Raw Masking](https://helpx.adobe.com/in/camera-raw/desktop/using/sharpening-noise-reduction-camera-raw.html)，仅功能语义参考，非算法等价。
- DeepSeek 有界接线 patch 22597 tokens；首轮测试草稿 1506 tokens 因错误 API/类型未采用；第二轮 2643 tokens 修正顶部格式用例阈值后采用，下半杜撰 Window 接口弃用，由 GPT 补数学与实际 UI 并执行。
- 无照片发送给 API。

委派PNG对照测试草稿1018 tokens（stop）；GPT把缺少参考文件的QSKIP改为QFAIL，避免漏测被跳过。文档草稿1386 tokens，已按实际失败与资源候选修订。
