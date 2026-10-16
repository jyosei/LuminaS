# S3 B3：主窗口局部笔刷接线结果

2026-10-16。本批把局部编辑接入原生主窗口；尚未完成 B6/B7 及 C/D，不能据此宣布 S3 完成。

## 实际行为

- 原调整区增加“全图基础 / 局部调整”，全图控件、导航和对象名保留。新建中性笔刷层后自动选择该层、打开紫色覆盖并启用笔刷；普通浏览仍是小手与滚轮。
- `BrushTransaction` 冻结完整源尺寸/几何、层及组成项 ID、笔刷参数和 Add/Subtract。UV 经已有 outputToSource 回到方向归一后的原图；拖动只产生小文档草稿，结束时一条 EditHistory 命令，没有像素历史。
- 一笔、一次局部参数拖动分别只提交一次。Esc 丢弃未提交草稿；切图/层/工具、缩放、失焦先结束有效笔画。强度零、擦除初始全零蒙版和确定不接触原图范围的笔画不产生历史；不以中性曝光作为“没有蒙版内容”的判断。
- Alt 在按下时冻结擦除操作；Space 先提交再临时平移，松开后仍需新的按下才能画下一笔，不跨平移连线。`[ ]` 改下一笔大小，`O` 控制覆盖；画布快捷键不装全应用过滤器，文本焦点不会触发。
- 接受结果时先验证完整 RenderView、版本、路径及源 SHA，再更新照片与独立 Indexed8 覆盖。覆盖只在 Canvas 给索引加 UI 色表、画在设备呈现之上，不进入 SourceFrame、照片 RGB、直方图或胶片条。
- 默认参数层也能显示真实蒙版。覆盖与效果复用同一个 float 蒙版；Indexed8 量化只服务显示，完整源仍使用 float 覆盖与原片入口。
- 元数据绑定保留已有 asset ID，使用真实准备结果的 SHA、原始尺寸和 facts。结束事务保留新细化的 metadata；基底不相容时拒绝覆盖。空筛选/未准备照片禁用局部控件，不保留无图层的绘画工具。

## 当前检查证据

- `s3-b3-native-core-build.txt` exit 0。初轮核心专项 2/3：笔触事务和数值合成通过；画布测试暴露实际 WindowDeactivate 路由遗漏，以及三处夹具/断言错误。补了 event 路由，修正重复 finish 计数和无色表 Indexed8 夹具，随后 `s3-b3-canvas-reviewed-check.txt` **1/1，1.42 秒**，QtTest **10 passed / 0 failed / 0 skipped**。
- `s3-b3-window-build.txt` exit 0，`s3-b3-window-check.txt` **5/5，5.01 秒**：主窗口、画布、笔触事务、数值合成及真实呈现来源标识均通过。笔触事务 **5 passed**，局部渲染 **9 passed**，呈现来源 **5 passed**。
- 主窗口扩展检查 `s3-b3-window-expanded-check.txt` **1/1，3.04 秒**：**6 passed / 0 failed / 0 skipped**。包含一次按下/拖动/释放、一笔撤销重做、Esc、参数拖动撤销、Space 跨平移、文本焦点、组合几何的原图锚点、原图比较不改文档、切图提交与跨图撤销、空筛选禁用。
- 随后增加真实 Alt 擦除检查：主窗口按下 Alt 产生 Subtract，后续松开 Alt 不改变本笔操作；后台真实覆盖中心变为 0，一次撤销只移除此笔。`s3-b3-local-visual-check.txt` **1/1，2.96 秒**。此处使用程序生成的临时 JPEG，不能冒称已验证真实 R7 RAW。
- `s3-b3-native-full-build.txt`、`s3-b3-native-reviewed-build.txt` exit 0，主程序与所有既有目标重建。
- 首次完整回归 `s3-b3-native-full-ctest.txt` **47/50，340.94 秒**。三项失败为 adjustment_navigation、point_color_eight_samples、inspector_search；均因旧测试用“第一个 QScrollArea”定位全局页，而新增局部页改变了子对象遍历顺序。生产全图滚动区增加稳定对象名，测试明确选择全图页；保留禁用、完整可见区域、滚轮实际滚动及配方不变等断言，没有放宽验收。
- 修正后 `s3-b3-global-scroll-check.txt` **6/6，15.97 秒**，覆盖上述三项和 local_window、local_canvas_input、display_provenance。随后 `s3-b3-final-build.txt` 完整构建 exit 0。不把这次专项重验冒称修正后的完整 50/50。

QtTest 最新细节在 `build/dev/test-logs`，构建/运行日志在忽略的 `build/private-qa`。

## 呈现计时的边界

新增 `DisplayPresenter::imageReadyTagged`、`Canvas::canonicalFramePainted` 与 `localMaskPainted`，携带实际来源图像的 key。测试特意令同一上下文中的旧转换先完成、新 canonical 已更新，确认来源仍是实际交付的旧帧；sRGB 回退则报告实际返回的 canonical。不能拿 Window 最新 displayedTag 给旧中间帧计时。

蒙版单独重绘不增加照片 framePainted 计数。RenderResult/EditResult 分开传递 effectElapsedMs 和 maskElapsedMs；这些是后台计算耗时，不是请求到上屏延迟。端到端任务台账、真实 R7 4K/全源重放和实际交互分布仍待 B6/B7；本批不宣称 ≤100ms 或30/60fps。

## 可视检查与产物

使用主窗口真实涂抹和曝光调整后的合成 JPEG 场景截取一次 UI，核查了主题、面板可见性、文字/滑杆/数值可达，以及紫色覆盖没有污染照片缩略图。顶部导航提示已随笔刷模式更新。检查后的 `s3-b3-local-native-ui.png`（70,295 字节）已删除，不作为仍可打开的附件；默认测试不生成截图。

## 委派审计与恢复

- Canvas DSH 会话 `0bcd9839-6ac4-4c04-85f2-4b0c3bb69638` 先交付事件草稿、后补测试。其唯一构建在隔离执行环境中长时间停在配置之后，没有编译活动；未启动重复构建。取消原会话后确认相关子进程退出，再由 GPT 在本机运行同一目标，正常构建。隔离环境等待根因未定位，不能把该 worker 构建称为通过。
- Window 会话 `37494681-d19d-4c02-9c22-149710908dc0` 工具观察超时后，先按原 ID 查询；状态明确为 failed 才接管，没有自动重试 provider。其草稿破坏旧源 SHA 检查/预览分支并产生未定义信号，未采纳。GPT 核对任务前备份与已提交原文件一致后恢复 window.h/.cpp，再独立接入；没有在损坏的旧文件上叠补。
- 实际审计有公共源码越界读取、Canvas worker 列举 QA 文件名等范围偏离；没有观察到私人图像内容/凭证读取。三条 private-qa 标记分别是目录是否存在、文件名列表和本任务允许的构建日志重定向，不是图像读取。详细工具参数保存在忽略的 `.tools/s3-b3-<session>-audit.json`。
- 两会话实际 provider usage：Canvas 23 steps，input 26,097 / output 30,401 / cache read 626,304；Window 69 steps，input 98,502 / output 26,130 / cache read 5,463,552。不据此推算人民币余额。
- 追加 DeepSeek Flash 只读事务评审，仅发送 transaction 头/实现/测试三个文本文件；没有可证明的代码缺陷，提出一个版本溢出路径测试空缺，未当成产品故障。实际 input 3,363 / output 1,560，无缓存；GPT 保留最终判断，没有根据其回复声称执行过测试。

保留当前构建、源数据、资源和既有桌面包；没有推送、ZIP、安装器或快捷方式。
