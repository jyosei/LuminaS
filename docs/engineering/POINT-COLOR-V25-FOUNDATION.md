# LuminaS V25 精确分色基础（POINT-COLOR-V25-FOUNDATION）

历史基础层记录：主窗口/范围视图现已按[POINT-COLOR-V26-RESULTS](POINT-COLOR-V26-RESULTS.md)实现，V26按编辑幅度归一也替代本批原始重叠规则；以下保持V25当时的实际状态。

> 范围说明：本文只描述 **V25 精确分色基础**（数据结构、CPU 共享模型、面板组件、异步取样、验证现状）。
> 这是**基础层报告**，**不是已完成的终端用户功能**：尚未挂载到 `mainWindow`，尚无范围渲染（range-render），
> 也**未通过 4K 拖拽反馈认证**。既有广义目标仍然有效（existing broad goal active），当前仍在补齐 **S2**，**S3 仅准备**。

---

## 1. 参考实现（仅商业交互参考）

官方交互参考（仅用于佐证取色器 / HSL 范围 / 可视化这类**商业软件功能形态**，不代表 LuminaS 算法来源）：

- [Adobe Lightroom Classic — Color Mixer](https://helpx.adobe.com/ie/lightroom-classic/desktop/process-and-develop-photos/color-mixer.html)

该链接**仅支持商业取色器/HSL 范围/可视化功能形态的对照**，**不构成 LuminaS 算法的依据**；
LuminaS 的数值与色彩处理决策由 GPT 负责，本文不据此推导任何算法结论。

---

## 2. 已实际实现内容

### 2.1 全局分色调整向量

- 最多 **8 个** 全局 Point Color Adjustment 向量（`max 8 global PointColorAdjustment vectors`）。

### 2.2 十个有限字段

每个向量包含 **十个有限字段**：

| 字段 | 取值范围 |
| --- | --- |
| center hue | `[0, 360)` |
| center saturation | `[0, 1]` |
| center luminance | `[0, 1]` |
| width hue | `[1, 180]`（度） |
| width saturation | `[.01, 1]` |
| width luminance | `[.01, 1]` |
| feather | `[.01, 1]` |
| shift（色调/饱和/亮度类位移） | `[-1, 1]` |

（十个字段为：hue/saturation/luminance 三个中心 + hue/saturation/luminance 三个宽度 + feather + 相应的 shift 分量，合计十个有限字段。）

### 2.3 严格 JSON

- **严格 JSON**：要求十个字段齐全；**未知字段 / 缺失字段 / 类型错误 / 容量超限一律拒绝**；
- **原子解析（atomic parse）**：解析失败不产生半成品状态；
- 旧数据中**不存在的 pointColors 视为空**（legacy absent → empty）。

### 2.4 CPU 共享模型位置

- 共享 CPU 模型插入位置：**完整 color/BW 分支、grade 及黑白混合之后**，
  在共同的 Tone 尾部执行，位于 vignette / grain / SDR 之前。

### 2.5 描述子（descriptor）定义

- **HSV hue** 取自 `linearRec2020` 工作 RGB；
- `S = clamp(delta / max(abs(max), .18), 0, 1)`；
- `L = Y / (Y + .18)`；
- 要求 `Y > 0` 且 `chroma > 1e-7`。

上述描述子**不是感知均匀 HSL**，也**不是标定过的 sRGB / 物理相机标准**。

### 2.6 权重与位移行为

- 权重为：围绕**圆环色相（circular hue）**与**饱和/亮度中心**的**软平滑阶跃（soft smoothstep）乘积**；
- 灰色置信度 `gray confidence = S`；
- 相同输入下的**重叠锚点与顺序无关**（order independent）；
- 加权位移按 `max(1, sum weights)` 归一化；
- 色相 ±60°，饱和度 delta 乘子 `1 + shift` 且**保持 max/min 中点（midpoint held）**，亮度 ±1EV；
- Alpha 与 source **保持不变**；
- 黑/灰被绕过（black/gray bypassed）；
- 负值 / HDR 在 float 中被保留，SDR 按通常方式裁剪；
- 中性色（neutral）**精确旁路（exact bypass）**。

### 2.7 预设强度

- 预设强度采用**固定选择锚点（fixed selection anchors）**，base/target 匹配到的出现位置位移渐隐；
- 超过 8 个的并集仍然拒绝（unchanged）；
- 端点与相同快照保持精确一致（endpoints and identical snapshots exact）。

### 2.8 新增异步 EditLoader 取样 worker

- 使用**不可变 prepared float 4K 快照**；
- 走与预置点相同的全局/空间路径，但**排除 point / vig / grain**；
- **alpha 加权 3x3**；
- 包含翻转（flips included）；
- 仅处理 ready 请求；
- 同时只有**一个 busy job**；
- 对 stale 的 source props / tag / recipe / generation 做门控；
- 取消操作**使 generation 失效**，但不强制中止计算（without force aborting computation）；
- 析构函数 join。

### 2.9 面板组件

- Worker **PointColorPanel** 为独立可编译组件，含列表、取色器/删除、7 个 shift/range/feather 控件与范围复选框信号；
- 该面板**未挂载到 mainWindow**；
- 实际范围图像**尚未渲染**。

---

## 3. 明确未完成 / 不作出的声明

- **不是已完成的终端用户功能**；
- **尚未接入 `mainWindow`**；
- **尚无范围渲染（range-render）**；
- **未通过 4K 拖拽反馈认证**（no 4K drag feedback certification）；
- 无 30/60 硬门限，也未认证 p95；
- 取样 worker **不采样编码后的显示图（encoded display）**，也**不上传私人照片**。

---

## 4. 评审与视觉 QA 现状

- GPT 已复核并修正：范围状态（range-state）、上限 8、删除释放、精度/门控；
- **视觉 QA / mainwindow 反馈尚未完成**。

---

## 5. 验证现状（实际记录）

- 最终构建退出码 **0**，原生启动退出码 **0**；
- 最终原生测试 **13 passed / 462 ms**（含 init/cleanup），覆盖：
  JSON 范围 / 旧数据 / 缺失字段 / 未知字段 / 原子性；色相环绕 / feather / 中性 / 顺序 / 负值与 HDR / alpha / source 不可变性 / fused LCMS ≤ 1 code；预设混合一致性 / 并集失败保持不变；异步 busy / cancel / stale job / flips；面板在精确其他字段 / 恢复范围 / 单手势完成 / 删除 / 空 / 上限。
- 早期失败已修正：worker 红蓝测试索引写反；GPT 的 PNG 夹具不受支持改为 JPEG；副作用 `QTRY` 重新评估，启动请求改为只读 `canSamplePointColor`；Qt metatype 顺序已修正。
  **这些测试/构建失误不构成任何算法缺陷结论。**

### 5.1 三次完整探针

3 次完整探针均 exit 0、`sourceUnchanged` 为 true，recipe 仅含一个 point + exposure，无其他空间算子：

| 场景 | 尺寸 | 预览 | 峰值私有提交 | pre | full |
| --- | --- | --- | --- | --- | --- |
| RAW | 6960x4640 | 3840x2560 | 745.254 MiB | 1958 ms | 1055 ms |
| JPEG | 4640x6960 | 2560x3840 | 744.711 MiB | 2024 ms | 1166 ms |
| 合成 45MP | 9000x5000 | 3840x2133 | 1036.051 MiB | 1579 ms | 1355 ms |

- 上述为**工程快照，不是受控基准**；
- **未做 8GB / 全空间算子 / 真实 45MP RAW / 色卡认证**。

### 5.2 CTest

- 首次 CTest：**4/4 passed，203.49 s**（在异步/面板改动之前）；
- **最终 CTest 4/4通过，201.53秒**：point_color .56秒、S2 197.25秒、S1 .25秒、S0 3.44秒；
- 最终构建与原生启动退出0；12张私人样片SHA校验0变化。实际日志：build/private-qa/point-color-v25-ctest-final.txt。

### 5.3 工作会话用量

- Worker JSON/测试会话 `955a0b23-0ee3-4ca6-ba49-55e44600d7a1`：completed，38 calls / 33 steps，input 18207 / output 20177 / cacheRead 732160 / total 770544；
  曾读取首个 allowlist 之外的 `cpu_renderer.h`，未观察到图像/密钥引用。
- 面板会话 `95d2af25-f404-47f4-8cb2-6c5bb94976ab`：completed，22 calls / 22 steps，input 11028 / output 13607 / cacheRead 352128 / total 376763，零私密照片/密钥引用标记。
- Workers **UNBUILT**；实际构建/测试**仅由 GPT 执行**。
- 上述计数**不代表 RMB 余额**。

---

## 6. 后续即时事项

- 在 `mainWindow` / 共享历史中挂载面板；
- 互斥的 WB / Point 取色器，以及 Esc/取消；
- 范围预览为**临时视图**（transient view），含 job identity；显示缓存只收普通预览，范围图保持临时，
  **绝不写入持久化 Recipe / 导出**；
- 真实 4K 拖拽反馈、精确释放、Undo/Redo、切换与 stale 处理，以及视觉 QA；
- S2 手动光学 / 透视 / 摄影质量仍待完成；S3 仅架构规划。
- **不得把整个 S2 标为完成，也不得宣传占位控件。**


文档会话7f0fc5a5-ba23-47e2-b3ab-b59fbbd6a23b完成，3次工具/4步骤，input3802/output3671/cacheRead24192/total31665；仅写本文并读回。GPT修正Tone阶段、S2/S3措辞、临时视图缓存要求和最终日志；命令中官方URL导致普通审计标记，不是照片/凭据读取。


