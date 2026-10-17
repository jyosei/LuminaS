# D3 流式哈希基础实测（未接生产调用点）

2026-10-17。新增独立`Sha256Stream`，Win初始化阶段可回退Qt，开始输入后的失败永久失效；支持缓存finalize及安全move，不同时喂两个后端。GPT复核DeepSeek草稿，修正其实际缺失`Impl`析构导致覆盖未完成目标时原生句柄泄漏的问题；原生句柄现由pimpl自身清理。SHA-256语义未变。尚未替换source/payload/client生产调用，不是照片提速结果。

GPT实际构建新专项目标exit0：`build/private-qa/s3-d3-stream-foundation-build.txt`。CTest1/1、0.59秒、exit0，Qt13 passed/1个显式性能项skip，覆盖空输入、NUL、不规则含空分块、标准abc向量、超过16MiB单块、重复finalize、结束后拒绝写入、move构造和覆盖已喂入状态、两后端与独立Qt参考。日志`s3-d3-stream-foundation-ctest.txt`、`build/dev/test-logs/sha256_stream.txt`。未人为注入BCrypt初始化/中途失败，失败分支只完成静态检查，不谎称故障实测。

显式开启`throughputOptIn`，相同64MiB合成字节按1MiB分块，四轮交替后端次序；每次结果对照独立Qt摘要。填充数据与期待值计算不计入被测段，构造/分块/finalize计时。

| 轮 | Qt ms | BCrypt ms |
|---|---:|---:|
| 0 | 405.0031 | 34.6120 |
| 1 | 403.3163 | 33.9794 |
| 2 | 401.8405 | 35.2058 |
| 3 | 406.3873 | 33.9684 |

每个原生请求均实际`usedNative=true`；平均404.1368/34.4414ms，**仅此内存散列段约11.7倍**。Qt3 passed/2414ms，外层exit0/2.4744348s。日志`build/private-qa/s3-d3-stream-memory-benchmark.txt/.json`。没有私照上传或图像输出，不能将该倍数套到RAW解码、磁盘或首次导入。

当前完整回归最新仍D2修正后76/76、417.54秒。新增注册后共77项，未完整重验；基础独立测试不是全套通过。下一步按[S3-D3-STREAM-HASH-CONTRACT.md](S3-D3-STREAM-HASH-CONTRACT.md)接线热路径，finalize必须先于文件commit，然后做真实R7数值/取消/故障/全源/端到端与资源对照。不减少校验，不改像素数学，不扩大S3范围。

该基础草稿直接DeepSeek输入1965/output4511/total6476，GPT已审实际代码并自行构建测试；不使用草稿中虚构的`~Impl`存在说明作为验收。记录不是余额。
