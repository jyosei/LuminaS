# C4羽化/反相核心专项（控件与诊断尚未接入）

2026-10-16。实现合同见[S3-C4-NUMERICAL-CONTRACT.md](S3-C4-NUMERICAL-CONTRACT.md)。本批只推进C4数值与渲染入口，不能据此宣布C4或整个S3完成。

## 实现

- BrushMaskReference和PreparedBrushMask支持整体feather/invert；先按持久顺序组合，再平滑，最后反相。源平面归一化可分离余弦窗，f=0保留原解析路径；不是高斯sigma，不在屏幕坐标模糊。
- 生产羽化按256×256源网格分组，以九个累积矩逐行扫描查询；每组只存一行累积矩与tile查询列表，不新增全源蒙版。参考路径直接逐样本卷积，两种实现相互对照。
- alpha只在目标像素门禁，不卷入邻域、不重复乘部分透明度。反相空蒙版对有效目标为1，透明/无效映射仍为0。旧参数变换保持straight线性Rec.2020和alpha。
- 格式最大半径16384继续被接受；测试覆盖其有界临时分配与取消，**不代表最大半径非空场的完整吞吐已验证**。源区域扫描时间随支持区域增长，C5仍需实测与优化。

## 实际构建和运行

- `s3-c4-core-build.txt` / `reviewed-build.txt` / `accepted-build.txt` / 最后 `s3-c4-core-final-build.txt`：实际编译成功exit0；最后同时重链生产LuminaS。最初未初始化Stencil临时系数已在复核中补零；并将取消检查细化到每个源采样，未更改像素数学。
- `s3-c4-core-check.txt` 首轮5/5（2.80秒）；补充完整渲染负值/HDR/alpha后 `reviewed-check.txt` 5/5（2.55秒）；最大半径取消与显式HDR通道断言修正后，**最后 `s3-c4-core-final-check.txt` 5/5（2.71秒）**。
- 五个实际目标：`local_mask_feather`、`local_mask`、`local_reference`、`local_renderer`、`local_gradient`。新羽化7 passed、原蒙版10 passed、参考14 passed（含init/cleanup）；无羽化的旧画笔位模式对照一起通过。不能以此替代新的整仓库回归或真实R7像素基线。
- 新测试包括已知硬阶跃0/.25/.75/1、常量0/.25/1及跨原始256源网格位置、负坐标与大有限源坐标、fractional feather、整体与逐组件羽化非等价、最后反相、部分alpha/透明洞不污染邻居、组合几何、256/512完整区域逐点对照（覆盖误差<2e-6）、取消/非法输入不发布部分结果，以及完整渲染的实际RGB变化/负值/超白与alpha位模式保持。
- 最大半径非空采样取消用例：临时容器payload断言在3–5MiB之间、400次源采样内响应取消，旧输出哨兵不变。该范围是一次测试查询批次的payload估计，未计容器节点/分配器或图像内存，不是进程RAM承诺。
- `s3-c4-core-smoke.json`：最新生产程序实际Windows QPA启动，用公开合成fixture `build/dev/fixtures/中文横图.jpg`，exit0、photoCommitted=true、实际photo paints=2，进程内报告1986ms。这个时间是整个小图启动/退出流程，不是R7读取速度、羽化延迟或FPS。

## 委派与后续

DeepSeek Flash仅接收photo_document.h和渐变组件头文件，返回独立蒙版选项Widget草稿；实际输入1356、输出2016 tokens，stop结束，无执行声明。GPT审查发现sliderDragging在valueChanged置true后不在release清除，会阻断结束提交；键盘滑块也没有正确排队finish，因此**尚未接入该草稿**。草稿只保存在忽略的开发目录，后续需修复事务/刷新门禁并实际验证，不能把草稿当产品完成。

本批注册58项CTest，未运行完整58/58；最近全量49/50中的atomic_file偶发拒绝访问仍未定位修复。C4控件、覆盖色和黑白查看、真实R7/45MP羽化全源重放与速度，C5/C6及D仍在范围内。导出队列尚未桥接局部文档。没有新截图/整图渲染输出、打包或推送；样片、资源、当前构建与旧包保留。
