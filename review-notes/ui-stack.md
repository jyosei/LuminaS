# UI 与桌面技术路线独立审查

审查日期：2026-10-12。已阅读 ASTRA-REVIEW.md、DECISIONS.md、docs/02-stack.md、docs/06-ui-design.md、qa/report.json，并直接查看 dark-photo、dark-compose、light-photo 截图。没有运行包内脚本。以下产品判断来自设计包；技术能力来自下列官方资料；延迟和投入数值均为建议实验目标或估算，不是实测。

## 决策

用户补充的单人开发、C++/Qt 经验明显多于 Rust、8–16 GB 核显笔记本、人像优先且必须有批量处理，足以改变默认方案：**采用 C++20 + Qt 6 Widgets；核心与界面保持模块边界，但不为边界引入第二语言。首版先用 CPU 分块/代理图处理与 Qt 的视口显示路径，wgpu、Rust、Qt Quick 均不作为先决依赖。**

Rust + egui + wgpu 有能力画出第二版的面板与摄影画布，拒绝它作为本项目默认方案不是因为“画不出来”或“没有中文/压感/无障碍”。拒绝理由是此人的学习、集成和后续维护成本大于统一 Rust/GPU 栈的潜在收益。Qt 也不会自动解决图像质量、显存、色彩管理或笔刷算法。

| 方案 | 当前结论 | 理由与边界 |
|---|---|---|
| C++20 + Qt 6 Widgets + 同语言核心 | 采纳 | 已有经验最有价值；菜单、数值编辑、键盘焦点、模型列表、IME、tablet API 与辅助技术入口现成。无需 Rust/C++ ABI 和双构建链。自定义画布、曲线、蒙版与可访问性仍需应用实现。 |
| Rust 核心 + Qt Widgets | 拒绝作为 v0.1 默认 | 只在已有成熟 Rust 核心或后续拥有 Rust 维护者时有充分理由；当前没有既有代码资产值得为它承担 C ABI、内存所有权和异常边界成本。 |
| Rust + egui/eframe + wgpu | 保留实验备选，不进入主线 | 普通摄影面板可实现；egui 输入抽象并不是完整专业数位笔事件集。只有团队配置改变且独立纵向样机通过所有实际输入/画布门槛时再考虑。 |
| Qt Quick/QML | 延后 | 能做漂亮自绘 UI，不能因现代外观就推断更适合本项目。此人熟悉 Widgets，当前需要工具密集桌面与批量列表，新增 QML/scene graph 调试面没有明确收益。尤其不要在 Widgets 中逐块塞 QQuickWidget 来追求外观。 |
| Qt + wgpu 双 GPU 栈 | 拒绝 v0.1 | 共享设备、纹理、同步和呈现的成本没有被预算。不是把一张 GPU texture 指针传过去就完成。 |
| CPU 视口显示 + 可替换 CanvasBackend | 采纳，先测 | 将实时目标限定为可见区和合理代理分辨率；完整分辨率的导出在后台。若 CPU 代理仍不达标，只升级热点与画布后端，不先改语言/GUI。 |

官方资料已核对的版本是 **egui/eframe 0.36.2、winit 0.30.13、该 egui 工作区使用的 wgpu 30.0，以及 Qt 6.11.2**。这是证据版本，不是“全部各自选最新”的依赖指令。[egui 0.36.2 Cargo.toml](https://raw.githubusercontent.com/emilk/egui/0.36.2/Cargo.toml) 中明确列出上述依赖与 Rust 1.95 最低版本。

Qt 6.11.2 于 **2026-08-18** 正式发布，官方明确允许开源用户通过开源区域取得安装器和源码，并不是仅商业补丁：[发布公告](https://www.qt.io/blog/qt-6.11.2-released)、[源码档案](https://download.qt.io/archive/qt/6.11/6.11.2/single/)。现在已有更新的 Qt 分支；正式实施时选公开可取得、仍维护的稳定 6.x 补丁，锁定版本并复跑样机，不把 6.11.2 写成“最新”或长期维护承诺。

## egui 能力的准确边界

- **中文与输入**：egui 0.36.2 有 Ime 事件；指针、键盘、触摸与 force 入口均存在。其通用 Event 中没有 tablet 的 tilt、橡皮擦端、proximity 等完整语义，普通 PointerMoved 也不携带压感。应用要保留每个输入样本，不能只在每帧读鼠标末位置。[Event 0.36.2](https://docs.rs/egui/0.36.2/egui/enum.Event.html)
- **不能错误声称 Windows winit 不支持压感**：winit 0.30.13 Windows 后端明确对 PT_PEN 调用 GetPointerPenInfo，取 pressure 并发出 Touch 事件，也读取 pointer 历史。存在能力不等于所有 WinTab/Windows Ink 驱动都通过。[Windows 后端 v0.30.13](https://raw.githubusercontent.com/rust-windowing/winit/v0.30.13/src/platform_impl/windows/event_loop.rs)
- **无障碍**：eframe 的 native AccessKit 默认启用；内置控件提供角色/名称，自绘控件仍要显式提供 widget info、label 和可操作语义。所以“有 AccessKit”不等于自绘图层列表已经可用。[官方 accessibility.md 0.36.2](https://raw.githubusercontent.com/emilk/egui/0.36.2/docs/accessibility.md)
- **按需绘制、多窗口**：Context 提供 request_repaint/延迟唤醒；eframe 支持后台唤醒。deferred viewport 可独立刷新，immediate viewport 的父子窗口会相互触发重画。即刻模式不要求静止时 60 fps，也不自动保证无忙循环。[Context 0.36.2](https://docs.rs/egui/0.36.2/egui/struct.Context.html)
- **摄影画布**：egui-wgpu 的自定义 callback 可以接自有 GPU 管线，但纹理精度、ICC、alpha、显示变换与操作系统呈现契约都仍由应用承担；不要把图像处理塞进标准 UI 颜色控件或上传每帧全尺寸 8-bit ColorImage。[CallbackTrait 0.36.2](https://docs.rs/egui-wgpu/0.36.2/egui_wgpu/trait.CallbackTrait.html)

因此若缺少此次团队信息，egui 可获得一个有严格止损线的 2–3 工程周验证窗口；现在没有理由让这条实验阻塞熟悉的 Qt 主线。

## Qt 主线的实际实现边界

**先完成 CPU 核心 → 代理/可见 tile → 显示变换 → 不可变 QImage → QWidget::paintEvent。** 核心工作缓冲仍可为 float32；QImage 的显示用格式不决定引擎精度。后台产生新帧，GUI 线程只替换带 revision 的帧与呈现。禁止 GUI 线程跑 RAW/全图算子，也不能让 worker 修改 GUI 正在读取的同一个缓冲。输出文件走全分辨率输出变换，绝不复用显示变换后的预览。

Qt 6.11.2 的 QImage 有 16/32-bit 浮点格式，可在非 GUI 线程绘制，且使用隐式共享；但 QPainter 并未对全部格式同等优化。`setColorSpace` 只标记，不变换像素；已有色彩转换 API 也不证明任意窗口自动正确显示 ICC。使用 QImage 作为持有/传递显示 tile 的容器，而非让 QPainter 定义摄影算法。[QImage 6.11.2](https://doc.qt.io/qt-6.11/qimage.html)

QWidget 的 update 会合并刷新请求，适合完成新结果后按脏区重画；无需另开固定 60 Hz 定时器。[QWidget 6.11.2](https://doc.qt.io/qt-6.11/qwidget.html)

Qt 的 QTabletEvent 给出 pressure、tilt、rotation 等 API；实际设备能力取决于驱动，必须测一套用户真实数位板。正确处理 tablet 事件并 accept，避免回退成被压缩的合成鼠标事件。[QTabletEvent 6.11.2](https://doc.qt.io/qt-6.11/qtabletevent.html)

若测出视口 CPU 变换/合成瓶颈，再比较 **QRhiWidget**。它支持 Qt 的 D3D11/12、Vulkan、Metal 等后端及浮点颜色缓冲；但自带离屏纹理与合成成本，使用的 QRhi/QShader 类只提供有限兼容保证，需 GuiPrivate 且锁 Qt minor，不能将其当作稳定独立引擎接口。它也没有承诺任意外部 wgpu device/texture 无成本互操作。[QRhiWidget 6.11.2](https://doc.qt.io/qt-6.11/qrhiwidget.html)

QQuickWidget 官方明确有额外离屏 render pass，并禁用 threaded render loop；这不是对纯 Qt Quick 的性能否定，而是拒绝混合方案的具体依据。[QQuickWidget 6.11.2](https://doc.qt.io/qt-6.11/qquickwidget.html)

Qt 原生控件的辅助技术与 High DPI 设施能降低集成成本；自定义曲线/蒙版、跨屏显示变换、200% DPI 的实际像素量仍需专门处理。[Qt Accessibility](https://doc.qt.io/qt-6.11/accessible.html)、[Qt High DPI](https://doc.qt.io/qt-6.11/highdpi.html)

## 第二版 UI：具体修改

专业感已比线框方向完整；中性灰、控件分组、数字输入和有限紫色强调值得保留。主要弱点是同时显示长期功能、工作区、图层、风格、虚拟副本与 AI 操作，却还没有清楚说明当前操作作用在哪张照片、哪个局部调整。

### 信息架构

1. 首版只保留“浏览/批量”和“编辑”两个任务入口。删掉“图层合成”工作区；编辑使用全局调整 + 命名的局部调整列表 + 修饰层，避免承诺 PS 通用合成。
2. 右侧不用尚未实现的“人像”占一级页签。建议“调整 / 局部 / 修饰”；点局部调整时明确显示当前名称、蒙版缩略图、显隐/反选、笔刷目标。人像预设可以是内容，不等于自动美肤能力。
3. 底部现在全是同一图的虚拟副本，占约 80 px 却没有服务用户必需的批量流程。改为可折叠真实照片胶片条，明确多选计数、当前图、处理中/失败状态；虚拟副本放比较菜单，首版可整体延后。
4. 批量同步必须让用户选择同步项；默认只同步全局参数，裁剪、蒙版、克隆修饰不直接跨照片复制。队列显示目标目录、命名/覆盖策略、完成/失败/取消；批量任务采用参数快照，编辑当前图不悄悄改队列内容。
5. 顶部的项目条、工作区行、文档页签、工具属性叠了多行。将全局操作和工作区合并；工具属性只在当前工具需要时出现。数值单位明确：曝光 EV；RAW 白平衡 K/tint，普通 RGB 图不伪装成物理相机色温。
6. “导出设置”应成为明确“导出”主按钮 + 可记忆设置弹层；项目保存与导出必须不同。文档位置显示修改/保存状态。对比可选按住看原图、分割比较，不再用虚拟副本暗示已完成独立文档。
7. 首版去掉不可用的选择人物/背景与生成候选按钮。没有模型时不展示假按钮。保留手动画笔/渐变/椭圆等真正交付的选区工具。

### 视觉设计

1. 在 1366×768 与 1920×1080 的 100/150/200% 缩放实测桌面信息密度。320 px 网页无横向溢出对 Windows 摄影体验的证明很弱。
2. 当前 1024×920 截图照片四周留白较多，多个横栏与比较条挤占视口；删去重复栏后把空间返还照片，面板默认约 300–340 DIP 可调，键盘一键隐藏侧栏。
3. 保留浅/深色外壳，但画布衬底单独可选中灰，不随浅色主题变得过亮；色彩评价条件不能完全交给主题。减少低对比细字，关键字段使用一致数字对齐。
4. 每个图标需要可见 tooltip/快捷键、清楚的选中和焦点状态；当前选择箭头、局部工具、波纹等仅凭图形辨认困难。曲线、直方图、蒙版当前状态不能仅靠紫色区别。
5. 显示正在处理/低分辨率预览/完整预览状态，避免代理图被误认为最终质量；计算中仍能取消和编辑。状态栏不要把“RGB 预览”当作有用色彩保证。

### 底层能力与验收

Qt 现成控件适合标准文本、数值与列表；曲线、直方图、图像画布、蒙版、笔刷预览和有语义的调整列表仍是自定义工作。CSS 截图的尺寸/字距不应直接照抄到不同 DPI 的 Windows 字体栅格。

数字框需支持未完成输入（负号、小数）、Enter 提交、Escape 放弃、双击复位、键盘细/粗步进；一次拖杆或一次笔划应合并为一个撤销命令。Ctrl+Z 在文字编辑焦点时不应同时撤销图像。画布坐标需明确屏幕 DIP、物理像素、图像像素的转换，缩放和平移不得损失笔刷落点。

## 一个主线最小实验与失败路线

**在真实 8 GB 核显本上完成一张照片的 Qt 纵向切片：**真实图 → CPU proxy → 曝光/白平衡 → 显示变换 → 中文命名局部调整 → 压感蒙版/克隆点 → 撤销 → 导出设置；另设批量队列 20 张但一次只让 1 张占用完整工作缓冲。测 100/150/200% DPI、跨屏、最小化再恢复、睡眠唤醒、鼠标/真实数位板、微软拼音与读屏器基础导航。

建议通过条件：没有吞字/误触快捷键/丢结束笔划；笔压可见且落点在缩放后不漂移；键盘能完成打开、调参、保存/导出、取消；后台 RAW/批量不锁 GUI；静止无持续忙重绘。性能先以 2 MP 预览预算试测：普通输入至下一次提交 p95 <33 ms，基础调参预览 p95 <100 ms，笔刷可见反馈 p95 <50 ms。它们是首轮目标，内部提交时间不能冒充真实屏幕端到端延迟。

若只有 CPU 显示/调参热点超标：先降代理/限制并发并分析热点，然后替换 CanvasBackend 或给少数算子加 QRhi GPU；不因此换掉 Qt。若 tablet 驱动失败：保留鼠标修饰、修复特定驱动路径并列出实际支持设备；若压感是对外承诺则在修复前阻塞该承诺。若 Qt 全链路按目标机仍达不到最小交互门槛，再以相同切片比较替代后端；egui 只有实测证明净收益才重开选型。此实验阻塞桌面首版体验，AI 实验不应掺入。

单人且熟悉 Qt 的验证投入粗估为 1–3 个专注工程周；需要补驱动/ICC 实测、拿不到硬件时可达 3–6 周。这只是实验工作量区间，不能据此推算完整 v0.1 工期。
