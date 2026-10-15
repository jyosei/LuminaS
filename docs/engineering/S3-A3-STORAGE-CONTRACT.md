# S3 A3 存储接口与保护决策

2026-10-15，GPT 架构合同。已实现并通过 A3 专项检查；结果见 S3-A3-RESULTS.md。

## 状态与格式

- `ProjectState` 保存稳定 projectId、名称、revision、有序照片列表、activeAssetId；每项为 A2 的不可变 PhotoDocument 小快照及 Unmarked/Keep/Reject 标记。项目引用原图，不包含图像或蒙版栅格缓存，不持久化 Undo 时间线。
- 冻结时深复制顶层文档值，Qt 值容器可以隐式共享；调用方随后持有的可变别名不能改动保存内容。项目最多 2000 张照片；所有文档以持久化模式校验（hash/方向归一源尺寸完整）。asset id 与源路径在组内唯一，activeAssetId 为空或属于组内。
- 空组允许保存；若任何照片项携带空文档则拒绝。调用方在自身拥有的稳定编辑状态上冻结后，再把快照交给后台，不与其它线程并发修改正在冻结的可变树。
- API 见 `src/core/project_store.h`。源路径使用绝对路径；Windows 规范路径按不区分大小写判重。项目文件上限 256MiB、单文档 JSON 上限 64MiB，限制在读取大 BLOB 前检查。名称上限 4096、projectId 为 1–128 字符、revision 非零。
- SQLite `application_id` 为 LuminaS 自有固定签名，`user_version=1`；独立 metadata 表记录 format/formatVersion/algorithmVersion/projectId/name/revision/activeAssetId。revision 使用十进制字符串。photos 表按 ordinal 连续保存 assetId、mark 和规范 PhotoDocument JSON BLOB。
- 项目格式与 A2 文档算法版本分别校验；未知格式、额外表/节点、损坏 JSON、重复身份/序号、超容量或不完整源身份一律拒绝，不默默丢节点。旧全局 Recipe 的显式导入沿用 A2 接口，不把任意 JSON 当作 SQLite 项目。
- SQLite 读取只读，关闭连接后移除连接注册；连接不能泄漏到下一次原子替换。解析先构造完整临时状态，验证全部记录后才替换调用方输出。缺源不阻止读取编辑数据。

## 保存与源保护

- 持久化模块只提供同步数据基础，调用线程应为后台；后续窗口接线必须异步，GUI 不等待存盘或源哈希检查。
- 进程内最多一个保存 writer；同一路径使用 QLockFile 协调跨进程写入。忙时明确失败，不能并发改同一个 SQLite 文件。
- 冻结快照 → 同目标目录唯一临时 SQLite → transaction + synchronous FULL + 提交 → 关闭数据库 → 检查一致性/读回 → QSaveFile 原子提交目标。禁止 direct-write fallback；失败及取消仅删除本次拥有的临时目标，旧项目不受损。
- 已存在目标必须是本模块可完整读回的同一 projectId 项目；未知/损坏/其他项目不覆盖。保存不得写入任何项目照片的源路径。符号链接目标、目录、空路径与无效路径明确拒绝。
- `loadProject` 返回实际文件 SHA-256；覆盖保存必须传递完全匹配的 `expectedFileHash`，保存成功返回新 SHA-256。无 expected hash 仅能新建，不能覆盖已有文件。旧 revision 不覆盖新版本，同 revision 内容不一致同样拒绝。返回 hash/状态仅成功时修改。
- 保存开始与最终提交前对已存在目标采集字节指纹；发现外部变更拒绝提交。此检查和合作式 lock 提供应用层冲突保护，不宣称能防御未配合锁的外部程序在最后一个文件系统操作间隙恶意竞写。
- 取消在建库/逐项写入/复制提交前轮询，未提交时清理临时目标；成功提交是不可取消的完成边界。
- 原始源检查分开提供逐文件 SHA-256 流式只读检查，区分可用、缺失、内容变化、不可读、取消；不调用 RAW 解码或分配整张图。重定位只接受 hash 完全一致的新路径，保留已有记录与稳定 id，不擅自接受变化源。
- 重定位产生新的项目/文档 revision，localIdentity 不变，计数溢出明确失败；同一原路径无变化可返回原快照。读回同一组后通过 A2 resetDocuments 建立新历史会话。

## 检查边界

覆盖整组标记/顺序/所有全局字段/局部组成项往返、缺源仍可打开、同 hash 重定位及变化 hash 拒绝、未知格式/损坏失败不改输出、既有目标和原图不被覆盖、中文路径、取消清理、同路径 writer 排斥。数据基础不扩展复杂项目界面；显式保存/恢复副本与 dirty 版本比较在 D 接线时完成。

需要在 Windows 检查目标被占用时的提交失败保护，不能用删除旧目标后重命名绕过原子要求。断电/真实磁盘满与其他设备仍须区分逻辑故障注入和实际系统证据。
