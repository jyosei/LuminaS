# D2 故障、进程树与会话归属

2026-10-17。本批继续D2，**生产读取链尚未切换，D2及整个S3仍未完成**。不推送、不打包；最新完整回归仍是7b3b006基础版本70/70，不能将本批专项结果冒充新完整72项通过。

## 实现

- `worker_client_testing.h`为内部测试接缝：故障程序、私有根、期限、拒绝Job分配与PID/会话观察，仅测试调用。生产`runWorker`仍固定同目录程序，无环境或UI路径替换开关。
- 父端成功派发前记录父/worker PID及Windows进程创建时间，两个不可覆盖的独立JSON标记带校验值。PID数值复用不视为原进程仍存活。无法确认身份时拒绝派发。
- 正常/失败退出后先回收Job及worker，再清理本任务会话；若进程树无法确认结束则保留目录，不提前删仍可能在用的缓冲。
- 再次使用RAW根时，仅扫描该根下受控`session-xxxxxx`目录。默认至少60秒、标记校验正确、父和worker都已结束、文件集合全为标记/对应token帧/受控stage文件，才逐文件清理；不用递归删除。Windows目录句柄拒绝reparse point并在删除文件期间防止目录重命名。
- 缺少worker身份而存在payload、记录损坏/过大/异根/非规范JSON、活动进程、访问或查询失败、未知文件/子目录/链接均保留。尚未派发且仅有owner标记的空会话可在父进程结束后清理。只删除明确归属的私有缓存，不扫描系统TEMP、照片或旧QA目录。

校验值用于识别损坏与错配，不宣称它是抵御同账号恶意进程的认证机制。外部存储I/O或操作系统拒绝终止等极端边界仍需如实保留；部分清理失败保留记录以供后续检查。

## 实际构建与专项

GPT实际运行并观察终态：

- `s3-d2-fault-parent-build.txt` exit0，最初修正后fault Qt16 passed、CTest1/1（5.86秒），日志`s3-d2-fault-parent-first-ctest.txt`。
- `s3-d2-owned-session-build.txt` exit0；client/fault/session 3/3（7.74秒），日志`s3-d2-owned-session-first-ctest.txt`。
- `s3-d2-tree-guard-build.txt` exit0；包含进程后代及父进程强杀的fault Qt18 passed，CTest1/1（7.29秒），`s3-d2-tree-first-ctest.txt`。
- `s3-d2-cancel-queue-build.txt` exit0；排队取消后相关3/3（9.03秒），`s3-d2-cancel-queue-ctest.txt`。
- **最终相关五项5/5（10.33秒）、exit0**：`s3-d2-reliability-focused-build.txt`及`s3-d2-reliability-focused-ctest.txt`。transport10 passed；process7 passed/1个私有实照skip；client8 passed/2个私有实照skip；fault18 passed；session8 passed。默认skip项由单独实照命令覆盖其相应范围，不能写成默认全量实照通过。

以上是核心库、worker及专项目标构建，本批尚未重新构建所有目标或运行完整72项；此前70/70仍单独保留。

## 故障与回收证据

专用故障程序只使用临时生成的96×64 JPEG，生成的RGBA32F帧为98,304像素字节。覆盖：正常成功、错误Hello、无Hello、请求后无响应、退出7、错误UUID、重复Done、Done后杂音、缺LF、Done后异常退出、payload损坏、超1MiB行。每项断言原源字节/SHA不变、无错误帧发布、持有的实际进程HANDLE变为signaled、私有会话已由client移除，并能随后执行真实worker的小图读取。

正常模式必须得到真实成功帧，不能将“所有模式均失败”当作故障测试通过。检查目录是否移除发生在测试自身根目录析构之前，避免测试清理掩盖client泄漏。

- 初次实际无Hello/卡住约1015/1123ms；该时间含配置为1秒的测试期限，不是产品180秒期限。
- Job分配拒绝注入：不给Request、不发布帧，启动成功回调不调用；清理后后续请求成功。不是在所有宿主实际制造Job分配失败的证明。
- 源在payloadReady回执后由测试修改自己的JPEG：父端拒绝导入，下一任务正常。
- 一个请求占用全局槽时，另一个请求在排队阶段取消，没有收到子进程进度；首请求解锁后正常完成。
- 超时：故障worker先产生真实私有float帧，再启动专用后代并保持运行；父端期限触发后，持有的worker与后代HANDLE均signaled，未依赖子进程C++析构函数。
- 父进程强杀：另起真实驱动进程持有Job、worker和后代；准备记录出现后，测试持有两级HANDLE再精确kill该驱动进程。两级进程均结束；按不可变归属记录回收遗留帧/会话，驱动源JPEG字节不变。没有遍历/终止其他进程。此项是小合成输入，不能说成真实R7各阶段父进程强杀。
- 会话：活动owner、死owner但仍活动worker、未知文件、损坏/异根记录及缺worker证明的payload均保留；真实已终止的记录通过年龄门禁后回收。未声称实际触发了操作系统PID复用，创建时间判据由代码与受控记录验证。

## 真实R7取消

单独运行`raw_worker_client_tests actualRawStageCancellation`，`LUMINA_WORKER_SAMPLE`仅在GPT本地指向`pictures4test/4X1A9579.CR3`。3 passed、exit0，整套6.0359997秒，记录`s3-d2-r7-phase-cancel.{txt,json}`。接收到指定current=0回执后取消：

| 回执 | 从任务发起到取消返回 |
|---|---:|
| rawOpen | 416ms |
| rawUnpack | 420ms |
| rawDevelop | 570ms |
| write | 3577ms |

这是包含达到该阶段的总时间，**不是取消反应时间**；收到回执不证明LibRaw当时仍停留在该内部指令。均不发布帧；每次之后的小JPEG任务正常；R7原片SHA前后一致。此前R7全源/4K数值等价证据见S3-D2-FOUNDATION-RESULTS.md，没有因为本批回收测试重复声称新的数值基准。

## 协作审计

DSH新会话`ae36cfc4-7b27-460a-8957-f977cf6588e7`限定两份故障测试文件，但实际额外读取CMakePresets、少量client/protocol/payload工程文本及build目录名，违反给定读取范围。GPT停止该会话，不继续使用；审计未发现图片/缩略图/截图或凭据读取。完整实际工具参数保留在忽略目录`.tools/audit-ae36cfc4-7b27-460a-8957-f977cf6588e7.json`。

草稿有反向握手、虚构setup消息/接口、不可移动返回对象、把正常模式也断言失败，以及测试根先析构掩盖会话泄漏等问题，未作为验收接受。GPT保留草稿、修正后自行构建执行；工作者的构建调用随取消报告failed，没有可用测试终态，不声称其已跑通。实际日志去重计量：input22,094、cacheRead394,880、output12,181、total429,155 tokens，不推算RMB余额，无自动付费重试/充值。

本批未输出截图或成片；私有测试帧正常回收，仅留文本/hash。先前40060054字节的两个旧超时测试目录不在本清理根范围，自动审批拒绝后仍保留，未绕过。

## 下一片与未验

1. 生产EditLoader预解析使用Prepare；需要完整几何支持时仍使用真实完整源，不能放大4K代理。保留RAW WB、翻转分层、局部源坐标、代际门禁及现有实时缓存。
2. 盘点并接线公共readSource、导出转换预览及完整源导出；CLI/测试显式移到后台调用，不弱化GUI线程拒绝。Window已跳过旧JpegLoader的RAW缩略图分支，该旧API仍在代码中，不能将它和实际窗口调用混为一谈。
3. `export_metadata.cpp`目前还有父进程LibRaw.open_file，尚未隔离；必须处理或保持未完成标记，不能据本批称“所有RAW解析已隔离”。数值链与元数据过滤/GPS保护不能随便改变。
4. 生产原生切图/Undo/关闭、实际R7批量、45MP边界、进程树资源及磁盘预算、D4百轮和D5含局部层全源出口继续。真实45MP RAW和8GB设备仍缺验证证据。
