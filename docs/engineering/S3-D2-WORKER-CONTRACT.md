# D2 RAW 进程实现接线合同

2026-10-17生产EditLoader更新：Prepare 4K与真正全源几何支持已接线；所有目标构建exit0、完整73/73（404.66秒）、真实R7有局部层的两分支像素/完整源SHA、原生项目往返通过。严格首绘核验54–690ms，首批12张仍约78秒；读图速度问题未关闭。其他公共读取、导出/RAW元数据尚需隔离，见S3-D2-EDIT-LOADER-RESULTS.md，不宣称D2完成。

2026-10-17可靠性更新：故障、排队/真实RAW回执取消、Job后代及父进程强杀、封印归属记录与保守孤儿回收已实现并通过相关5/5（10.33秒），见S3-D2-RELIABILITY-RESULTS.md。下列早期“未完成”状态由该记录对应证据更新；生产接线/原生资源及父端RAW元数据open仍未完成，不据此关闭D2。

2026-10-17，GPT 架构决定。本合同接续 S3-ARCHITECTURE 的 RAW worker v1；不改变解码算法、色彩空间、全源输出或生产 GPU。**D2仍未完成**：协议、二进制帧、CLI及父端第一片已实现，生产路径尚未切换。D1最后完整回归67/67通过。

当前切片：所有目标构建exit0，完整70/70（420.72秒）通过。真实R7 JPEG/CR3在CLI的全源及4K Prepare与原核逐像素SHA相同、矩阵/WB与原片SHA保持，整套对照分别38.013/42.751秒；父端跨进程对照也通过，分别36.163/42.316秒，不能当单次读取基准。默认CTest跳过需显式样片路径的私有用例，实照由GPT另行验证，详见S3-D2-FOUNDATION-RESULTS.md。

父端采用后台串行槽、固定同目录程序、派发前Job Object、取消/超时回收、私有会话及退出后大帧导入。基础实际宿主嵌套Job接线已随client测试运行；故障注入/父进程强杀/进程树与资源审计、生产EditLoader接线、完整几何支持、孤儿会话清理仍未验/未完成，不能据CLI或父端基础测试宣布D2收口。

## 实施次序与不可替代项

先锁定协议/二进制传输及故障测试，再接 worker CLI，随后接父端和当前 EditLoader、取消/关闭，最后实照/原生/进程树验证。整个 D2 仍需 Prepare 4K 工作帧和原有几何支持，不能只隔离一个命令行 probe 就算完成。

第一片可用 DecodeFull 原样调用现有源读取核，证明跨进程结果与原有 SourceFrame 一致；它只是实现中间态，不替代 Prepare 4K。Prepare 只处理原有 RAW WB/几何前缀，不烘焙全局曝光、曲线、局部层等后续编辑。父端仍需精确几何反馈所依赖的完整源支持，不能悄悄拿4K缓存充当完整源。完整引用/私有文件 lease 的生命周期必须与旧在途任务一起计入 D3，不能靠把内存转成磁盘漏计资源。

生产调用固定同包绝对路径的本地 `LuminaS-raw-worker.exe`，不搜 PATH、不经 shell、不接受环境变量指定替代程序。测试可对独立 client 注入专用故障程序，生产入口不暴露此开关。缺 worker 或版本不匹配应清晰失败，不能静默退回无法隔离的 RAW 解码。JPEG保持原有路径。

## 控制与版本

- stdout仅逐行紧凑JSON，最大单行1MiB（包括转义路径/≤256KiB源事实），超过上限或无换行积累超限立即失败；stderr有界保存诊断，不把大块图像写到stdout/base64。
- Hello声明protocol=1、source/geometry algorithm版本、LibRaw实际版本、LCMS实际版本、Qt版本、little-endian float32约定。协议/算法/库版本必须与父端所用实现匹配；不把将来最新库升级夹进此任务。
- 一进程一请求、一次仅一个RAW解码在途。Request包含独立job UUID/tag、源绝对路径/预期SHA、WB、操作（DecodeFull/Prepare）、几何/目标边长（仅Prepare）、会话输出token。父端把内部UUID对应回原请求，现有EditTag所有门禁继续保留。
- Request严格拒绝未知操作、缺字段、非法类型、非有限数、越界尺寸及无效参数。几何使用已有合法配方的前缀字段，未知全局调色字段不能悄悄烘焙或忽略。45MP及原有宽高/字节限制保持。
- Progress只报告实际phase/current/total，不伪装ETA。Done带相同job UUID/tag、源SHA、归一原尺寸、工作尺寸/支持尺度、raw WB/matrix/facts、固定二进制文件token、精确字节数和payload SHA。Error带明确code及有界文本。重复Done、错误tag、乱序或非JSON输出都不能提交。
- 每次读取的时间/transport诊断继续放在SourceFrame的readStages/elapsedMs里，不能新增会污染持久化sourceFacts的运行计时字段。D1已复现过该错误。

## 大帧交接与源保护

二进制放父端新建的专属会话目录，固定受控文件名和token。没有任意用户输出路径；拒绝已有目标、路径逃逸、链接目标以及不匹配token。先同目录临时写入再原子提交，不启用direct-write fallback。

明确头部/metadata长度、packed RGBA32F行宽、宽高和精确像素字节数；乘法先用有界64位检查，拒绝截断/尾随数据、hash不一致、非法float、矛盾尺寸或矩阵/WB事实。维持straight linear Rec.2020 D65和现有负值/超白/透明边缘策略，不加未经验证的新裁剪、gamma或量化。源数据全程只读，原有解码前后SHA核验保留；导入前再检查源身份未变。

DecodeFull阶段父端等子进程真正退出后再分配/导入全幅QImage，避免父子同时持有两份完整RGBA缓冲。不能把单进程低值冒充进程树峰值。Prepare或映射文件后续仍需验证原始源尺度、cached fullGeometry生命周期及渲染基线。

## 取消、退出和Windows边界

QProcess在已有后台任务的线程中创建和使用。GUI只更新tag/取消token，不做阻塞wait。后台client可小步等待并检查共享token，等待串行槽同样可取消。允许一个RAW在途，不建立无界子进程队列。

worker主线程保留现有同步源核、独立控制线程读取stdin，仅把Cancel/协议错误/EOF映射到共享原子取消状态，不从控制线程触碰GUI或可变图像。实际测试发现CancelSynchronousIo不能让当前宿主的空管道读取按500ms期限退出，Done已发出仍exit3；该失败已保留。当前改用单消费者PeekNamedPipe，只读取已报告可用的字节，空管道Sleep(2)并重查停止标志。Done/Error之后父端关闭stdin；即使父端保持输入打开，stop也应退出并join。500ms退出保护仍保留，超时明确失败并终止自身，不detach；不声称CancelSynchronousIo已实测有效。

父端先使旧tag无效，发送Cancel，给有界短宽限期，再终止进程树兜底。LibRaw回调读取消token，不宣称open/unpack每阶段都可立刻协作结束。Worker启动后、派发Decode前先分配Windows Job Object并设置KILL_ON_JOB_CLOSE；分配失败则拒绝解码并终止子进程，不假称已有子进程树保护，不采用breakaway绕开宿主限制。嵌套Job的宿主兼容性要实际验证。

退出/崩溃/超时/协议错误不发布半帧。确认子进程/Job结束后，只清理本任务拥有的临时目录。父端强杀的残留只能在明确归属的应用会话根目录内按token/进程存活/时间核验后回收，不扫描任意系统TEMP；没有这个证据就登记未验，不用删除其他目录代替。

## 必须给出的实际证据

1. 协议/格式：正确往返、未知版本、错误UUID、超长/畸形JSON、尺寸溢出、矩阵/WB矛盾、NaN/Inf、截断/尾随数据、坏hash、未知已有目标和路径逃逸。
2. 父端：启动失败、Hello不匹配、worker崩溃、无响应超时、重复结果、取消在排队/解码/写出/读取时、源变化。断言无错误缓存/帧提交、旧tag不达画布。
3. 实际进程：open/unpack/process/写出等阶段人为中断；Job分配/强杀、关闭后无存活子进程；IPC输入线程退出无死锁，失败项后下一个任务可执行。
4. 算法等价：原核与worker的R7 JPEG/CR3及WB/几何4K输出/完整源引用同数值与hash；没有真实45MP RAW时保留未验项，合成45MP仅证明边界。
5. 原生：取消/切图/Undo/关闭时界面可响应，已缓存4K调参继续；过程时长与端到端、private commit/working set/磁盘/父子进程分别记录。生产UI与现有全套回归保持。

## 参考边界

采用当前本地Qt6.8 API，不因在线6.8维护文档更新升级本地6.8.3。官方[QProcess说明](https://doc.qt.io/qt-6.8/qprocess.html)区分信号/等待和运行线程；[Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)说明嵌套Job、进程树和KILL_ON_JOB_CLOSE行为；[同步I/O取消说明](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations)用于评审控制线程的退出。引用文档不代表已验证实际宿主、强杀或I/O取消。
