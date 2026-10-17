# 2026-10-17 额度条件暂停续接

后续覆盖：用户已通过宿主恢复，get_goal核实active；原session47057已结束，无需再次等待。三项失败均已定位修正并实际3/3专项通过，详见S3-D1-WINDOW-RESULTS.md首节；本页后文保留暂停时历史状态，不能当作当前阻塞。继续D1源恢复链接、补完整回归及D2–D5。

03:30 定时唤醒已执行：五小时额度已用0%，但get_goal仍返回paused；工具目录只有create/get/update_goal，update_goal没有active/resume入口，不能伪造恢复状态。一次恢复自动任务已停用，额度监测先前已停用。需要通过宿主目标面板恢复目标后接续，未启动新构建或worker。

原有session47057已核实终态exit8：完整66项63通过/3失败，683.57秒。失败project_window（dirty）、chromatic_aberration_window（拖动中间帧仅1，worker1203ms）、s0（closeWhileDecoding在约301907ms处QtTest超时终止）。后两者详细Qt日志另保存到build/private-qa/s3-d1-full-ca-failure.txt、s3-d1-full-s0-failure.txt，重跑前不得覆盖。以下“仍在运行”属于暂停时的历史快照，已被本条终态核实取代。

用户授权剩余不足2%时暂停，上海时间03:30恢复。本次工具返回 primary.usedPercent=100；不使用重置信用。此记录覆盖早先专项通过信息，S3/D1均未完成。

## 当前实际状态

分支 feat/s3-local-editing。最新提交41e32df（后台控制器）；随后窗口/新测试/待导入mark兼容/JPEG缩略图跨项目失效等改动仍未提交，务必保留工作树，不reset或丢弃。所有目标最终重建成功，日志 build/private-qa/s3-d1-window-final-full-build.txt。

**完整CTest仍在运行**：exec session **47057**，日志 build/private-qa/s3-d1-window-full-ctest.txt。最后实际观察为第47/66项s2，在此前23/project_window已有失败；不要因观察超时重新启动。恢复后先write_stdin轮询47057；若句柄不存在，再核查日志与进程的实际终态。测试可在目标暂停期间自行完成，不启动新的开发任务。

## 必须先处理的失败

project_window::unselectedIdentityAndActualRoundTrip 在完整回归中重开有非空笔刷层的项目后 dirty=true；断言行62。单独此前原生6项全通过，但不代表这个交错缺陷已修复。保留完整失败Qt日志 build/private-qa/s3-d1-window-full-first-failure.txt。

刚在 tests/project_window_tests.cpp 的该断言前加入保存/载入后项目revision、active、mark与实际documentJson差异诊断，**这项诊断尚未重新编译**。完整回归结束后先重建project_window_tests，再运行此槽或专项，获取实际数据差异，定位是源事实精化、文档指针观察还是其他修改。不要猜测或放宽dirty断言。native等待失败日志s3-d1-window-rich-native.txt另属测试错误使用neutralPickerAvailable作为局部覆盖完成信号，已经改为实际EditResult配方/层匹配，不是本次dirty故障。

## 新增源码和验证

- src/app/window_project.cpp：存储准备、200ms提交文档观察、打开/保存/恢复菜单/快捷键、显式/恢复状态、关闭保护、缺源门禁。
- 未选照片prefetch绑定完整身份；bindAsset相同事实保持指针；EditLoader.resetImports取消旧预解析/清导入显示缓存；JpegLoader.invalidate丢弃旧缩略图。后者最后全量构建已包含。
- Recovery PendingImport新增可选mark，旧文件缺省Unmarked；首轮hasOnlyKeys要求全字段导致兼容失败，已修并经4/4专项通过。未知类型/范围仍拒绝。
- load后台准备loadedHistory，窗口只采用已校验数据。
- 存储准备中/不可用仍关闭保护；完整回归中 unavailableStorageStillProtectsClose 已通过。
- 新tests/project_window_tests.cpp；新tests/project_recovery_process_tests.cpp。后者由DSH新会话97e1423d-63b8-4db2-8fc0-b5b0863110e2起草，GPT复核并实际构建/运行；会话已完成，无活跃worker。审计 .tools/audit-97e1423d-63b8-4db2-8fc0-b5b0863110e2.json，14个shell调用，无图像/凭据读取，额外tests目录存在性查询已登记。
- 先前专项4/4，16.29s；原生窗口6 passed 9.559s；原生已提交副本真实子进程强杀恢复3 passed 3.457s。最后完整回归的进程测试亦已通过，child槽正常skip（由父槽单独启动）。全部测试结果以这次完整回归失败优先，不宣称66全通过。

## 后续顺序

完成现有完整回归观察→诊断并修复dirty→重建相关测试/生产程序并验证→按影响补完整回归→更新STATUS/结果并提交当前窗口批次。尚缺安全源恢复链接入口、恢复发现/磁盘策略及更多故障验证；D2 RAW进程、D3资源、D4百轮、D5完整验收继续，不能把D1数据基础当S3完成。

完成S3后冻结开发、按用户授权上传GitHub并准备换设备迁移包，再停下；当前不生成包/ZIP/快捷方式、不推送。生产CPU路径保留，GPU正式接入等新设备。最新已有结果与限制见S3-D1-WINDOW-RESULTS.md；它的专项记录不能覆盖此处的新失败。
