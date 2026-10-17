# D3 流式哈希接线与真实样片对照

2026-10-17。当前候选热路径已接线，数值/专项/样片通过，**完整77/77通过（417.66秒），D3/D4/D5及S3仍未完成**。不推送、不打包。

## 实现与实际检查

只更换`source_reader.cpp::hashFile`、`worker_client.cpp::sourceHash`及`worker_payload.cpp`的pixel/whole/file散列流。每次addData失败终止，最终摘要必须32字节；写帧的file摘要在no-replace commit之前取得。源前后SHA、请求身份、长度/finite/矩阵/WB/严格字段校验、取消和Job保护不减少；像素算法、文件格式、Qt引用路径、GPU与缓存预算未改。

GPT实际专项目标构建exit0（`s3-d3-stream-wiring-build.txt`），相关10/10、16.03秒、exit0（`s3-d3-stream-wiring-focused-ctest.txt`），包含stream、transport/process/client/fault/session/loader、metadata flow、s3_cancel和export_preview_lifecycle。全部目标构建exit0（`s3-d3-stream-all-build.txt`）；完整77/77通过、417.66秒、exit0（`s3-d3-stream-full-ctest.txt`）。此前D2的76/76（417.54秒）单独保留。

真实R7 CR3父端/worker对照3 passed/25824ms，外层exit0/25.9011749s（`s3-d3-stream-r7-equivalence-native.txt/.json`）：

- 原源SHA `1cc9a37d72cc02c137b0db12e17e7ea3d4176ed30058f9342a9fed2a663d2856`。
- DecodeFull 6960×4640，独立Qt像素摘要 `d5c371e5e69bed6d48e8669893d76a0ef6eb2bf46796b2f946258b9fd08e4a36`。
- crop/angle/WB Prepare 3840×2560，摘要 `bfdaefaa94255edb871c8f02d76cb2a079661f30ae44d0850eae02d38bdbf6c9`。
- 输出矩阵/相机WB与原核一致。该整体时间含参考核与独立摘要，不是单张读图。

真实生产loader3 passed/30903ms，外层exit0/31.0227028s（`s3-d3-stream-r7-production-native.txt/.json`）。包含WB/裁剪旋转/翻转/非空局部笔刷，普通/完整几何两分支的显示SHA分别为`18aabf17b7c326944445317b545d5d03b565f68d31b1aca22184700ca771670d`、`9a8d322521fc8951c17d57b854ce203ade7b9db382a489cec7fe2199273d90f4`，与D2完全相同；完整浮点源SHA也匹配原核。此次后端回执5781/7901ms，D2同一用例9567/20168ms，仅为本机两次运行对照。正常照片视图mask为空，空maskSHA不是非空覆盖层证明。

## 12张端到端与资源

相同6对R7、相同采样器/显式1500ms附加等待及5秒稳定期：

| 指标 | 优化前 | 当前候选 |
|---|---:|---:|
| 资源用例外层耗时 | 88.4377s | 64.5222s |
| Qt结果 | 3 passed/87591ms | 3 passed/63719ms |
| tree private采样峰值 | 1,497,874,432B | 1,458,843,648B |
| tree working set采样峰值 | 1,390,305,280B | 1,394,806,784B |
| disk采样峰值 | 1,887,448,321B | 1,887,448,305B |
| 最后一轮disk bytes | 0 | 0 |

本次整体约减少27%。内存/磁盘未见明显增加，不把小幅差异称节省内存。候选125轮、500ms间隔，1轮memoryComplete=false；stop=`all-known-identities-ended`、remaining空，磁盘无截断/错误。query平均282.70ms，有观察成本。原始记录`build/private-qa/s3-d3-r7-resource-stream.*`，基线见[S3-D3-R7-RESOURCE-BASELINE.md](S3-D3-R7-RESOURCE-BASELINE.md)。结束后GPT逐个验证12源长度/SHA与基线manifest一致，只删除本次private-qa下已核实为空的`d3-resource-temp-*`目录，未触碰旧40MB拒绝删除目录。

另单独关闭采样器运行`realImportPreparesAllBeforeClicks`：3 passed/60038ms，外层exit0/60.3813518s；**预解析52870ms**，此前同机78045ms，本次约减少32%。2秒拖动22个渲染回执，不当FPS。12次实际切图均canonical key核验、sourceRebuilt=false；首绘41–60ms，中位57ms；此前那轮54–690ms/中位179.5ms，仅两次样本，不保证通用点击延迟。日志`s3-d3-stream-r7-import-native.txt/.json`、`s3-d3-stream-r7-import-numbers.json`。

样本/文件系统缓存/系统负载都会影响时间；上述不是物理冷盘统计，也不能套用64MiB微基准的11.7倍。首次12张仍约53秒，读取速度问题未关闭；后续多轮趋势、100次释放、局部层全源导出桥接和最终全套/原生验收继续。真实45MP RAW、8GB机器未验。

## 协作与审查

DSH `39b12a60-1a54-40fe-8499-07d38e9fd64d` 完成3文件机械接线，9次工具调用实际均为给定5份文本读取/限定编辑及这3文件diff/check，没有构建/图片/额外目录探测。GPT检查23增16删实际diff和完整hash段后自行构建执行；不采信其“所有hash已替换”的泛化说法，微小ICC/协议hash未改。审计`.tools/audit-39b12a60.json`，input20412/output3787/cacheRead143616/total167815，不是余额。GPT备份仅3份源文本，留在忽略目录`.tools/s3-d3-prestream-backup`。

后续资源循环草稿单独存`.tools/s3-d3-resource-cycles-draft.md`，未集成、未构建：已有`core/recipe.h`虚构头、QPointF当horizontal/vertical字段、shared_ptr使用点访问等错误，不能据草稿称多轮已验证。直接DeepSeek input3644/output3562/total7206。GPT后续修订并独立验收，默认不启用私照性能用例。
