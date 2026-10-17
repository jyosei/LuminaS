# D3 流式 SHA-256 优化候选（尚未接线）

2026-10-17。当前所有图像/文件校验仍保留。候选只替换散列的实现，不减少哈希次数、不改变 SHA-256、文件格式或像素算法；必须先对照与测量，再决定保留。

## 动机与可核实调用点

`worker_payload.cpp` 写出时逐行 pixelHash + header/json/像素 fileHash，读入时 wholeHash + pixelHash；当前均为 QCryptographicHash。`source_reader.cpp` 和 `worker_client.cpp` 逐块读取原图也用 Qt流式散列。已有 `fastSha256` 只优化完整内存块，未覆盖这些流式调用。多个45MP float帧遍历有可疑成本，但尚无新提速数字，不能把代码盘点当性能归因。

## 接口与失败边界

新增独立 `Sha256Stream`（非全局状态，Win类型仅在cpp，pimpl）提供 addData/result/ok/native，可显式 preferNative=false 作参考。Windows构造时尝试 BCrypt，初始化前失败可回退Qt；一旦开始输入后失败即永久失败，绝不转Qt而丢已喂入前缀。原生路径不能同时喂Qt。result第一次完成后缓存32字节，重复相同；完成后addData拒绝但不破坏结果。空输入标准摘要；move后原对象安全失效。

**已审草稿的实质错误**：忽略目录 `.tools/s3-d3-stream-hash-draft.md` 的说明声称存在 `Impl::~Impl`，实际未定义，外层默认 move赋值会释放旧pimpl却漏清理其原生句柄。接线前必须把BCrypt RAII放在Impl析构，外层析构默认化，确保覆盖已有未完成目标时不泄漏。不能照抄该说明冒充正确。

## 接线约束

- 先实现独立类/测试，保留旧fastSha256行为。测试标准向量、NUL、空块、不规则分块、超过16MiB单块、两后端、move构造/覆盖未完成对象、重复finalize与finalize后写入。Qt参考须独立，不调用新helper生成期待值。
- payload每次addData失败立即拒绝；**fileHash最终digest在无覆盖commit之前取得并验证32字节**，不能提交后才发现原生finalize失败。阶段文件仍由既有RAII回收。
- 读入whole/pixel任一步失败不发布帧；保留长度、SHA、finite/alpha、矩阵/WB、源身份与进程退出门禁。
- 原图helper块读失败/取消或hash失败返回失败，保留前后源SHA核验；不能一次readAll代替流式低内存读取。
- 微小JSON/协议/marker哈希无需更改；先限制为源与payload热路径，不顺带改大量项目持久化代码。
- 不新增环境变量来切换生产后端或绕过worker。缺少原生后端时按初始化回退逻辑可用；记录实际后端，不能把参考路径也标原生。

## 测量与出门

等当前完整回归结束再构建/运行候选。先做缓冲区微基准（同数据、双后端、重复次序轮换），再做真实R7完整源/Prepare4K像素SHA与元数据对照、取消/故障/已存在目标回归。单独采样进程树private/working-set及磁盘；微基准不替代用户首次导入端到端。

如整体没有收益，保留证据后撤回生产接线。原型GPU继续隔离；读取/导出源保护不因速度让步。该候选与D4百轮、D5局部层完整源接口及整个S3完成互不替代。
