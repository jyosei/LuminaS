# Point Color V26 结果报告

品牌：LuminaS。范围：S2 全局 Point Color 工程工作流。V25 的基础实现已被 V26 取代，本报告记录 V26 的实际主窗口（mainWindow）行为。

## 一、UI 与控制器

PointPanel 现为真实原生卡片：样本数不超过 8，含吸管取色、删除、色相/饱和度/明度三个偏移、3 个宽度（widths）、羽化（feather）、范围勾选框（range checkbox）以及重置图标；旧的紫色 stage 卡片与 SourceSerif4 字体被保留。控制器实现放在独立源文件 `window_point_color.cpp`，与主窗口共享 liveRecipe / history；采用一次手势释放（one gesture release）提交语义；Undo/Redo、切换、Esc、取消、对比（compare）等门控齐备。新增样本自动被选中；白平衡（WB）与点取色器（point picker）互斥。

本地异步 float 采样器在 point 前处理 Tone，采用 3x3 alpha ROI，取色参考不从8位显示像素产生；状态在 finished 通知后在取消（cancel）情形下重新启用按钮；具备 source / tag / recipe / generation 四类门控。Qt CPU 实际支持 4K 长边 3840 与较小原图，full source 使用同一 Recipe。应用内部不含浏览器引擎，也不含 DeepSeek key。

## 二、范围视图（Range View）

范围视图使用独立的 `RenderView.pointRange`：取值 -1 表示正常，或为样本索引。包含显式校验：非法负值、越界 bank 索引均被拒绝。渲染顺序为：全局影调、颜色分级与黑白混合完成后、分色前评估范围权重、再点编辑，随后将未选中的 RGB 沿线性 Y 方向去饱和（带选择权重 w），最后叠加暗角（vignette）、颗粒（grain）与 SDR。whole / fused / spatial 递归路径都携带该视图，prep / spatial 基线保持正常模式。后端结果按视图与 tag 门控；普通显示 LRU 只缓存正常模式结果，范围视图不刷新 filmstrip。视图从不写入序列化 Recipe / preset / full source；导出探针默认使用正常模式。

测试：范围勾选状态下保存 preset，验证 pointRecipe 未改变，range / compare 不进入历史。正常灰度 RGB 输出已知：未选中绿 ≤2 code，选中红 chroma >10，取自实际 `RenderResult.image`，而非 Canvas 信箱（letterbox）像素。

## 三、算法修正（GPT实现）

V25 的求和权重归一化在加入中性重叠样本时会削弱已有编辑。V26 改用幅度 `a = max(abs(hueShift), abs(saturationShift), abs(luminanceShift))`；分母为 `max(1, sum(w*a))`，分子为 `sum(w*shift)`；a 为零的条目跳过编辑（但在范围视图中仍可选择），gray confidence / hue wrap / feather 不变。边界 ≤1，过零连续，与顺序无关。单点行为不变。为零条目提供 bit-exact 专用测试，并提供微小位移连续性测试。描述子为 linear Rec2020 HSV、截断 chroma、Y/(Y+.18)，不是感知 HSL，也不是厂商标准； photographic / color chart 质量仍待定。实际最终 build 为 0；最终启动退出0，12张样片SHA校验0变化。

## 四、验证结果

单元测试 15 项通过，662 ms，含 init/cleanup，覆盖范围 fused ≤1 code、source alpha、零条目、连续性、缓存、JSON、preset。窗口测试 4 项通过，10304 ms，含 init/cleanup、2 秒内 7 次预览 7 次绘制。实际 RAW 9579 窗口测试 3 项通过 20327 ms，5 preview / 5 paint；JPEG 0705 窗口 3 项通过 18194 ms，6/6。真实照片测试仅省略合成的红/绿像素断言，完整 history / range / preset / compare 链路继续执行，无 QSKIP。真实照片 UV 测试网格只从正常显示中选择可见颜色坐标以决定点击位置；实际颜色参考仍为 float 后端。此前固定坐标 RAW 测试停在等待并被主动结束，早期日志未完整定位具体取样失败阶段，不计为通过；增加取样错误断言并改用场景坐标后通过。合成与私有 JPEG 版面截图由 GPT 本地查看，未经 worker 图像 QA。

完整探针 3 个，exit 0，sourceUnchanged true，含实际 2 个样本 Recipe（一个已编辑、一个为零），由 Window 在 range 关闭后保存；无空间算子：RAW 6960x4640 → pre 3840x2560，峰值 745.172 MiB，pre 2074 / full 1120 ms；JPEG 4640x6960 → pre 2560x3840，峰值 744.785 MiB，pre 2228 / full 1299 ms；合成 45MP 9000x5000 → pre 3840x2133，峰值 1036.266 MiB，pre 1719 / full 1950 ms。探针属工程验证，不是正式导出 UI，也未做 8GB / 全算子 / 真实 45MP RAW 认证。30/60 FPS 为额外项，非硬门槛，p95 未认证。最终 CTest 5/5通过，221.41秒：窗口11.38、算子.90、S2 205.31、S1 .29、S0 3.48秒；日志为build/private-qa/point-color-v26-ctest-final.txt。

## 五、已修正的失败

GPT 在 EditLoader 分配之前挂载 panel，导致无效信号连接与崩溃；现改为先创建 loader、初始化指针，build 与原生测试均通过。取消采样任务后按钮未刷新，现改为 busy 为 false 后无条件本地状态通知。worker 控制器状态 cancelled：tool 60、session 478fb657-0f21-4d14-a0a9-c3e54d8fd859，42 provider steps，input 38980 / output 11820 / cacheRead 1074560 / total 1125360；该草稿未成功交付；GPT 修正了门控、指针、正常模式回调与 recipe 校验。worker 窗口测试 session b5568d1f-68dd-418e-b55d-a9605af0edcc 完成 59 次调用 / 51 steps，input 49257 / output 14828 / cacheRead 1960064 / total 2024149，UNBUILT；GPT 修正了 show window、以 readRenderImage 取代抓取信箱像素、自比较 WB 断言、缺失的 cancel、void API 断言以及真实 QSKIP / 固定坐标问题。工具参数审计未发现被标记的照片 / key 引用（非绝对隐私保证）。失败的 worker 不自动重试。GPT 负责实际 build / 测试 / 最终集成。Token 统计不代表 RMB 余额。

## 六、结论与遗留

S2 全局 Point Color 工程工作流已在上述范围内实现并验证；剩余的 manual optics / perspective / photographic quality 与更大范围的 S2 审计仍未完成，S3 仅处于准备阶段，当前目标未完成。S3 的瞬态 view / job / cache 标识，以及全局描述子与幅度归一化，必须延续到 Tiles；S3 生产工作尚未开始。不得声称全部 S2 完成或与商用算法完全对齐。[Adobe官方交互参考](https://helpx.adobe.com/ie/lightroom-classic/desktop/process-and-develop-photos/color-mixer.html)，仅支持交互特性，不代表本项目数学实现。


文稿会话4fe239aa-6ef2-4133-8499-a9c8603fee7c完成：3次工具/4步骤，input3815/output3189/cacheRead23424/total30428；仅本文写入/读回。GPT已修正控制器文件与颜色偏移措辞、范围运算阶段、早期RAW等待原因未证实的表述、启动/SHA与最终CTest。用量不是余额。
