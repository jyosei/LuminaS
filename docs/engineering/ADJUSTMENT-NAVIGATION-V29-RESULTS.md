# V29 调整卡片导航（ADJUSTMENT-NAVIGATION-V29-RESULTS）

## 1. 行为
- 在调整面板滚动区之上插入紧凑导航栏：分类下拉框 + 「全部展开」「全部收起」两个按钮。
- 下拉框条目严格按卡片布局顺序生成，文字/图标取自各卡片 `card-header`，与卡片标题一致。
- 选择某分类：展开该卡片（`setChecked(true)`）并在布局生效后滚动到该卡片头部，使其可见。
- 只改变被选卡片的展开状态，其他卡片展开/收起状态保持不变。
- 全部展开/全部收起仅切换各卡片头部勾选状态。
- 键盘：下拉框获得焦点后连续按 Down 可逐项跳转并展开，焦点保留在下拉框，便于反复按键。
- 无照片、面板内容被禁用时导航仍可用：导航栏本身保持启用，可展开/收起并滚动位置；卡片头部仍随 `editPanel` 禁用，不绕过门禁进入编辑。
- 导航仅改变展开/滚动等 UI 状态，不写入编辑状态、不写入历史。
- 原滚动区对象身份、内容、信号与控件数值均不变；控件数值/信号零变化。

## 2. 文件
- `src/app/adjustment_navigation.h`：声明 `Ui::installAdjustmentNavigation(QDockWidget*, QScrollArea*)`。
- `src/app/adjustment_navigation.cpp`：实现导航栏装配、下拉选择、滚动跳转、展开/收起。
- `tests/adjustment_navigation_tests.cpp`：10 个 QtTest 用例，覆盖装配、顺序、跳转、隔离、键盘、禁用、数值不变、销毁安全及原生窗口。
- `CMakeLists.txt`、`src/app/window.cpp`：组件/测试构建注册与主窗口接入。
- `docs/engineering/ADJUSTMENT-NAVIGATION-V29-RESULTS.md`：本报告。

## 3. 实现要点（代码走查，非算法改动）
- 复用原 `QScrollArea`：新建 wrapper，`scroll->setParent(wrapper)` 后 `dock->setWidget(wrapper)`，工具栏位于原滚动区之上。
- 仅按 `content->layout()->itemAt(i)->widget()` 的布局顺序识别 `adjustment-card`，不依赖 QObject 子对象顺序。
- 头部用 `QPointer<QToolButton>` 保存，避免悬垂指针。
- 延迟跳转用 `QTimer::singleShot(0, wrapper, ...)`：先 `layout()->activate()` 再 `ensureWidgetVisible`；generation 票据丢弃过期跳转；wrapper 销毁时自动取消待执行定时器。
- 前置守卫：`dock`/`scroll` 为空、`dock->widget()!=scroll`、`scroll->widget()` 为空、无 layout、无 `card-header` 时不安装。
- 纯原生 Qt Widgets；沿用现有深紫色主题与 Source Serif 4；未新增动画。

## 4. 实际验证（GPT 执行）
- 最终构建：exit 0。
- 原生专项 QtTest：12 passed / 0 failed / 1065ms，含 `mainWindow`（未加载照片）场景。
- 启动冒烟：exit 0。
- UI 技能 `ui-design-system` / `interaction-design` 已应用：层级、间距、主题、悬停+焦点、可访问性。
- 视觉核查由 GPT 本地完成，未上传照片；引用本地工件路径 `build/private-qa/adjustment-navigation-v29-ui.png`（本执行层未读取）。
- 完整 CTest 9/9通过，288.52秒，exit 0。包括暗角39.26、透视窗口32.95、透视.42、分色窗口10.67、分色.99、S2 200.08、S1 .29、S0 2.98、导航.66秒。实际日志：`build/private-qa/adjustment-navigation-v29-ctest-final.txt`。

## 5. 委托边界与诚实说明
- 初始 DSH 实现会话 `e8a30867-1321-40fd-9224-0b06a468ce21` 仅产出脚手架/测试/CMake/窗口草稿，**未被接受**。
- GPT 停止了卡住的 worker 构建进程及其进程树；跨进程 CLI 取消找不到运行中的会话记录，故改用限定范围的进程终止。
- worker **未完成、未通过**构建/测试。GPT 发现并修正了错误的视口父级守卫、错误的工具栏插入位置、裸指针生命周期、include 路径与错误的 target 断言，随后重写组件/测试并实际运行检查。
- 因此本次**不表述为 worker 通过**。初始 worker 用量：30 次调用，input 27628 / output 8518 / cacheRead 676096 / total 712242；token 数不代表余额，审计标记仅为临时构建命令，不构成隐私认证。
- 无 provider 自动重试或充值。

## 6. 剩余事项
- 本批专项和完整回归均通过；没有重新认证照片解析速度、画质、内存预算或所有DPI/辅助技术设备。
- S2 手动畸变/色差/displayICC/摄影画质/资源检查仍缺。
- 卡片导航已实现，但整体 UI/可访问性/DPI 仍需进一步 QA。
- S3 仅为计划，目标保持 active。

文档DSH会话 e0f9f419-6ef8-4aa9-bcc5-375f29f07c12 正常完成：16次调用，input16176/output5040/cacheRead196480/total217696；GPT审查并补入最终回归结果。计数不代表资金余额。
