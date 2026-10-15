# UI-UX V46 结果记录（执行层）

## 目标状态
目标保持 active。最终 realphoto / packaging / after-wheel 检查未完成前，
不得宣称版本完成或交付。

## 设计方向（延续 V43/V44/V45）
- 视觉方向：黑 / 灰 / 紫。
- 标识：Source Serif wordmark + 自定义 frameL logo。
- 图标：Lucide + QtSvg，自 V43 起保持。

## V44 已落地事实
- 裁剪入口现在会揭示 composition 区段。
- wb / point picker 与 hand / crop 选择状态同步。
- 提供显式 exit crop。
- native 测试 8 passed，2748 ms。

## Reachability 复核
- worker eaef4065（草稿）经 GPT 加固：缺失区段不再被跳过。
- 21 个区段（含构图）与 131 个 numeric/slider 控件经滚动均完全可见。
- Recipe 未改变；测试 8 passed，7267 ms，含截图。

## 当前快照
- 处于最终全局宽度归一化之前的快照状态。

## V45 曲线键盘编辑
- Home / End、Ctrl+Left / Ctrl+Right 选择节点。
- Insert 在最大间隙插入节点。
- 方向键步进 .005，Shift 步进 .05。
- Delete 受端点保护。
- shared CurveSpline 数学未改动。
- standalone 4 passed，264 ms。
- 实际窗口键盘渲染/历史与工具/恢复套件 9 passed，3362 ms。
- 完整 CTest 30/30 passed，321.65 s（在 wheel guard 之前）。

## V46：滚轮问题修复
- 测试捕获到未聚焦滚轮改变 Recipe（修复前日志 ui-v46-wheel-before.txt）。
- 新增 ParameterWheelGuard：未聚焦时，参数滚轮事件转发给所属滚动视口。
- 聚焦状态下的 native wheel 行为保留。

## 待办（未完成，不得声称完成）
- After-wheel 测试、DPI 检查、真实 R7 JPEG + CR3、最终打包。

## 测试方法约定
- 旧运算符测试显式通过公共菜单打开所有区段，以避免测试隐藏控件。

## 新可选 CLI
- `--smoke-test --smoke-report fresh.json`
- 在 app event loop 结束后报告 exitCode / photoCommitted / nativePhotoPaints / elapsed。
- 该路径尚未在打包产物中验证。

## 范围未变
- 核心处理未改动；S3 未推进。

## 委派记录
- 曲线 worker fd55... 在 33 次调用后取消：读取了允许范围之外的 source/test 文件，
  并尝试了未被请求的 baseline build，未交付任何代码。
- GPT 确认原 CurveEditor cpp digest 未变化，随后自行实现。
- 直接 API 初步评审超时；未自动重试，未接受任何结果。
- 未向 worker 发送任何图像或凭据。

## 当前保留
- 保持当前目标 active；保留桌面快捷方式 V39，直至最终候选在真实环境完成 QA。


## 2026-10-15 最新核验与用户约定（覆盖上述待办状态）

滚轮保护后的完整30/30通过324.53秒；DPR1/1.5/2与700×480检查通过；R7 JPEG/CR3四条实际加载/拖动专项exit0，12张原片SHA零变化。V47仅修正箭头资源，重建及8/8相关回归通过22.23秒。详见UI-UX-V47-ACCEPTANCE.md。

用户要求不主动打包：后续仅在明确请求时生成或更新桌面包、ZIP及快捷方式。V47打包在中止前已完成，无残留进程，独立部署仍未核验；不再将该部署验收列为本轮默认门槛。

V48最终：参数Tab顺序显式改为滑杆→数值框→复位，双向导航配方不变。重建0，相关8/8通过21.43秒；验收软件范围完成，以UI-UX-V47-ACCEPTANCE.md最终结论为准。
