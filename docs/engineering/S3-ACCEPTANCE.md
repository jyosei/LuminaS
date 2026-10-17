# S3 本机验收与功能冻结

2026-10-17，GPT终审核对实现与日志后，S3通过本机验收。进入功能冻结；只继续用户已授权的GitHub上传和开发迁移包，然后停止等换设备，不开展S4或生产GPU。

## 最终实际检查

- 全部目标构建exit0：`build/private-qa/s3-d5-all-build.txt`。UI夹具隔离修正重建exit0：`s3-d5-ui-isolation-build.txt`。
- `ctest --preset dev --output-on-failure`：**81/81、428.33秒、exit0**，`s3-d5-corrected-full-ctest.txt`。可选私照/性能slot默认skip，实际样片证据另列，不将skip当实照通过。
- 首轮完整80/81、443.15秒、exit8：新增夹具污染共享设置/队列，影响两个旧UI子用例。隔离后专项1/1、原生整套8 passed及完整全套通过；原失败保留，产品冻结/恢复语义未为夹具改变。
- R7 JPEG/CR3及合成45MP局部全源ProPhoto RGB16 TIFF：独立解码标签、ICC、尺寸和逐行像素对照通过，3 passed/132097ms，原片SHA不变。真实CR3原生导出及100%检查8 passed/49552ms；生产启动退出exit0/3408ms。
- 百轮释放90合成/10 R7 RAW，3 passed/1022138ms；源SHA、weak、窗口/控件释放通过。预热后活跃分配平台，末两组private均值50.65/50.51MB；归因清缓存后allocated3.908MB，首轮3.932MB。归因操作未加入生产代码，正常末尾缓存disk0。

## 要求与证据

| 切片 | 实现与验收依据 |
|---|---|
| A | 文档、命令、旧Recipe兼容、存储往返：[A3](S3-A3-RESULTS.md) |
| B | 源坐标笔刷、补减、一笔历史、几何贴合、4K/全源和计时分离：[B6/B7](S3-B6-B7-RESULTS.md) |
| C1/C2 | 九项参数与层管理：[C1](S3-C1-RESULTS.md)、[C2](S3-C2-RESULTS.md) |
| C3/C4 | 线性/径向渐变、控制点、有序补减、羽化/反相及蒙版查看：[C3](S3-C3-NATIVE-RESULTS.md)、[C4](S3-C4-UI-RESULTS.md) |
| C5/C6 | 精确缓存依赖、全局组合及显式去空白边缘：[缓存](S3-C5-DEPENDENCIES-RESULTS.md)、[组合](S3-C6-NATIVE-RESULTS.md)、[裁切](S3-CROP-CONSTRAINT-RESULTS.md) |
| D1 | 照片组保存、恢复副本、哈希重链接：[D1](S3-D1-RELINK-RESULTS.md) |
| D2 | 生产RAW worker、单RAW在途、取消/故障/进程树与元数据边界：[D2](S3-D2-EXPORT-BOUNDARY-RESULTS.md)及其可靠性报告 |
| D3 | 父子进程RAM/磁盘、多图多轮和保留全部校验的流式SHA：[优化](S3-D3-STREAM-HASH-WIRING-RESULTS.md)、[多轮](S3-D3-R7-RESOURCE-CYCLES.md) |
| D4 | 百轮及预热后资源平台：[D4](S3-D4-RELEASE-STRESS-RESULTS.md) |
| D5 | 文档冻结/队列往返、局部全源出口、预览失效、全套与原生检查：[D5](S3-D5-EXPORT-BRIDGE-RESULTS.md) |

## 保留限制

- 真实45MP RAW缺样本，8GB设备未认证。合成边界、单机采样及有限百轮不能替代这些检查或无限时长认证。
- 首次12张R7预解析单机对照约78.045→52.870秒，仍有优化空间；复杂空间算子不保证即时反馈。30/60fps、33/100ms仅额外目标，不是本轮硬门槛。
- 冷物理磁盘、真实磁盘满/断电/在途项目提交强杀、实物印刷和跨机安装发行未认证。已有注入故障和RAW父进程强杀证据不泛化为所有物理场景。
- 历史atomic_file替换拒绝访问与色差反馈偶发失败不宣称全部根因已定位；有限重试与后续通过按原报告保留。
- 去空白边缘是显式保守收紧，不是默认自动裁切或最大面积优化；GPU仅实验，生产CPU。
- 被自动审批拒绝的历史残留清理仍保留，不绕过。正常临时成片/缓存已回收，只保留数字证据，不新增截图。

新机入口：[迁移交接](../../handoff/S3-MIGRATION.md)。旧报告的未完成状态为当时记录，本页与STATUS最新条目为当前结论，历史失败不删改。
