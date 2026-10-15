# Perspective V27 结果

## 范围

V27 为 LuminaS 手动水平/垂直透视校正。参数域 `-.3 .. +.3`（归一化）。**无物理相机 3D 标定、无 Upright 自动、无自动镜头配置文件**：本版本只做手动投影校正，光学配置文件（畸变/镜头阴影/色差）不在本次范围。

## 算法

归一化逆单应：`normalizeX = 2px/W - 1`，`normalizeY = 2py/H - 1`，`denom = 1 + hX + vY`，映射 `((X/d + 1)W/2, (Y/d + 1)H/2)`，原始全画布中心固定，画布尺寸保持。

- 分母正值有限保护：`> 1e-8`，否则返回无效位置并输出透明像素。
- 采样外包围盒保护在取整（floor）之前完成，避免非法转换。
- 无效/越界像素 alpha = 0。
- 重采样为 alpha 加权双线性；保留 float RGB 余量（headroom）。
- 画布保持原始尺寸/宽高比，变形可能留下透明边缘，由用户手动裁切。
- 裁切存储于"已校正全画布"的归一化坐标；`outputToSource` 现已包含透视，供未来 S3 映射使用。

## 管线集成

Geometry prepare 先做透视，随后清除已烘焙的几何字段再执行 Spatial/Tone，防止重复变形；whole/fused 回退路径与 Spatial cacheRecipe identity 均尊重新 geometry。

四个互不相交的 CPU 重采样行任务 join 后才发布结果。

实时拖拽只计算**4K目标像素**，从保留的不可变全源取样（通常单张图），因此裁切不会掉到 3072 或放大代理；最终精确结果使用同一全源 Recipe。缓存匹配包含 perspective 与 RAW balance/source props；全局控制与中性拾取器读取同一份已 prepare 的 float；点采样器在 replay 前清除 geometry。

透视激活或初始 geometry 拖拽期间共享全源；普通显示 LRU 剥离 fullGeometry 引用；恢复零透视并精确提交时释放缓存属主；非零透视仍保留。可临时重叠副本/任务，**不是硬性进程 RAM 上限**。

Spatial Radius/Grain 度量为"原始尺寸校正画布像素"的名义值，在投影 Jacobian 下并非均匀 raw-sensor 支撑。**不对可变 Jacobian/AA 滤波作画质声明**；更多摄影类边缘/混叠/光晕测试待做。

## UI

NativePerspective 卡片：2 滑块 + 2 数字框及各自重置；小数 3 位，保留其他存储精度；复用全局 history 手势；比较为只读；零交叉/切换陈旧态已测。Whole geometry reset 会连同 crop/angle/flip 一起清除 perspective。Recipe JSON 严格校验：2 个必需有限键 horizontal/vertical，原子拒绝未知/类型错误/缺失/越界；legacy 缺失视为 0。全局色彩预设排除 perspective，加载/混合时保留当前 geometry。紫色 Qt 阶段/卡片/LuminaS/Source Serif 4 均保留。UI 本地 QA 截图仅由 GPT 使用。

## 验证（实际命令与结果）

构建 0 错误 / 启动 0 错误。

Unit 11 passed / 83ms（含 init/cleanup）：严格 JSON；中心；解析平面内部误差 < 2e-6；crop/angle/flip < 1e-5；坏分母；透明隐藏亮绿不泄漏；采样 NaN 拒绝；fused SDR <= 1 code；组合 texture/sharpen/grain 及 geometry 变更后缓存新鲜度相等。

Window 4 passed / 31467ms（含 init/cleanup）：6000x4000 合成 `.crop.8` 在 EVERY ready/live 经断言保持 3840，4 previews、4 paints/2s；精度 `.071234` 另一轴 / `.312345` EV 不变；Undo/Redo、预设保留当前 geometry、比较门禁、零交叉/切换。

真实 R7 RAW 9579 / JPEG 0705 各 3 passed，39749 / 35890ms，4 preview、5 paint 及 4/4 每 2s。最终工件 UI 测试截图重定位仅改测试，无引擎改动。

RAW/JPEG 全探针 3 退出 0 / sourceUnchanged true，实际保存 Recipe：ph(-.12, .09)、crop(.08, .12, .8, .8)、angle -9、Vflip true、无 spatial 操作：

- RAW：源 6960x4640，full 5568x3712，pre 3840x2560，峰值 893.039 MiB，preview 545 / full 1065ms。
- JPEG：源 4640x6960，full 3712x5568，pre 2560x3840，峰值 893.234 MiB，pre 550 / full 992ms。
- 合成 45MP 9000x5000：full 7200x4000，pre 3840x2133，峰值 1243.762 MiB，pre 474 / full 1480ms。

工程探针**非正式导出 UI**；crop 全尺寸差异为有意。**未做 8GB 上限 / 全算子 / 实际 45MP RAW / 色卡合格性验证**。30/60 额外项、p95 未认证。

最终CTest 7/7通过，244.34秒：透视窗口31.89、透视算子.29、分色窗口10.56、分色算子1.01、S2 197.28、S1 .27、S0 3.00秒。12张样片SHA校验0变化；日志build/private-qa/perspective-v27-ctest-final.txt。

## 委派记录

Delegation JSON/tests 会话 `5ab647e1-22a2-46bb-ae41-c525e0555847`，tool 60 cancelled，草稿未完成；51 steps，input 41382 / output 26387 / cache 1775872 / total 1843641。GPT 复核 helper 并修复测试 map 顺序、像素中心、部分 alpha、地平线位置、macro 列表、nan 命名，并补充 mixed cache 测试。

UI 会话 `16ae3504-ffda-412f-9c69-ae9a9602c0ce`：完成初始 18 次调用后经授权复核 3 次（共 21）；19 steps，input 21219 / output 5779 / cache 275968 / total 302966。修正缺失 lambda spin 捕获、错误双重绑定、mixed auto 类型、空行、restore signal blockers；GPT 构建实际产物。

Window 测试会话 `5507bdea-5f7c-4788-a535-24e3bb925a3d`：完成 37 次调用后复核 14 次（共 51）；47 steps，input 55356 / output 16726 / cache 1780864 / total 1852946。GPT 复核返回的修正由 DS 执行：坏 3072 断言、实际源尺寸、预设自加载与 JSON crop 编译错误、等待前滑块释放；GPT 补充全 live ready 尺寸测试。

Workers 未构建，**不接受 worker 的运行声明作为检查**；失败调用不自动重试；允许同会话成功复核。工具参数审计未观察到照片/密钥引用。计数非资金余额。

## 剩余

手动畸变 / 镜头阴影 / 色差；摄影画质 / 每显示器 ICC / 原生 8GB / 复杂资源场景；UI 更大卡片导航。S3 仅为计划。**不声明全部 S2 完成，不声明 67MP 设计**。当前上限 45MP Canon R5 预算，实际材料为 R7。验收状态以上述最终日志为准。

实际检查命令：tools/dev.ps1 build、tools/dev.ps1 test、LuminaS.exe --smoke-test、perspective_tests.exe、perspective_window_tests.exe，以及render_probe.exe读取本地源和保存的配方。工件均在build/private-qa，原片未写入。


文稿会话f3295aae-e25c-49a0-ac23-93f855874b71 completed：4工具/4步骤 input4377/output3084/cacheRead25216/total32677，仅本文写入/读回。GPT修正几何阶段、4K目的地与缓存释放措辞，并填入最终CTest与SHA事实；计数不代表余额。
