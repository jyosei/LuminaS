# S3-D2 导出/RAW 元数据边界工程记录

日期：2026-10-17。GPT 已复核本页所列原生运行及日志；新增后首轮完整回归75/76，修正后完整76/76（417.54秒）通过，不宣称整个 S3 完成。

## 1. 读源边界

- 公共 `readSource`：CR3 固定走 `worker DecodeFull`，不按“是否预览”或“是否小图”切换。
- `prepareSource`：CR3 固定走 `worker Prepare4K`（长边 3840，小源不变）；只含 RAW WB/几何前缀，调色字段由调用方随后恰好应用一次。
- RAW 读取要求后台调用，无父端进程内 LibRaw 降级；缺 worker 或版本不符即清晰失败。
- JPEG 保持原有读取路径；旧 JPEG loader 会拒绝 RAW 输入，不会误接。

## 2. RAW 元数据

- 拍摄参数来自 worker 已打开的 LibRaw header；版权沿用有界 CMT1 解析补充。父端不再二次打开 LibRaw 读取元数据；并不声称全链路只打开一次源文件。
- 使用独立的 typed schema1 codec；RAW payload 具备 `sourceMetadata` 版本能力。
- `SourceMetadata` 与 `AssetIdentity`/`sourceFacts`/Recipe 分离，不作为项目身份字段持久化。
- 导出侧 `PhotoMetadata` 为 `SourceMetadata` 的白名单别名，仅取规范化字符串，不拷贝不透明 IFD 块；`SourceMetadataSnapshot` 携带 `sourcePath`/`sourceHash`/值。

## 3. 导出

- 导出仍从原图重新读取全源；该次已校验 `SourceFrame` 的 path/hash/metadata 快照供元数据筛选使用，父端不另行调用 LibRaw。
- GPS 关闭时：flag 与经纬高全部清零。
- 导出预览取消 token 贯穿读源 / render / 色彩转换；列表替换时施加 `sourceGeneration`/path/recipe/index 门禁，旧 token 结果不提交。
- 真实 R7：收到 worker `rawDevelop 0` 后关闭，native 3/3 通过、1211ms；取消 join 85ms；原图 SHA 不变；外层 exit0、1.6364674s。日志：`build/private-qa/s3-d2-r7-export-close-native.txt/.json`。这是特定阶段的取消实测，不承诺全部阶段都低于此耗时。

## 4. 真实 R7 独立头对照

- 曾经真实失败：make 为空。原因是以原始头对照时 `raw.recycle()` 已先执行，采集时机在 recycle 之后，导致对照失败（保留失败日志 `s3-d2-r7-metadata-native`）。
- GPT 已将采集移至 recycle 之前，保留原有释放时机与图像算法。修正后真实 R7 元数据：native 3 passed / 0 failed / 18404ms；外层 exit0、18.6913516s。日志：`build/private-qa/s3-d2-r7-metadata-fixed-native.txt/.json`。对照 make/model/lens/ISO/shutter/aperture/focal，未弱化独立头断言。

## 5. 构建与回归现状

- 最新专项目标构建 exit0，日志 `build/private-qa/s3-d2-raw-metadata-fix-build.txt`。
- 全部目标重建exit0：`s3-d2-export-boundary-all-build.txt`。首轮完整76项为 **75/76、416.81秒、exit8**：`s3_cancel` 预取消RAW入口返回中文“已取消”，与既有内部`Canceled`契约不符，其余75项通过；S2为82 passed/17可选skip、170139ms。完整记录 `build/private-qa/s3-d2-export-boundary-full-ctest.txt`，失败Qt日志另存 `s3-d2-cancel-contract-failed.txt`。GPT统一公共RAW入口预取消/哈希后/worker返回后的`Canceled`结果，保留断言，并为实际R7原生关闭检查补充同一结果断言；修正后结果见下一条。此前73/73不替代本轮失败。
- 修正后全部目标构建exit0（`s3-d2-cancel-contract-all-build.txt`），取消/metadata/worker/导出相关 **11/11、21.22秒、exit0**（`s3-d2-cancel-contract-focused-ctest.txt`）。真实R7原生显影阶段关闭再次3 passed/1158ms，取消join119ms，明确断言读源结果`Canceled`；外层exit0/1.3478507s，日志`s3-d2-r7-export-close-contract-native.txt/.json`。另一真实R7公共读源100ms后取消3 passed/412ms，响应178ms，外层exit0/0.5455267s，日志`s3-d2-r7-public-cancel-contract-native.txt/.json`；后者不是阶段回执证明。原片SHA均不变。第二轮完整76/76通过、417.54秒、exit0；日志 `build/private-qa/s3-d2-export-boundary-fixed-full-ctest.txt`。
- 真实 R7 JPEG/CR3 + 合成 45MP 全源导出：3 passed、97082ms，外层 exit0、97.57022s。ProPhoto RGB 16位 TIFF，实际成片回读校验，源 SHA 保持不变。JPEG/CR3 均6960×4640、分别24930/40144ms；合成 JPEG 9000×5000、30672ms；9001×5000 超限拒绝。日志 `build/private-qa/s3-d2-r7-full-export-native.txt/.json`。单张时间含全源渲染/编码/校验，不是解码基准。
- 本次不打包、不推送、不生成截图；未改动 STATUS/计划文件。

## 6. 遗留与未关闭项

- D3：资源树 / 磁盘趋势。
- D4：百轮稳定性。
- D5：局部层全源导出接口与最终回归。
- 真实 45MP RAW 缺样本（合成 45MP 只证明边界）。
- 读图速度问题未关闭（首批 12 张仍约 78 秒）。
- 冻结任务的现有文件哈希准备仍不可取消，正常窗口在准备期拒绝关闭；外部强制析构可能等待该后台准备。此既有限制不冒称已由预览取消覆盖。冻结开始后的资产副本按入队时点保留，不因继续修图而改写。

## 7. 执行层审计

早前元数据 codec 草稿由 DSH `83c5d5ca-2942-4ccb-85be-33e7cf6588e7` 提供；执行超时取消后的构建未构成可用终态。GPT 接手实际构建发现测试错误地给 `QJsonDocument::fromJson` 传 bool指针（C2664），改用 `QJsonParseError` 后39项通过。失败/修正记录分别 `s3-d2-metadata-codec-parent-build.txt`、`s3-d2-metadata-codec-fixed-build.txt`、`s3-d2-metadata-codec-parent-ctest.txt`。该会话额外读取CMakeCache/CMake少量工程文本；未发现照片/凭据读取。实际 input14155/output16971/cacheRead327040/total358166。

后台测试/工具调用点的机械迁移由 DSH `a9bf6a35-a3c6-4d05-a6e9-0ad3fb0825d5` 提供，GPT复核实际小范围diff后采用。没有将同步测试包装器用于生产GUI；生产仍在已有Qt线程池内调度。工作者额外枚举测试cpp文件名，但未读取其他内容或私照；input4926/output2791/cacheRead52480/total60197。实际验收由GPT运行，不采信工作者测试声明。

DSH `627e8e25-aa22-48dc-af7c-fee754a2fb24` 只新增本文草稿，GPT 已纠正“源文件不二次打开”等歧义并核对实际日志。6次工具调用除给定文本读取外，额外枚举 `docs/engineering/S3-D2-*` 文件名、查询 `git status -- docs src`；不算完全遵守禁止枚举指令，但未见其他文件内容、图片或凭据读取。日志中的 `private-qa` 仅出现在写入本文的证据路径字符串，未读取 QA 内容。实际 input6058/output2000/cacheRead40448/total48506；非余额依据。直接 DeepSeek 接线评审 input26476/output3415/total29891；未直接采纳其不成立的缓存/token 报警，准备阶段限制如上保留。
