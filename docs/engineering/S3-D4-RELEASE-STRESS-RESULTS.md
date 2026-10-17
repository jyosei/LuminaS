# S3-D4 百轮释放压力与内存诊断

2026-10-17。DeepSeek整理前置报告草案，GPT核对运行日志/数值并完成后续诊断。本机100轮功能与预热后资源释放验收通过；S3仍等待修正后的完整回归，不先打包或推送。

## 最终百轮堆诊断

`s3-d4-heap-diagnostics100.*`：100轮（90合成/10 R7 CR3），Qt 3 passed/1022138ms，外层exit0/1023.7199305秒。每轮完整几何/非空局部/真实4K绘制及weakExpired通过；100轮结束widgets/windows均0，GDI23、句柄稳定。源文件前后SHA相同。

正常每轮测量在窗口销毁并等待1秒后进行，**在任何压堆/清缓存之前**。首次约30轮填充Qt全局像素缓存，之后活跃堆分配进入平台；末40轮私有提交组均值也未继续上升：

| 0起轮次 | allocated均值B | private均值B | private最小–最大B |
|---|---:|---:|---:|
| 0–19 | 7,604,661.50 | 24,460,083.2 | 16,211,968–31,674,368 |
| 20–39 | 13,847,066.00 | 37,636,915.2 | 30,576,640–50,044,928 |
| 40–59 | 14,520,882.15 | 47,298,355.2 | 39,215,104–50,839,552 |
| 60–79 | 14,591,618.35 | 50,649,497.6 | 42,094,592–54,673,408 |
| 80–99 | 14,147,775.70 | 50,512,281.6 | 41,115,648–54,255,616 |

所有正常测量完成后才做归因探针：beforeTrim allocated14,646,966B/private53,534,720B；`_heapmin()`后allocated14,648,054B/private不变；清QPixmapCache后allocated3,908,416B/private29,184,000B。首轮allocated3,932,277B，清缓存后没有百轮累计的活跃分配。堆提交/保留未强制归零，也没有向生产代码添加压堆或清缓存操作。

因此本用例的增长归为有上限的Qt缓存预热与分配器保留，预热后的活跃分配及私有提交无持续增长趋势，D4通过本机百轮验收。[Qt QPixmapCache文档](https://doc.qt.io/qt-6.8/qpixmapcache.html)说明全局缓存默认上限10240KB，满后淘汰较旧项；本项目未修改该上限。**不要求OS回零，不以末尾清缓存数值冒称正常编辑稳态，也不外推无限时长或8GB设备。**

1993个500ms进程树样本，8次内存查询缺测；采样private峰1,502,990,336B、working set峰1,517,244,416B、owned disk峰674,418,957B，末尾disk0/remainingKnownAlive空。采样非连续精确峰值，短命子进程由D2 Job/handle用例另证。阶段统计、完整样本与摘要保留在`.phases.json/.resources.json/.summary.json`，不保存截图或成片。

以下保留前置失败及当时未通过的判断，不以最终结果抹掉。

## 1. 前一百轮结果（当时未关闭）

- 第二次百轮运行完成：100 轮（90 合成 4500×3000 sRGB JPEG / 10 私人 R7 CR3），Qt `3 passed` / `1017181ms`，外层 exit 0 / `1018.4297727s`。
- 每轮窗口销毁后 `weak_ptr<const SourceFrame>` 均过期；原图 SHA 全程不变；正常结束时 owned temp 磁盘为 0；已知子进程为空。
- 内存门槛 **未通过**：释放后父进程 private 每 20 轮均值持续上升，最终 `55885824B`，不能以弱引用过期宣称无泄漏。
- 第一次百轮因 QTest 默认函数看门狗 300000ms 在第 29 轮超时终止，**不能据此判定产品崩溃**。

## 2. 首次尝试（看门狗超时）

- 现象：300000ms 默认看门狗于第 29 轮结束，`QFATAL Test function timed out`，退出码 `-1073740791`。
- 归因：QTest 的 `QTEST_FUNCTION_TIMEOUT` 机制（参见 Qt 6.8 QTest Overview 的 Increasing Test Function Timeout 一节），属测试框架超时，不作为产品崩溃证据。
- 日志保留：`s3-d4-release100.*`。该结果不被后续成功结果覆盖。

## 3. 第二次运行（仅调测试看门狗）

- 改动范围：仅将测试看门狗增至 `1800000ms`，helper 超时 `1860s`；未改被测逻辑。
- 轮次构成：90 合成 / 10 RAW（私人 R7 CR3）。
- 结果：Qt `3 passed` / `1017181ms`；外层 exit 0 / `1018.4297727s`。
- 每轮门禁：窗口销毁后弱 `SourceFrame` 引用均过期；合成与 RAW 逐源 SHA 前后一致；owned temp 磁盘末尾为 0；进程树残留为空。
- 日志：`s3-d4-release100-long.*`（不含截图）。

## 4. 采样与内存观察

- 采样：500ms 间隔共 1984 个样本，其中 11 个不完整。
- 进程树峰值（非连续峰值）：private `1505439744B`，WS `1498685440B`，owned disk `674418982B`。
- 释放后父进程 private 每 20 轮均值：

| 区间 | private 均值 (B) |
|---|---|
| 1–20 | 24454348.8 |
| 21–40 | 36983398.4 |
| 41–60 | 47302246.4 |
| 61–80 | 50592563.2 |
| 81–100 | 53953126.4 |
| 最终 | 55885824 |

- 判读：存在增长趋势，**D4 内存门槛未过**；弱引用过期只证明对象生命周期结束，不等于堆/窗口/句柄无泄漏。
- 未设帧率门槛，不强制 30/60fps。

## 5. 20轮诊断（尚未关闭内存门槛）

- 新诊断构建 exit0（`s3-d5-model-d4-diagnostics-build.txt`），原生20轮18合成/2 R7 CR3，3 passed/223369ms，外层exit0/224.2715853秒；原图SHA一致，weak全部释放。日志`build/private-qa/s3-d4-release-diagnostics20.*`。
- 所有轮释放后widgets/windows均0，GDI对象23，USER对象104–105，句柄563–568。private由首轮16728064B增至末轮33415168B。
- 循环结束后beforeTrim33427456B，`_heapmin()`后仍33427456B；清空QPixmapCache后24420352B。约9MB可归于像素缓存相关保留，但其余增长仍未定位，不能据此判定无泄漏。
- 下一步仅在测试内记录Windows各堆的allocated/committed（缺API/失败明确记录），区分活跃分配与提交保留；不对生产渲染进行强制清缓存或压堆。
- 后续HeapSummary诊断20轮完成：18合成/2 R7 RAW，3 passed/205251ms，外层exit0/206.3476759秒，源SHA不变。日志`s3-d4-heap-diagnostics20.*`。6个堆全部可查询；末尾allocated11732172B、heap committed38133760B、process private34873344B；压堆后allocated11733260B、committed33415168B，private仍34873344B；清PixmapCache后allocated4270758B、private25579520B。首轮allocated3926625B。实际分配的增长主要可被缓存解释，但20轮不足以关闭百轮趋势；各指标不同口径，不直接互减推算泄漏字节。
- 测试动态解析[Microsoft HeapSummary](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsummary)，结构按[HEAP_SUMMARY](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/ns-heapapi-heap_summary)布局；不记录堆地址/内容，无API时明确available=false。
- 早期三轮 16.4→20.8→22.0MB 的小幅增长仍不能宣布无泄漏。

## 6. 残留与清理受阻

- 首次超时运行遗留 `157706325B` 合成 JPEG 与 work 目录；清理被自动审批拒绝（blocked by policy），**保持保留，不绕过**。
- 另有旧 2 个目录合计 `40060054B`，同样清理受阻。
- 证据：`build/private-qa/s3-d4-watchdog-retained-evidence.json`。
- 两次正常运行的空临时根已核对为空并删除，记录`s3-d4-success-empty-root-cleanup.json`。后续heap20的空根删除也被自动审批拒绝，仍保留，记录`s3-d4-heap20-retained-cleanup.json`；不替换命令绕过。

## 7. 状态

- D4最终百轮结果以上述新诊断为准，已通过本机用例；20轮和首次百轮的未通过判断保留为历史。
- S3修正后的完整81/81、428.33秒、exit0已通过，GPT终审见S3-ACCEPTANCE.md；进入功能冻结、授权上传/开发迁移包，随后停止。
