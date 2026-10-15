# S3 A3 存储结果

2026-10-16。范围为 S3 A3 存储基础与失败保护，以下结果由 GPT 实际执行并核对。

## 实施范围

- A3 生产接口为 `src/core/project_store.h` / `.cpp`，接入 `Qt6::Sql` 与 `qsqlite`。对外仅提供同步阻塞原语，供后续后台调用；尚未接入 Window 保存 UI。
- `ProjectState` 保存整组有序照片、标记、每张照片的 global Recipe 与局部记录、activeAssetId 和 revision。冻结使用小 const 快照，防止调用方持有的可变别名改动保存内容；空组允许保存，空文档拒绝。
- SQLite 使用自有签名 `application_id=0x4c534d31`、`user_version=1`；metadata 表 7 个键加 photos 表。读取时严格校验版本、列、Sql 存储类型、JSON 与 size；source 文件不嵌入项目。
- 进程内单一 writer 加 `QLockFile` 跨进程协调。保存路径为：临时 DB（transaction / synchronous FULL / 关闭 / 读回）→ `Persistence::writeAtomicFile`。已存在目标必须提供完全匹配的 `expectedFileHash` 防覆盖；未知项目、不同 projectId、旧版本、源路径目标、符号链接目标均受保护拒绝。
- 源检查为流式 SHA-256（1MiB 分块）；同 hash 重定位保留记录与稳定 id，计数前进。

## GPT 已修复缺陷

worker 草稿中的空指针、数据库 PRAGMA/列校验、SQL 存储类型、UTF8/QString NULL 空元数据、路径别名、复制后 target 指纹、取消错误路径，以及测试 API / const 别名错误均已修复。

## 实际执行的检查

| 检查 | 命令 | 结果 |
| --- | --- | --- |
| 构建 | `tools/dev.ps1 build` | exit 0，日志 `build/private-qa/s3-a3-reviewed-build.txt` |
| A3 专项 | `ctest --preset dev -R '^(project_store\|atomic_file\|photo_document\|document_history\|export_queue)$' --output-on-failure` | exit 0，`s3-a3-specialized.txt`，5/5，2.98 sec |
| project_store 明细 | `build/dev/test-logs/project_store.txt` | 13 passed / 0 failed / 0 skipped，925 ms |
| 原子/队列轮次 | 各 20 轮 | 全通过，共 22.07 sec |
| 100 次替换 | 实际执行 | 101 attempts 全部正确 |

- 完整回归 `ctest --preset dev --output-on-failure`：`s3-a3-full-ctest.txt`，exit 0，39/39，328.28 秒。
- 永久锁、取消与半写失败保护检查通过。
- 完整回归后仅新增一个 Windows 项目级最终替换失败用例，生产代码未变：`s3-a3-final-fault-build.txt` exit0，`s3-a3-final-fault-check.txt` 1/1、1.16秒，最终 `s3-a3-project-store-final.txt` 为14 passed/0 failed/0 skipped、982ms。目标被禁止删除共享的句柄占用时，项目旧字节、输出hash及文件数量均保持；释放后同一保存成功并能读回。没有为新增测试重复全部无关渲染检查。

## 真实源与原生窗口

- `build/private-qa/s3-a3-source/pixel-comparison.json`：R7 JPEG/CR3 的 4K 与完整源四个像素哈希均与 A0 相同；两种格式原图未变。全源进程 exit 0，wall 为 15673/29502ms，包含读取与 PNG 编码，不能作为交互速度。
- 两个 Windows 原生窗口 exit 0，photoCommitted=true、nativePhotoPaints=1；报告 elapsed 为 3409/5346ms。此检查验证原有实际照片显示，不代表项目保存 UI 已接入。
- 12 张原片 SHA-256 全部未变；四张对照 PNG 已在核对后删除，共 89,035,680 字节，保留 JSON、哈希与过程日志。QA 目录约 41MiB。

## 原子记录故障补充

裸 Qt `QSaveFile` 100 次压测中第 82 次 commit 出现 `RenameError 10` 失败，旧文件保持完整。已增加薄封装：仅对 Windows `RenameError`、总计最多 8 次写出尝试、20 ms 启动重试预算，无 sleep、无事件泵、不删除旧目标、无 direct fallback。该封装不构成硬性 OS IO 时延保证，也不保证所有外部占用均能在预算内解除。

## 未完成与不作声明

- 本轮未宣称 45MP RAW、外部 8 GB、真实磁盘满或强杀场景通过。
- D1 恢复副本、项目 UI dirty、窗口保存重开接线，以及 D2 worker 均未实施；B/C 未实施。A3 专项通过不等同 S3 完成。
- 此前完整 38 项运行时，37/38 色差 native 测试曾一次失败且缺详细输出；独立复核 3 passed 并连续 3 轮通过（59.85 sec），但未定位原偶发原因，故不声称已定位。
- CMake 已为每个 test 固定最新 textlog，不再堆积截图。
- 无新增包、ZIP 或 shortcut，无推送。

## 委派审计

存储草稿会话 `ae54aa5a-37ee-4d74-8ee4-60e207e60161` 首次触发 95 次工具调用上限，随后仅补测试；合计 111 个 provider 步骤，输入 62484、输出 56289、缓存读取 7016832。实际检查由 GPT 执行，未采信 worker 的检查声明。发现额外公开源码/测试、构建配置、Qt SDK/DLL 读取，以及自建 scratch 文件操作，超出最小任务范围；未发现私人照片、截图或凭据读取。文档草稿中的 SHA-1 误写已经依据实际 SHA-256 实现修正。
