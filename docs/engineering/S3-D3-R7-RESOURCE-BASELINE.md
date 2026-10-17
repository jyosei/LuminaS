# D3 R7 资源首轮基线

2026-10-17。仅一次12张预加载与空间调整实验，**D3多轮趋势及D4百次释放未完成**，不是首次导入提速结果。

## 实际执行

`s2_tests spatialSteadyResources`，Windows原生平台，6组R7 CR3/JPEG共12张；导入后确认全部预处理，切到已缓存照片，应用texture=.4/clarity=.3，断言不重建源且输出3840×2560；稳定等待5秒后销毁窗口。GPT实际构建`s2_tests`退出0（`s3-d3-resource-control-build.txt`），执行3 passed/87591ms，外层exit0/88.4376812s。控制文件只在显式设置`LUMINA_RESOURCE_CONTROL`时生成，Qt API给出真实缓存路径，在导入前额外等待1500ms供采样器附加；默认回归无该控制逻辑。无截图或成片输出。

主进程PID27572，采样间隔500ms，172轮。统计明确的本次TEMP根与Qt测试应用raw-worker根，拒绝重叠根，未枚举系统TEMP。multi-root合成自测8轮均识别子进程、exit0，新增重复根/驱动根拒绝检查通过（`s3-d3-sampler-multiroot-synthetic.txt`）。

| 项目 | 观察结果（字节） |
|---|---:|
| 树合计private peak | 1,497,874,432（约1.40GiB） |
| 树合计working set peak | 1,390,305,280 |
| 主进程private peak | 1,496,662,016 |
| 主进程working set peak | 1,383,292,928 |
| 单RAW worker private peak | 782,401,536 |
| 两个目录合计disk peak | 1,887,448,321（约1.76GiB） |
| 最后一轮disk bytes / files | 0 / 0 |

观察到6个不同RAW worker身份及7个conhost（主程序1个、worker6个），不是13个RAW解码器。主进程峰值与子进程峰值发生时间不一定相同，**不能相加制造树峰值**。稳定阶段主进程约957.7MB private，临时空间计算阶段曾更高。

有1轮`memoryComplete=false`：两个刚退出身份仍在拓扑快照中但内存查询未确认，记录`process-memory-unavailable`，未填零假装完整。其余无拓扑全局错误、无磁盘枚举错误或截断。最终stopReason=`all-known-identities-ended`，remainingKnownAlive为空。此为已观察身份结束，采样间短命进程未验；D2 HANDLE/Job强杀测试仍是独立证据。

采样query平均267.57ms、最大395ms，含CIM/进程属性与磁盘目录枚举，存在明显观察成本。88.44秒包含1500ms附加等待及5秒稳定期，不能与未采样首轮约78秒直接比较，也不能据此声称解码性能变差或变快。

## 证据与清理

忽略目录`build/private-qa/`保留：

- `s3-d3-r7-resource-baseline.txt/.json`：Qt结果及外层退出状态。
- `s3-d3-r7-resource-baseline.resources.json`：原始数值序列。
- `s3-d3-r7-resource-baseline.summary.json`：简要统计。
- `s3-d3-r7-resource-baseline.control.json/.steady.json`：本次PID/缓存根和稳定期标记。
- `s3-d3-r7-sources-manifest.json`：本轮结束后12个源文件SHA/长度，作为后续候选对照基线；不是所有文件在本轮前后各算一次的证明。解码核仍有自身源读前后检查，R7原片保护另有D2专项。

原生窗口结束后12个约4K float缓存已由正常生命周期移除，最后采样目录总量为0。GPT核验本次`d3-resource-temp-*`路径位于private-qa、目录为空后，仅移除这一空目录；没有递归删除。先前被自动审批拒绝的两处40,060,054字节旧缓存未触碰。保留数值报告，不留新图片垃圾。

下一步：流式哈希对照及端到端验证、十张多轮趋势、清空导入与销毁窗口分别检查、D4百轮、D5局部层全源出口。此次生产算法/预算未改，完整回归最新仍D2修正后76/76（417.54秒）；本次仅增加可选资源测试入口，不冒称重新跑过完整套。
