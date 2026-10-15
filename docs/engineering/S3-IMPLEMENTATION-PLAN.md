# S3实施计划：局部编辑、稀疏仿制与资源

2026-10-13。这是启动前准备，不是S3实现完成。来源为handoff/sol-implementation-v1/02-PLAN的S3、03合同、04的A07/A08/A14及用户追加4K/45MP要求；不引入AI、通用图层框架、浏览器引擎或完整保存平台。S2剩余调参/验收见S2-COMPLETION-AUDIT，S3准备不掩盖其缺项。

## 起点与组织

当前SourceFrame为方向归一线性Rec.2020 float32直通alpha；Recipe每图独立、EditHistory一条跨图有序历史。EditLoader有当前准备/导入预解析/4K反馈三路、标签门禁和完整源互斥锁；缓存预算512MiB显示+4GiB会话磁盘。当前关闭仍等待正在跑的LibRaw，未硬取消/隔离。UI已按stage/上下文/card组织，新增局部工具应复用该工作区而非新建大型批处理/图层界面。

按职责组织实际文件，随切片创建：core/document扩展局部/修饰快照引用；core/commands扩展有序命令payload；edit/mask处理源坐标覆盖率和软笔触；edit/clone处理选源与稀疏笔触；render/local_renderer与tile_cache负责CPU局部算子/halo/预算；jobs负责标签/取消与RAW进程；apps/raw-worker独立CLI；app工具控制器只处理指针、焦点和命令，不做图像计算。现有src/app/edit_loader逐步抽取调度职责，保留公开ready/prefetch接口以避免一次重写。架构与协议见S3-ARCHITECTURE.md。

## 切片与实施顺序

| 切片 | 真正要完成的代码/行为 | 通过后再推进的检查 |
|---|---|---|
| S3.0 标签/取消基线 | 增加共享取消状态，新的选择/session先失效旧结果；阶段/行块轮询；UI与后台不共享可变QImage | 取消/切图/Undo/关闭并发，旧结果0错误提交；S2缓存/RAW重叠/颜色/完整源回归保持 |
| S3.1 一条局部曝光笔 | 命名局部调整，源坐标软画笔，增加/擦除、显隐，局部曝光/饱和度；4K重建覆盖率，全源重放同一笔迹 | 各缩放/平移/crop/angle/双轴flip/DPI位置一致；一笔一撤销；失焦/切图结束或取消笔划，Esc取消未提交；透明边缘与负值策略明确 |
| S3.2 稀疏选源仿制 | 显式选源，源/目标相对位移，软覆盖率，冻结取源基底，记录笔迹而非全图RGBA副本 | 在两个代理尺寸及完整源中同位置取样；跨tile源请求与halo；Undo/Redo恢复像素；不能冒称自动修复 |
| S3.3 tile/资源实验 | 256/512两种逻辑tile，算子support决定halo；当前ROI优先，LRU与pin/在途预算；用CPU参考核对 | 合成24/45MP+R7，冷/暖各多轮；全进程私有提交/working set/临时磁盘，错位/接缝0；真实45MP RAW仍待外部覆盖 |
| S3.4 RAW隔离worker | QProcess独立RAW进程、版本化小控制消息+会话私有二进制输出；单RAW在途；超时/崩溃/取消清理 | 人为中断在open/unpack/process/写出；父窗口可继续4K调参，源不变，只有最终有效结果提交；子进程退出和DLL部署记录 |
| S3.5 扩充与收尾 | 在正确画笔基础上加线性/径向渐变、反选/羽化/颜色亮度范围，统一局部列表与修饰工具上下文 | 文档重开/释放100轮无增长趋势；多命名局部独立；S2全部回归；留下S4项目/同步/导出接入点 |

每个切片先做CPU参考与小合成图，再真实R7；没有准备好的局部功能不展示假入口。S3.1/S3.2在其原图重放通过前不能只用4K编辑PNG当永久真相。渐变与范围为共同能力扩充，须有真实算子和历史后再开放UI。

## 资源与验证门槛

起始预算沿现有验收目标：稳态进程树私有提交≤1.5GiB，RAW/完整计算峰值≤2.5GiB，另报working set/GPU/磁盘。它们是待实测目标，不是已达成承诺。45MP RGBA32F单缓冲约687MiB，不能同时无限保留源、RGBA输出、mask与Undo副本。笔触与局部参数为小不可变记录；稠密mask或tile可重建，仅缓存。

建议新增s3_tests和tile_probe/worker_probe，命令形式遵循现有tools/dev.ps1与可选LUMINA_REAL_SAMPLES；当前未创建这些空target。默认合成测试：源坐标双向变换、覆盖率/羽化、笔迹重放、alpha、clone跨块、256/512参考差异、单笔历史、旧任务门禁。原生Qt：真实拖动/失焦/IME/Esc、stage切换、缓存导入重叠。故障：worker错误启动/非法协议/超时/强杀/写盘失败/输出hash错误/源变更。资源：父子进程采样、退出确认、10张循环与100次文档开关；不是靠单次RSS或只测QtTest主进程宣称无泄漏。

## 可参考方案与许可边界

- [Qt 6.8 QProcess](https://doc.qt.io/qt-6.8/qprocess.html)：外部进程与异步I/O、finished/error状态；使用绝对exe路径和QStringList参数。避免GUI线程waitForFinished/大流量stdout。6.8中已有API足够，不因在线文档默认6.12升级工具链。
- [LibRaw API说明](https://www.libraw.org/docs/API-notes.html)：独立对象/生命周期与错误处理；本地0.22.2头文件已有set_progress_handler与LIBRAW_CANCELLED_BY_CALLBACK。取消回调只能协作结束，不能保证所有解码阶段马上响应，隔离进程才提供故障边界。
- [Qt 6.8 QThreadPool](https://doc.qt.io/qt-6.8/qthreadpool.html)：线程池用于CPU行块/局部任务；clear仅清未开始任务，不能替代在途取消。维持有限并行，不增加未经测量的全图队列。
- [OpenCV滤波教程](https://docs.opencv.org/4.x/d4/d13/tutorial_py_filtering.html)：可作为有限核与边界处理概念参考；此处未引入OpenCV库或复制其实现，正式算子须先锁定halo、坐标、误差/许可。
- [Camera Raw蒙版](https://helpx.adobe.com/in/camera-raw/desktop/edit-and-enhance-images/masking-and-local-adjustments/masking.html)、[像素蛋糕局部工具](https://wiki.pixcakeai.com/guide/9399.html)与[手动修饰](https://wiki.pixcakeai.com/guide/9425.html)：参考用户操作分类，算法独立实现，不搬品牌素材或假AI。

以上为架构决策与准备，不是性能证据。实施中保留LCMS核心/LibRaw当前许可和源码来源，新增依赖先查许可；没有选定正式项目LICENSE。

V13基线补充：S3切片测试需组合新增参数曲线与亮度点曲线验证局部覆盖/仿制重放、近黑和外域数据，并复用S2的共享不可变LUT与完整recipe键；只更新准备计划，未提前实现S3局部功能。
