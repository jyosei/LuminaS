# D2 协议、浮点帧与本地进程基础

2026-10-17。**D2尚未完成，生产RAW仍直接调用源核，不能宣称已经隔离或提速**。本批实现协议、payload、worker CLI、后台父端第一片；后续继续故障注入、生产接线、原生/资源审计及D3–D5。S3完成前不推送、不打包。

## 实现范围

- `src/raw/source_kernel.h`：从现有readSource提取原核并增加可选阶段观察，不更改LibRaw AHD、WB、矩阵、线性Rec.2020计算。原readSource目前仅转调该核。
- `worker_protocol`：protocol/algorithm=1，依赖版本匹配的Hello；严格Request/Cancel字段、UUID、源SHA、数值/几何白名单和规范单行JSON。DecodeFull禁止烘焙几何；Prepare固定长边3840、小图不放大，保留现有几何源支持尺度。
- `worker_payload`：32字节little-endian头、严格metadata、packed RGBA32F。尺寸/字节数先检验再分配，保留负值/超白，拒绝NaN/Inf/非法alpha/矛盾矩阵、hash或长度。文件与像素分别SHA；临时文件真正关闭后无覆盖提交。
- CLI：固定一个请求；源指纹核验→原核→可选几何4K→私有帧→Done。独立控制线程只发原子取消，不触碰GUI或图像。stdin为单消费者PeekNamedPipe/可用字节读取/2ms空轮询；停止500ms保护，无detach。
- 父端：拒绝GUI线程阻塞调用；可取消全局单槽；固定同目录程序，派发请求前建立KILL_ON_JOB_CLOSE；取消/超时尝试协作后终止Job。严格Hello/消息顺序/身份/阶段，stdout总量8MiB、16384消息、单行1MiB，stderr留16KiB。子进程正常退出且Job计数清空后才分配导入大帧；再次校验源。私有QTemporaryDir随任务释放，不扫描任意TEMP。

这不是最终的PreparedFrame lease或生产EditLoader接线，不能拿单命令行成功替代D2的完整可靠性要求。

## 实际检查

构建/专项：

- `s3-d2-transport-handle-build.txt`、`s3-d2-worker-cli-build.txt` exit0。
- `s3-d2-transport-handle-ctest.txt` exit0，transport Qt10 passed。
- `s3-d2-worker-pipe-poll-ctest.txt` exit0，两项2/2、1.93秒；process Qt7 passed、1个实照可选skip。
- `s3-d2-client-drain-build.txt` exit0；`s3-d2-client-drain-ctest.txt` exit0，client专项1/1、1.06秒，Qt6 passed。覆盖后台Full/Prepare逐行浮点一致、主线程拒绝、源SHA错误、预取消、取消后槽释放。
- **所有目标构建exit0、完整70/70通过（420.72秒）**，日志`s3-d2-foundation-full-build.txt`、`s3-d2-foundation-full-ctest.txt`。transport10 passed；process7 passed/1个可选实照skip；client7 passed/1个可选实照skip，含新增收到子进程hashBefore阶段后取消且下一请求成功。默认skip不代替此前独立R7对照证据，也不扩大为未实现的故障注入通过。

实际R7 `4X1A9579.JPG/.CR3`，源6960×4640。可选私有样片用例由GPT单独执行：

| 路径 | JPEG | CR3 |
|---|---:|---:|
| CLI原核对照（Full+Prepare） | 38.0129534秒、exit0、3 passed | 42.7508173秒、exit0、3 passed |
| 父端跨进程对照（Full+Prepare） | 36.1628769秒、exit0、3 passed | 42.3163221秒、exit0、3 passed |

记录分别为`build/private-qa/s3-d2-worker-r7-{jpeg,cr3}.{txt,json}`和`s3-d2-client-r7-{jpeg,cr3}.{txt,json}`。每次包含原核参考及跨进程两条计算、全幅与4K两个操作、传输/hash；**不是单张读取基准，不计算提速倍数**。每次先释放参考全幅再启动子进程，没有为了对比同时保留两幅完整源。

Prepare采用crop(.1,.1,.8,.8)、angle5；CR3另用warmth=.25/tint=-.1。矩阵/WB/源尺寸保持，Prepare3840×2560，源SHA不变，逐行RGBA32F像素SHA完全一致：

| 内容 | SHA-256 |
|---|---|
| JPEG源文件 | `40dd3b69da039d7b6f492dd731cf49429d57eed4e633554014a973b6623c2893` |
| CR3源文件 | `1cc9a37d72cc02c137b0db12e17e7ea3d4176ed30058f9342a9fed2a663d2856` |
| JPEG完整线性源 | `adc162966969fb511c37ccbfb9d1d1283dd6d8b800699fd4e2acb0e1c3485ad3` |
| JPEG Prepare | `8cf713ff41d587250b9f07f2d701d2be5ca7b734f6c7b5ea8260077ea042fa9e` |
| CR3完整线性源 | `d5c371e5e69bed6d48e8669893d76a0ef6eb2bf46796b2f946258b9fd08e4a36` |
| CR3 Prepare | `bfdaefaa94255edb871c8f02d76cb2a079661f30ae44d0850eae02d38bdbf6c9` |

父端实际对照发生在新增进度观察回调前；回调不改变核，新增的阶段取消用例随后随完整70项通过，不把合成阶段取消说成真实RAW各阶段强杀。

## 已发现并修正，失败证据保留

1. QTemporaryFile.close仍保留Windows文件句柄，MoveFileExW返回ERROR_SHARING_VIOLATION(32)。改为先结束临时文件对象作用域，再无覆盖提交，由单独路径owner清理。原4项失败保留`s3-d2-transport-first-failure.txt`；修后10 passed。
2. 父端保持stdin打开时，阻塞读控制线程经CancelSynchronousIo仍未在500ms退出，已发Done但exit3。诊断`s3-d2-worker-exit-diagnostic-ctest.txt`；初次失败`s3-d2-worker-process-first-failure.txt`。改为有数据才读后两正常用例通过，不靠关stdin规避测试。
3. 首次父端测试失败两项：进程退出后的Job空判定未立即通过，增加最多500ms有界排空；原诊断未区分会计仍非零与查询失败，不能把延迟解释当作逐事件追踪结论。测试QFuture.waitForFinished可能把任务偷到主线程，改std::launch::async确保测试后台线程。`s3-d2-client-first-failure.txt`保留，修后专项通过。

## 未完成/未验

- 父端错误版本/无响应/崩溃/重复Done/错误tag/源在途变化的故障注入；RAW具体阶段取消、进程树强杀与父进程崩溃后的证据。
- Job分配失败的故障注入。当前基础宿主中真实Job分配与退出已执行，不推断其他宿主均支持嵌套Job。
- 生产Prepare/fullGeometry/lease缓存、切图/Undo/关闭的原生响应；不能放大4K替代完整源。
- 残留会话的归属核验与回收、进程树private commit/working set/磁盘峰值，D3资源与D4百轮释放。
- 真实45MP RAW、8GB设备、物理磁盘满/权限及外部存储I/O挂起；R7约32MP不当45MP证明。
- D5含局部层的完整源导出桥接与最终原生/全量验收。没有把已有Recipe-only导出称为含局部层完成。

## 协作与清理

DeepSeek仅获得选定项目文本，返回payload审查、CLI/测试/文档草稿及父端静态审查。GPT拒绝草稿中的虚构Frame/取消API、错误WB范围、主线程等待等，按真实接口修正并自行构建/执行；worker的“未发现”不是通过凭据。无私照/缩略图/截图发送。输入范围本地校验失败的调用未自动重试。token实际用量见`.tools/deepseek/usage.jsonl`，不据此推算余额。

本批未生成截图或独立成片；测试私有浮点帧由QTemporaryDir正常析构清理，只保留文本与hash。先前两处合计40060054字节的超时测试残留仍因自动审批拒绝未删除，未用其他工具绕过；无新增删除尝试、包或推送。

Windows API语义参考[PeekNamedPipe](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-peeknamedpipe)和[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)。PeekNamedPipe在同步句柄、多线程情况下也可能阻塞，当前设计仅一个输入读取者、无并行输入I/O；当前宿主测试通过不替代跨宿主/操作系统挂起验证。
