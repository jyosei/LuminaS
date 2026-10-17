# D5 局部文档导出桥接：专项结果

2026-10-17终态：桥接专项、窗口整合、原生CR3成片检查及修正后的完整81/81（428.33秒）、exit0通过；D4百轮也已通过本机验收，S3功能冻结。最终依据见[S3-ACCEPTANCE.md](S3-ACCEPTANCE.md)，下文保留过程。只继续用户授权上传/开发迁移包，不做快捷方式或新功能。

## 实现

- Asset/Job增加可空PhotoDocument快照；freeze复制并验证source/hash/global/revision/raw绑定，Pending/Skipped均保留文档。导出Job版本2，Settings/预设/项目/队列外层版本不变。v1旧任务继续支持，拒绝携带非null局部文档的v1，避免静默丢层。
- Queue存入已验证解码的副本；保存失败仍回滚，16MiB/2000任务上限不放宽，重启未完成任务仍需显式重试。
- Window向导出模块传当前/历史局部文档及每文档revision。完整导出从原始JPEG/CR3重新读取全源，非空文档调用既有renderDocumentFullLinear；不使用4K缓存或设备RGB成片。
- 转换预览调用既有renderDocumentPreviewLinear，缓存和旧结果准入包含局部文档，目标ICC路径不变。

## 构建与专项

`s3-d5-bridge-focused-build.txt`与`s3-d5-corrected-and-heap-build.txt`实际构建exit0。后者同时包含独立D4测试诊断，不代表内存问题修复。

`s3-d5-bridge-corrected-ctest.txt`：类型、局部全源、预览生命周期3/3，3.16秒，exit0。

- 类型：v1/v2/null/局部往返、错身份/参数/版本/RAW标记/尺寸/未知算法拒绝，失败不改变输出；复制隔离、队列副本、不可序列化拒绝、Skipped仍带文档。
- 小图全源：16位ProPhoto TIFF由LibTIFF独立解码，验证RGB/16位/contig/方向/unsigned/ICC/尺寸及每行字节。移除局部文档后对照确实不同，防止空场假通过。
- 持久化：先存pending，后改调用方文档，销毁并重载队列；直接start拒绝，显式retry后输出仍等于旧快照。两个源副本批量与单张像素一致。
- 预览：相同源/hash/global，只改局部曝光，实际canonical图像变化、重新读取；内容完全等值的文档拷贝复用线性缓存，读取次数不增加。

## 真实R7与合成45MP

`build/private-qa/s3-d5-r7-local-export.txt/.json`：原生Windows Qt，3 passed/132097ms，外层exit0/132.3055811秒。串行参考→释放浮点源→实际导出；参考与实际读取分别执行，保留的参考仅16位编码像素。每个原图前后SHA相同。

三组均使用全局曝光/对比、旋转3度/翻转/裁剪、非空笔刷与曝光、梯度、径向减区、opacity/反相/羽化及禁用层；RAW匹配源WB。输出ProPhoto RGB 16位ZIP TIFF，ICC精确相同，独立解码每行字节相同：

| 输入 | 规范化源尺寸 | 裁剪后成片 | RGB16像素SHA256 |
|---|---:|---:|---|
| R7 JPEG | 4640×6960 | 3712×5568 | `d017479abb0546931daa1eaf76feb6acccb88992fa5892153453f803efe3f989` |
| R7 CR3 | 4640×6960 | 3712×5568 | `18d07f1fcdec26408cf75167fb50bc8710a5dbe7c17bc6b36c7dc60ca2944d07` |
| 合成JPEG | 9000×5000 | 7200×4000 | `99a994a70fb1f03a3067a1a2607e3b9e1694d9438a5e1db915b99ceffc943a23` |

这验证桥接与现有全源核心/色彩转换一致；独立核心数学参考见B/C数值验收，不把相同核心的桥接对照称为独立算法推导。真实45MP RAW没有样本，未验证。全部临时JPEG/TIFF由QTemporaryDir正常回收，仅保留数字日志，无截图或成片堆积。

## 失败与工作者审查

- 类型首测发现freeze复制文档后未赋给最终Job，GPT补Pending/Skipped赋值并增加Skipped断言。首测`export_document_types`失败保存在`s3-d5-document-types-first-ctest.txt`。
- 成片首测错误期待恢复队列可直接start，实际显式重试契约要求拒绝；修正夹具后通过，原失败`s3-d5-local-first-failure.txt`/`s3-d5-bridge-first-ctest.txt`保留。未为通过测试改变恢复产品行为。
- DSH模型任务413bde64的补丁破坏三个文件开头及namespace布局，GPT保留独立原备份/损坏稿/差异后修复；另修非法测试参数、错断言和复制隔离检查。工作者没有执行过本报告中的检查，其“未损坏”声明不被采信。
- DSH数值夹具ca3ea67c在工具上限停止，只留下include骨架；实际工具日志包含额外读取核心/队列/渲染文本，未发现图片/凭据读取。无自动重试或续跑。GPT重新实现数值夹具；直接API的TIFF小助手草稿因length截断未采用。
- 预览/窗口测试草稿由直接API提供，GPT修正revision、数据模型、饱和测试像素、未存在接口等后整合。没有把草稿的假设当成运行证据。
- 首次新增后的完整回归中，新增Window局部绑定用例通过，但污染了共享导出设置/队列目录，导致后续两个旧UI用例失败：ProPhoto已是16位未触发自动切换提示、旧队列非空使新入队等待提前结束。失败保留`s3-d5-ui-first-full-failure.txt`；新slot改为独立LUMINA_EXPORT_DATA_DIR并在Window析构后恢复，尚待复测。不修改产品提示/冻结行为来适应夹具污染。

## 窗口整合与原生结果

- 全部目标实际构建exit0：`s3-d5-all-build.txt`。首次完整81项80通过、1个export_ui失败，443.15秒、exit8，`s3-d5-first-full-ctest.txt`；UI包含上面两个夹具污染失败，其余80项通过。完整失败不能被窄复测覆盖。
- 隔离修正构建exit0：`s3-d5-ui-isolation-build.txt`；专项export_ui 1/1、4.44秒、exit0，`s3-d5-ui-isolation-ctest.txt`。新增窗口路径实际验证当前doc/global/hash/path/normalizedSize/每doc revision、非空笔刷、入队后继续调参隔离、切到另一张后的history文档及null旧路径。
- 原生Windows Qt真实R7 CR3：整个export_ui 8 passed、无skip/失败，49552ms；外层exit0/49.8042271秒，`s3-d5-native-ui-cr3.*`。包含真实CR3全源导出及OutputInspector实际读取TIFF、真实100%物理像素缩放；合成局部窗口同样检查实际输出。没有设置截图环境变量，不留照片渲染证据图。
- 生产LuminaS.exe `--smoke-test --smoke-report ...`：exit0，`s3-d5-production-smoke.*`及`.receipt.json`记录实际Qt/windows平台与启动退出。这是空生产窗口检查，不冒称生产实照加载速度。

## 待完成

修正后的完整回归已经81/81、428.33秒、exit0，`s3-d5-corrected-full-ctest.txt`；D4见最终百轮报告。剩余仅上传与开发迁移交接，不扩大功能。
