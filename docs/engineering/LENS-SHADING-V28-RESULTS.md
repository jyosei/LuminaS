# Lens Shading V28 结果报告（LuminaS）

本报告记录LuminaS V28手动镜头暗角校正的实现与实际验证。

## 1. 实现范围

- 手动 `lensShading` 参数：EV 区间 `[-2, 2]`，默认 `0`。
- 严格校验：有限值 / 类型 / legacy 兼容（旧JSON缺少该字段时默认0）。
- 该参数包含在全局色彩预设中；预设中的几何字段仍排除，加载时保留当前构图。

## 2. 算法

- 源坐标径向增益：
  - `nx = (2x + 1 - W) / W`
  - `ny = (2y + 1 - H) / H`
  - `r = (nx^2 + ny^2) / 2`
  - `gain = 2^(amount * r^2)`
- 几何意义：中心处增益为 1；几何四角达到标称 EV；实际像素位于四个角之内。
- 采用通用固定 profile：不依赖已标定的镜头 / 光圈 / 相机，也没有 midpoint 控制项。
- 增益对 RGB 三通道一致应用：保持 alpha、负值、HDR 行为不变。
- 4097 点 float LUT + 插值；坐标除法通过预计算逆尺寸消除。未做受控的性能（速度）结论声明。

## 3. 处理顺序与缓存

- 在 alpha 加权双线性之前，对 SOURCE 邻域像素应用。
- 顺序早于 WB、空间处理、Tone。
- 裁剪 / 旋转 / 透视 / 翻转不会重新居中该增益。
- `preparedGeometry` 包含该校正，随后清除该状态以避免重复应用。
- proxy / last / frame 缓存标识包含 lens 状态；active correction 保留不可变 full source 以服务 4K 目标；display LRU 剥离 full source 引用。

## 4. UI

- 原生卡片滑块：3 位小数显示、reset 按钮，沿用既有紫色主题。

## 5. 构建与测试

- Build 0 错误。
- 领域测试 3 项通过，204 ms。
- 原生测试 4 项通过，40528 ms，2 秒 4 预览 5 次绘制。
- RAW 9579 Window3 通过 56402 ms，3/3。
- JPEG 0705 Window3 通过 49983 ms，4/5。
- 全部 long edge = min(3840, full size)；history / compare / reset / precision 均已具备。
- 最终 undo 恢复 `.24` 并写入 ACTIVE recipe。

## 6. 测试覆盖要点

- 负值 / HDR / alpha / 源数据不变性测试。
- crop 101 用例：源区间 20..80，不与 61 居中的模型一致（校正不随裁剪重新居中）。
- 组合场景：透视 / 翻转 / 角度 / 纹理 / USM mask，融合不超过 1 个 code。
- 已变更的校正在缓存与全新计算下结果一致。

## 7. 已修正与已知偏差

- Worker 的 HDR→SDR 差异断言因裁剪（clipping）失败：GPT 改为验证线性增量 > 0.01 且 midrange SDR 发生变化。
- 既有 neutral tone 仿射浮点舍入约 1e-8；零校正对无阴影基线完全精确。
- 输入 `R=-.2` 容差 1e-7，但不宣称 input identity。
- Worker 的常量 61-vs-101 crop、PNG loader fixture、错误 preset 曝光期望已被修正；未放宽任何算法。

## 8. 探针结果

三次探针 exit 0 / `sourceUnchanged=true`：`.24` lens / `.312345` EV / crop `.1,.1,.8,.8`（无空间处理）。

| 用例 | 尺寸 | full | preview | 峰值私有提交 | 预览/完整输出耗时 |
| --- | --- | --- | --- | --- | --- |
| RAW | 6960x4640 | 5568x3712 | 3840x2560 | 893.109 MiB | 721 / 1399 ms |
| JPEG | 4640x6960 | 3712x5568 | 2560x3840 | 893.227 MiB | 657 / 1433 ms |
| 合成 45MP | 9000x5000 | 7200x4000 | 3840x2133 | 1243.941 MiB | 607 / 1940 ms |

## 9. 认证边界（未认证）

- 工程探针不等同于正式 export UI 验证，也未覆盖 8GB 内存 / 全操作 / 真实 45MP RAW / 色卡认证。
- 30/60 额外项与 p95 未做认证。
- 最终CTest8/8通过，293.40秒：镜头暗角39.62、透视窗口31.93、透视算子.48、分色窗口11.07、分色算子1.02、S2 206.09、S1 .23、S0 2.91秒。启动退出0；12原片SHA校验0变化。
- 私有图像仅由 GPT 本地 QA。

## 10. 会话与委派记录

- DSH session `70a26853-88ce-485f-b66f-74029f38c1c6` 完成 79 次调用 + 10 次 review，66 steps，输入 93375 / 输出 27099 / 缓存 4187520 / 合计 4307994。
- 参数中未出现被标记的照片或密钥引用。
- UNBUILT worker 由 GPT 实际复核。
- 无失败的自动重试；成功的后续（followup）已复核。
- 不涉及资金余额。应用中不含 developer key，也不含在线 worker。

## 11. 后续工作

- 仍待实现：畸变（distortion）、色差（chromatic aberration）、质量（quality）、按显示器 ICC、复杂资源、UI 卡片导航；S3 仍为计划，仅目标处于 active。
- 未来 tile：使用原始 full-source 的 ROI 偏移坐标（而非 ROI 中心），在 prepare/cache key 中携带 lens 状态，并避免重复烘焙（double-baking）。
- 上述本批最终CTest已通过；整个S2仍未完成。



文稿会话968de699-7481-4f9f-9b1e-027086eef86a完成，4步骤input3159/output2910/cache21632/total27701。GPT修正legacy缺省、预设类型、文稿范围和最终结果；计数非余额。实际日志build/private-qa/lens-shading-v28-ctest-final.txt。

