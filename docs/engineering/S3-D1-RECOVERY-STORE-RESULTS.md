# D1 独立恢复副本数据基础

2026-10-16。实现S3-D1-RECOVERY-CONTRACT.md的第一项；后台控制器、两秒去抖及窗口打开/保存/恢复**尚未接入**，D1未完成。

## 变更

新增 `recovery_store.h/.cpp` 和 `recovery_serialization.cpp`，提供独立RecoveryState/RecoverySnapshot、严格内部JSON往返、冻结与原子存盘。已确认照片复用完整ProjectState和PhotoDocument；待解析/失败导入以路径与错误保留，整组顺序、活动项及最近显式保存位置/hash独立记录。公开SQLite项目格式不变，不包含像素、预览或Undo时间线。

校验2000项总量、已确认/待导入互斥与完整顺序、活动项对应、源身份、版本/字段/类型、规范uint64 revision、64MiB单文档和256MiB恢复文件限制。快照调用既有freezeProject消除可变别名；错误解析不改变调用者输出。超大浮点标记/版本在任何整数转换前拒绝。

I/O使用单writer与QLockFile、同目录QSaveFile原子提交（沿用既有有限重试、禁用直接写入回退）。目标及锁文件不能是已确认/待导入源或显式项目的别名。已有恢复文件要求准确SHA、相同项目身份、不降低revision；同revision内容变化拒绝，内容完全相同可幂等返回。写入前及每次提交前重查指纹；加载也在解析前后核对实际文件SHA。失败/取消保留旧文件及输出hash，不创建缺失目录。与A3一样，不宣称能防御不合作程序在最后OS操作间隙的恶意竞写。

## 实际构建与测试

- 首次 `./.tools/s3-build-targets.ps1 -Targets recovery_store_tests`：exit0，`build/private-qa/s3-d1-recovery-build.txt`；专项12 passed，1294ms，`s3-d1-recovery-ctest.txt`。
- 补加载解析后文件指纹检查，执行 `./.tools/s3-build-targets.ps1 -Targets recovery_store_tests,project_store_tests,atomic_file_tests,lumina`：exit0，`s3-d1-recovery-final-build.txt`，包含生产目标重新链接。
- `ctest --preset dev -R '^(recovery_store|project_store|atomic_file)$' --output-on-failure`：**3/3，3.26秒，exit0**，`s3-d1-recovery-final-ctest.txt`。Qt恢复12 passed（1277ms）、项目14 passed（981ms）、原子写9 passed（482ms），均含init/cleanup。
- 最后将进程内writer保护提前到冻结/JSON分配之前，避免并发写入者先同时分配大恢复包。相同四目标重建exit0（`s3-d1-recovery-serial-build.txt`），上述3项复查**3/3，3.23秒，exit0**（`s3-d1-recovery-serial-ctest.txt`）；恢复12/项目14/原子写9 passed，分别1263/972/495ms。没有继续重复已通过检查。
- 新增恢复专项使CTest注册总数增至63项。**没有新的完整63项结果**；最近全量仍是此前裁切约束批次62/62（374.63秒），不复用旧数字声称新增目标已全量验收。

专项使用QTemporaryDir里的微小文本源，不解码或访问真实照片。涵盖三种局部蒙版/颜色参数/三种标记/交错待导入顺序/中文路径完整往返，缺源仍可加载，空组与待导入组，深层可变别名隔离；严格JSON、超大数值/未知字段/版本/不完整身份/重复与乱序/错误活动项；实际文件SHA、新目标/未知文件/错误期望hash/其他项目/旧revision/同revision不同内容的失败保旧；源与显式项目及锁伴随路径保护；锁冲突、预先取消、缺父目录、逻辑大小256MiB+1文件拒绝读取。Windows实际持有禁止delete sharing的句柄，证明更新提交失败且旧恢复字节/hash不变。

测试临时目录结束后为空，QA无新增图像；没有截图、照片渲染输出、打包或推送。没有凭本批数据单测宣称真实45MP RAW、8GB设备、UI响应或进程树资源验收通过。

## 委派审查与待验事项

DSH会话 `c314d9da-ab26-47cd-bcc3-aa78aa68b00e` 在读取阶段以max_tokens停止，没有写文件或运行构建，不自动续跑。审计实际28次调用，发现额外源码/构建文本读取及glob/目录查询越过任务范围；没有图片读取或密钥查询调用。provider记录input41882、cacheRead568576、output10416，总620874 tokens，不据此推算人民币余额。

随后两次直接DeepSeek文本草稿均正常结束（序列化输入2162/输出5186、测试3461/5346）。GPT补齐总尺寸校验，修正越界数值转整数；重写草稿中无效基线（hash过短、活动项不一致）、错误锁后缀、自比较断言、把幂等操作当成实际提交失败等。最终I/O与测试由GPT集成并实际执行，不接受worker测试声明。

下一项是两秒去抖、单任务/最新合并、dirty与epoch门禁的后台控制器，再接窗口与未选照片源身份。强杀恢复、写入中取消、真实磁盘满/断电/权限不足、实际符号链接场景仍待D1整体验证；历史atomic_file偶发与色差反馈未定位不因本次3/3被抹去。D2 RAW隔离、D3资源、D4百轮释放及D5最终验收均未完成。S3完成后按最新指令冻结并准备设备迁移，暂停后续功能开发。
