# 单色强度 (Monochrome Strength) V22 结果报告

范围：本文件仅记录 S2 基础层结论。**TOTALPRESET STRENGTH 尚未实现**，本文为其后续工作提供基线。

## 1. 配方与范围语义

- `monochromeStrength` 取值范围 `0..1`，默认值 `1`。
- 旧版 JSON 在缺省该字段时同样按默认 `1` 处理（含 legacy JSON）。
- 禁用黑白（disabled BW）时**保留字段值**，不做清零或移除。
- 严格校验的 JSON（strict JSON）与自定义预设的保存/读取均被接受，往返一致。

## 2. UI 行为

- 微调框与滑块按黑白启用状态、对比（compare）与裁剪（crop）状态进行守卫。
- 普通历史记录（ordinary history）合并为单次手势（one gesture）。

## 3. GPT 算法要点

- Tone 在 primary / tonal / luma 处理链路上共享。
- 部分强度时：分别计算
  - 完整彩色分支：饱和度/鲜艳度 + RGB 曲线 / HSL / 分级；
  - 基于转灰前颜色的黑白分支：混合 + 曲线 / HSL / 分级；
  然后在 **RGB 线性空间**混合（mix RGB linear）。
- alpha 通道保持不变。
- 强度为 0 时结果**精确等于**彩色结果；强度为 1 时结果**精确等于**旧的黑白分支。
- 不引入两份整图缓冲区（no two full-image buffers）。

## 4. 数学与数值验证

- 抽样覆盖：负值、超白（superwhite）、alpha、曲线、HSL、分级、暗角（vignette）。
- 部分强度下 RGB 期望端点为混合（endpoint blend），容差 `2e-6`。
- 融合 LCMS 差异 `<= 1` code。
- 非法 JSON、默认值 1、预设往返（preset roundtrip）均通过。

## 5. 构建与测试实测

| 项目 | 结果 |
| --- | --- |
| Qt 6.8.3 MSVC 构建 | 0（成功），冒烟测试 0 |
| 12 张照片 SHA | 0 处变化 |
| 初始 native 测试 | 4 通过，9544 ms |
| 合成 4K | 7 次预览，8 次绘制 / 2 秒 |
| 最终模型（含预设往返） | 3 通过，71 ms |

### 真实素材

- RAW 9579：native 3 通过，20274 ms，4 次预览，4 次绘制 / 2 秒。
- JPEG 0705：native 3 通过，16429 ms，4 次预览，5 次绘制。

### 通用断言

- 所有就绪预览长边为 3840。
- 释放（release）、撤销/重做（Undo/Redo）精确成立；切换后数值保持一致，模式门控（mode gate）通过。

## 6. 全尺寸源探针

全部退出码 0，且源文件未变（sourceUnchanged）：

| 素材 | 预览 | 峰值内存 |
| --- | --- | --- |
| RAW | 6960x4640 → 3840x2560 | 744.914 MiB |
| JPEG | 4640x6960 → 2560x3840 | 744.664 MiB |
| 合成 45MP | 9000x5000 → 3840x2133 | 1036.313 MiB |

说明：该配方不含空间算子（no spatial operators），因此**不能**与 V19 空间版本的 2498 MiB 直接比较。此外，尚无 8GB 真机 / 真实 45MP RAW 的认证结论。

## 7. 未完成与遗留

- 最终 CTest 3/3 通过，总计171.57秒：S2 167.87秒、S1 0.29秒、S0 3.39秒。
- 截图由 GPT 在本地检查。
- DSH 会话记录：`8b1d5d75-5f72-4b23-982e-9486f8fd43a8` 已完成，实际仅改动 4 个允许的源文件；审计显示 10 次调用、8 个步骤，输入 12631 / 输出 3580 / 缓存读取 84992 / 合计 101203，未涉及私人照片或密钥引用；已由 GPT 复核并测试。
- 后续预设强度（preset strength）需要基线上下文与正确的曲线插值。
- 颗粒（grain）、Point Color、光学（optics）、质量（quality）仍待完成。
- S3 仍仅做准备工作；30/60 FPS 属额外成果，不是门禁指标；p95 未获认证。

## 8. 结论

S2 基础层已落地并通过上述检查；TOTALPRESET STRENGTH 未实现，需在具备基线上下文与曲线插值方案后另行开展。

文字报告任务 c2865b1a-941d-47e7-a6aa-327fa234edb0 completed，实际3次pwsh仅操作本报告，4个provider步骤input2740/output2218/cacheRead19584/total24542。GPT校正控件门禁措辞、鲜艳度/黑白分支术语及最终回归状态；未提供图像/凭据，token统计不代表余额。
