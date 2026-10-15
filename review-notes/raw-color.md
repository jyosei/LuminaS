# Lumina RAW 与颜色独立审查

审查日期：2026-10-12。已读 ASTRA-REVIEW.md、DECISIONS.md、docs/02-stack.md、03-architecture.md、04-roadmap.md、05-sources.md。交接包是候选方案；本文没有运行生产引擎、RAW 显影或性能测试。所有验收数值均为建议目标，不能写成已有能力。

补充约束来自主审转达：单人开发，C++/Qt 经验多于 Rust；8–16 GB 核显笔记本；主要 Canon CR3；人像与批量必须。相机确切型号尚未提供。

## 决策结论

**采纳 LibRaw；拒绝首版自行发明完整专业 RAW 显影。调整为：复用锁定版本的成熟解码与现有显影路径，先做一个实际机型、若干确切拍摄模式的人像闭环。** 先验证再决定是否把 RAW 列为 v0.1 已认证功能；“能打开 CR3”不是颜色、肤色、高光与细节通过验收。

默认 P0 从 LibRaw 0.22.2 的 dcraw 显影做基线，比较已有 AHD/DCB 等算法；不使用双线性结果冒充可交付画质。完整 RAW 文件与参数必须保留；修改 RAW 白平衡/源曝光必须重走显影阶段。16-bit、gamma=1 的输出可以作为**有限动态范围的线性 RGB 渲染结果**进入浮点编辑器，但不能宣称从传感器到导出保留了全部负值和超白范围。

可复现基线的参数起点（用于实验，尚不是生产预设）：`output_bps=16`、`gamm[0]=gamm[1]=1`、`output_color=8`（Rec.2020）、`no_auto_bright=1`、`use_auto_wb=0`、`use_camera_wb=1`、`user_qual=3`（AHD，对照4/DCB）、`no_auto_scale=0`（保留显影需要的缩放/WB阶段）。显式记录 `use_camera_matrix`、`adjust_maximum_thr`、`highlight`、WB fallback、输出crop/orientation及原生库构建特性；不能盲目套一套常量到所有机型。`adjust_maximum_thr=0`会停掉自动最大值修正，但可能要求正确的传感器白点，否则高光偏色，所以它是对照项而非通用修复。源曝光使用其显影阶段曝光能力时，官方参数文档给出的 `exp_shift` 范围是 0.25–8（约−2至+3 EV）；范围外不应悄悄以 RGB 曝光代替并继续声称同等 RAW 恢复能力。[参数定义](https://www.libraw.org/docs/API-datastruct.html)（访问 2026-10-12）。

这个起点的优点是白平衡、去马赛克、矩阵和整数输出全部留在同一个已经存在、可复现的处理链中；代价是LibRaw内部已有的截断仍存在。今后转为全float RAW处理时属于显影引擎版本升级，必须保留旧项目重现路径，不能仅把输出类型从u16改f32就宣布升级完成。

证据：LibRaw 官方明确定位于 RAW 读取，附带基本 dcraw 转换，生产级渲染不在其目标内；0.22.2 的 scale_colors_loop 与 convert_to_rgb_loop 有 CLIP 写回 ushort。因此“自研全部”和“LibRaw 默认结果就是专业显影”都不应成为既定方案。[LibRaw 定位与许可](https://www.libraw.org/about)、[0.22.2 实际处理源码](https://raw.githubusercontent.com/LibRaw/LibRaw/0.22.2/src/postprocessing/postprocessing_utils.cpp)。

若基线的肤色、高光或细节无法通过主要用户照片验收，优先顺序是：①采用用户可接受的成熟 RAW 软件转 16-bit TIFF 交接，诚实地把内建 RAW 留在实验状态；②若内建高质量 RAW 必须首发，则重审 GPL-3.0 路线及 librtprocess/既有引擎复用，并延后首发；③最后才考虑独立实现许可清晰的算法。不能以复制 GPL 算法后增加 worker 进程来自动规避许可义务。这个取舍会影响首发定位，需要明确记录。

值得自己写的内容是产品特有的交互和编辑算子：参数映射、可预测曝光与曲线、基础色彩调整、局部蒙版、普通透明合成、裁切/旋转、批处理配方、缓存调度、检验工具。首版不自研去马赛克创新算法、神经网络降噪、复杂去紫边、镜头资料库、DCP 全兼容、高光重建算法。实现已有公式和原创算法研发要分开估算。

## 官方事实与版本锚点

| 事实 | 核对结果 | 官方一手来源 |
|---|---|---|
| LibRaw 当前稳定版 | 官网及 release 列表为 0.22.2；发布日期 2026-07-16；0.22.0 为 2026-01-13 | [Release](https://github.com/LibRaw/LibRaw/releases/tag/0.22.2)、[0.22 发布说明](https://www.libraw.org/news/libraw-0-22-0-release) |
| LibRaw 许可 | LGPL-2.1 或 CDDL-1.0 双许可，按选择与实际构建履约；不能把应用 Apache 许可当成依赖许可证 | [About](https://www.libraw.org/about)、[0.22.2 COPYRIGHT](https://raw.githubusercontent.com/LibRaw/LibRaw/0.22.2/COPYRIGHT)、[LGPL 文本](https://raw.githubusercontent.com/LibRaw/LibRaw/0.22.2/LICENSE.LGPL) |
| 解码与显影不同 | open/unpack 可取 RAW；dcraw_process 包含黑电平、缩放/白平衡、去马赛克、颜色转换等；make_mem_image 为最终输出阶段 | [API 概览](https://www.libraw.org/docs/API-overview.html)、[0.22.2 显影流程](https://raw.githubusercontent.com/LibRaw/LibRaw/0.22.2/src/postprocessing/dcraw_process.cpp) |
| 现有去马赛克候选 | LibRaw 文档包含 AHD、DCB、DHT、AAHD；默认 gamma/自动增亮必须显式处理 | [API 参数文档](https://www.libraw.org/docs/API-datastruct.html)（访问 2026-10-12） |
| 成熟算法复用替代 | librtprocess README 标注 0.12.0，提供 AMaZE、RCD、LMMSE、DCB、高光恢复等；并非完整相机颜色引擎 | [仓库](https://github.com/CarVac/librtprocess)、[许可](https://raw.githubusercontent.com/CarVac/librtprocess/master/LICENSE.txt)（访问 2026-10-12） |
| RawTherapee 对照版本 | 官方当前为 5.13，2026-07-26，GPLv3；可用作视觉对照，不代表可把其源码嵌入纯 Apache 产品 | [官方主页](https://rawtherapee.com/) |
| Little CMS | 当前 release 为 2.19.1，tag lcms2.19.1；基础 CMM 用于 ICC 转换；采用开源核心即可，不需要付费加速插件作为首版依赖 | [release](https://github.com/mm2/Little-CMS/releases/tag/lcms2.19.1)、[开源核心](https://www.littlecms.com/color-engine/)（访问 2026-10-12） |
| 浮点 ICC 非万能 | 不是所有 profile 都支持 unbounded；浮点 API 不代表所有转换保留负值和 >1 值 | [Little CMS 作者在项目邮件列表的说明，2023-08-13](https://sourceforge.net/p/lcms/mailman/message/37883191/) |
| Rec.2020 色度定义 | BT.2020-2，2015-10-14，采用 D65；软件选线性 Rec.2020 是借用色度基底，不等于 HDR 输出承诺 | [ITU 标准](https://www.itu.int/rec/r-rec-bt.2020-2-201510-i/en)、[正文](https://www.itu.int/dms_pubrec/itu-r/rec/bt/r-rec-bt.2020-2-201510-i%21%21pdf-e.pdf) |
| DNG 不是免费全能后门 | 1.7.1.0 规范为 2023-09；SDK 当前页面列 1.7.1 Build 2724，2026-09-08；接入意味着另审许可、构建、可选编解码与 opcode 语义 | [Adobe 官方 DNG 页](https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/digital-negative.html) |

上表未承诺某算法必然更清晰或更快，也未把官方相机列表等同 Lumina 的测试认证。库版本要作为一个实际编译组合固定，包括可选 RawSpeed、DNG SDK、JPEG 等特性，不宜只固定主版本号。

## Canon CR3 首批范围与样本计划

1. 优先用户实际拥有的 **一个机型**；若需要候选实验设备，可用 EOS R6 Mark II 24 MP 常规单帧 RAW。LibRaw 0.22 官方相机列表包含该机，但尚无 Lumina 测试证据。[LibRaw 相机列表](https://www.libraw.org/supported-cameras)
2. 第一认证单元是“厂商/机型 + 固件版本 + 常规 RAW 或 C-RAW + 快门/连拍模式及实际位深 + 传感器裁切模式”，不是“.CR3”扩展名。常规 RAW 先测，C-RAW 独立验收；只有实际测过的组合显示“已验证”。其他同类单帧可作为实验兼容或拒绝导入，不能静默假装已认证。
3. Dual Pixel RAW、RAW burst 的容器/多帧语义与一般文件不同，v0.1 不处理其专有能力，也不能无提示地选其中一帧作为唯一原图。初版可直接识别并拒绝编辑，允许查看嵌入预览。R6 II 官方图像质量页面是 RAW/C-RAW；不要把旧型号 M-RAW/S-RAW 泛化成 R6 II 的模式。[Canon 图像质量](https://cam.start.canon/en/C012/manual/html/UG-04_Shooting-1_0030.html)、[Canon Dual Pixel RAW](https://cam.start.canon/en/C012/manual/html/UG-04_Shooting-1_0040.html)
4. 首轮约 40–60 张有测试使用权的照片：日光/钨丝/闪光/混合光人像、至少三种肤色、灰卡/色卡、白衣与过曝窗户、LED 饱和色、头发/睫毛/织物/斜边、高 ISO 阴影提亮、低 ISO 正常曝光、竖幅/裁切/旋转、同场景 RAW/C-RAW、所用快门模式。拍摄组而非单张随机照片提供参考；没有色卡/灯光基准时，只能做主观与回归比较，不能装作测出了绝对色准。
5. 镜头校正首版延后时，要求验收样本来自不强依赖数码畸变矫正的镜头/焦段，或明确接受未校正外观；否则“照片闭环”缺一环。镜头与焦段是决策输入。

### 最小验收与失败路线

| 项目 | 建议目标（待测） | 失败处理 |
|---|---|---|
| 几何/稳定性 | 全部白名单样本能正确识别、无越界尺寸、方向/裁切一致；未知模式明确提示；坏文件不拖垮主进程 | 缩小模式白名单；保留预览与 TIFF 交接 |
| 白平衡/颜色 | 固定照明、曝光归一和中性渲染下，色卡初始 ΔE00 中位数 ≤3、p95 ≤6；中性灰无明显色偏；最终阈值按基准和用户偏好校准 | 校正参数/矩阵；如果需要完整 DCP/相机风格才可用，改复用更完整引擎或推迟内建 RAW |
| 人像观感 | 用户与至少一位有摄影经验的评价者在 100% 和适屏视图评价；肤色无系统性偏绿/偏洋红；睫毛/发丝/衣物无不可接受伪色、拉链纹 | 比较已有 AHD/DCB/参数；达不到则不把该机型列已认证 |
| 高光 | 逐通道饱和样本无突兀彩色边、紫色白衣；完整饱和区域不承诺恢复真实细节；改变 RAW 曝光始终从原始数据重显影 | 限制宣称/支持条件；不以 RGB 曲线降亮冒充恢复传感器高光 |
| 批量一致性 | 同一 preset + 单图覆盖的 100 张照片，单张与批量导出一致；失败不阻止后续项；重试不产生错误覆盖/同名乱序 | 优先修配方与输出事务；属于首版阻塞 |
| 资源 | 8 GB 机上单 RAW worker、一张显影在途；100 张队列不随张数线性驻留 RGB 全图；记录首图、每图时间、主进程+worker峰值、冷/热区别 | 降低并行度/预览尺寸、限像素数；不靠加内存掩盖泄漏 |

Canon DPP 和 RawTherapee 固定版本均可作为**独立观感参考**。DPP 默认相机风格、自动光照优化、降噪、锐化应记入对照记录。与其默认像素不一致不是自动失败；复制某家的默认风格也不是色准定义。用户如果明确要求 Canon Portrait 风格一致，需要另立“风格匹配”项目，不能由相机矩阵保证。

## 颜色契约必须补齐

保留线性 Rec.2020/D65 作为 v0.1 唯一内部色度基底是合理的，避免首版提供一排工作空间选择。浮点容器允许负值与 >1，不要求 Rec.2020 三角形包含所有原始颜色。**不同输入的语义不同**：普通 JPEG 是已经渲染的 RGB；有限 LibRaw 16-bit 显影结果同样经历渲染和截断，不能与未来完整 scene-linear RAW 母版混为一谈。资产须存 encoding、primaries、white point、range、alpha、rendering stage、源配方版本。

推荐数据流：

```text
原始 CR3 + 已认证模式 + RAW 配方快照
  → 单 RAW worker：LibRaw 解码/现有显影（明确黑白电平、WB、曝光、高光、demosaic、输出域）
  → 16-bit 线性已显影 RGB + 转换元数据 / 原始文件仍保留
JPEG/TIFF + 内嵌 ICC / 无 ICC 的显式 sRGB 假定
  → Little CMS 输入转换（不额外再“解一次 gamma”）
两路 → 编辑工作域 RGB float32 → 编辑/蒙版/裁切/普通合成
  ├→ SDR 输出渲染/色域处理 → 显示器转换 → 画布呈现
  └→ 固定配方/revision → 输出色域/位深/抖动 → JPEG/TIFF + 正确 ICC
```

具体修改：

- **拒绝所有中间块默认 RGBA16F 的笼统保证。** CPU、RAW、累计/敏感算子采用 float32；预览 GPU 缓存可用 RGBA16F，源、长链、暗部大幅提亮和正式导出逐算子决定。half 的范围不等于足够精度，反复写回 half 会累积误差。原始文件、蒙版/笔触和配方是持久化真相；预览缓存不作为以后重编辑的唯一母版。
- 输入 ICC 本来包含传递函数，不应“手动 sRGB 解码 + LCMS 再按 sRGB 输入处理”双重变换。相机矩阵是矩阵路线，ICC 是 profile 路线，DCP 是另一套处理协议；一个“输入颜色配置”字段容不下所有语义。
- 明确相机白平衡与 RGB 色温控件。RAW 阶段可以标温度/色调但其映射需要相机模型；RGB 暖冷偏移不能伪装成同一个物理操作。固定 pipeline 版本以免旧配方升级后悄悄变色。
- 明确一次 SDR scene-to-display/rendering 变换的位置；不能在源显影、预览、导出各自暗中加一条不同的“好看”曲线。调色工作域和曲线/HSL 的感知域、负值行为需要公式。首版只保留确有必要且能解释的控件。
- 优先显示已合成到不透明照片背景后的 RGB，绕开画布最终半透明颜色管理的复杂度。蒙版为线性覆盖率，绝不应用 sRGB gamma。普通 source-over 在线性域进行；感知调色先解预乘、定义 alpha≈0 和 NaN/Inf 处理，再预乘。只支持 Normal 混合；不要首版承诺 PS 所有混合模式。
- GPU 3D LUT 通常只有有限采样域，不能把未映射的负值/超白直接钳到 0–1 再声称保留高光。先形成受约束 SDR display RGB，再做显示转换；优先矩阵+TRC profile 的分析路径或经过验证的 LUT。复杂 ICC 用 LCMS CPU fallback；不能为赶工静默忽略 profile。
- 显示器移动/ICC 改变时失效显示缓存；导出绝不包含显示器 ICC。Windows 系统颜色管理与应用 ICC 如何共同生效需要桌面画布实验，特别是 Windows HDR/Advanced Color。首版 SDR 可先限定 HDR 关闭的已验环境，HDR 桌面无结论时明确不认证，而非宣称色彩管理自动正确。
- 颜色 P0 至少含灰阶/高饱和/负值/超白/alpha 边缘、sRGB 与广色域输入、两块不同 ICC 显示器。CPU float32 数学参考与 GPU 预览比较：显示后建议 p99 ΔE00 ≤0.5、最大 ≤2（排除明确的色域裁切奇点另列）；所有输出必须有限、无可见带状。该数字是验证目标，不能在没有测试时写成精度保证。

## tile、halo 与批处理的修正

原设计“最终全流程统一 tile”应删除。外部接口可以固定逻辑 tile 地址，算子按实际后端使用物理 block/ROI/全图暂存。LibRaw 解码和显影可能需要整图；在外围套 tile 不能降低其内部峰值，也不能把 CFA 随意分块后拼接。v0.1 先将 RAW 当全图、受限并发的源任务；编辑预览、蒙版和导出再按 tile 流式处理。

512² RGBA16F = 2 MiB 的计算正确，但实际占用还包括 input+output+ping-pong+halo、mip、GPU上传 staging、读回、行对齐、CMM buffer 和正在执行、不能回收的旧 revision。24 MP RGBA32F 约366 MiB，60 MP约916 MiB；在8 GB机器上同时保留 RAW ushort、RGB float、多个历史全图和批量预读非常危险。多级 mip 总面积约原图4/3，也要计入预算。

halo 必须来自算子真实依赖：有限半径滤波可逐级膨胀；warp/clone 由逆映射/采样位置决定，不是一个固定半径；统计运算分归约阶段；迭代/全局降噪与 Poisson 不是独立 tile 加一点边即可。v0.1 若只保留基础调整与蒙版，可推迟复杂 halo 调度及全局修补，将精力用于批量输出的有界资源。

批处理优先保存轻量配方和缩略图，不为一百张照片创建一百个驻留多图层文档。单次批量开始就冻结每张资产哈希、配方版本、输出配置；后续编辑不得改变已排队项的含义。先一个 RAW 显影任务在途、一个导出编码任务串接；没有实测不要并发 N 张以为更快。worker 可复用但每张 recycle，观测峰值/泄漏后决定按张数或资源阈值重启。

取消要区分 UI 立即响应、后台可协作终止、第三方库无法立即终止：旧结果的 job_id + item_id + revision 校验是必须；必要时终止独立 RAW worker。GPU 已提交命令不能靠标一个取消令牌消失，需要保留资源到 fence 完成。批量文件先写临时目标、成功后提交；取消/失败不留伪完整 JPEG，也不覆盖源 CR3。

## 可交给主审的两个最高风险项

| 风险 | 最小实验 | 通过条件 | 失败路线 | 首版阻塞 |
|---|---|---|---|---|
| Canon RAW 人像质量与显影上限 | 一个真实机型，40–60 张分组样本，LibRaw 0.22.2 AHD/DCB 基线，对照 DPP/RawTherapee；记录模式、颜色参数、耗时、峰值 | 模式识别/几何100%正确；用户认可肤色/细节；高光无明显彩边；批量/单张一致；不宣称未保留的动态范围 | TIFF交接版先发；或许可接受GPL后复用更多现成算法并延期RAW | 若产品宣称原生 CR3 闭环，则阻塞；仅 RGB 编辑工具可独立发，但要改定位 |
| 颜色显示/导出与低内存数据流 | 带ICC测试图+alpha/负值/超白；CPU参考/预览比较；8GB集显100张批量+取消/移动窗口 | 不双重gamma、导出不带显示ICC、无NaN/接缝；有界RAM且无张数累计；界面仍可取消 | 降为CPU显示转换/低分辨率预览，单任务RAW/分块导出；关闭未验证HDR路径 | 是，色偏/崩溃/批量损坏不能靠“实验”标签交付 |

在一位熟悉 C++/Qt 的全职开发者、可及时获得 RAW 样本和真实目标机器的假设下，RAW/颜色这条最小风险实验可先预算约 2–4 人周；实际相机颜色资料、构建和测量失败会显著拉长。这是工作量估算，不是完整软件工期；缺少样本和参考光源的等待时间另算。

仍需用户提供的决策信息仅包括：实际相机/固件与常用 RAW/C-RAW/快门模式、常用镜头；人像色彩更偏向“自然中性”还是“Canon机内风格”；一批典型照片的数量/像素量和可使用的测试样本；是否接受先经 DPP/其他工具输出 TIFF 的临时路线；若现成 RAW 算法要求 GPL，是否接受项目整体采用 GPL-3.0。不得把这些未知值伪装成已经批准的产品保证。
