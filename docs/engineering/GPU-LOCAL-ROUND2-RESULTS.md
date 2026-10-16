# GPU第二轮：常驻4K局部处理

2026-10-16。按用户新增方向推进CPU/GPU混合硬件实验，结论仍为继续研究；生产后端保持CPU，实验默认构建选项仍OFF。不是端到端产品加速验收。

## 路径与数值合同

新增`gpu_probe --local`、`local.comp`及`local.inc`。CPU准备覆盖场，上传一次RGBA32F输入与R32F蒙版；GPU驻留两张RGBA32F图像、一张R32F覆盖与4097项float增益表。每次参数变化仅更新48字节uniform及约16KiB表，不上传整幅帧。设计中的输入位置是**全局前缀完成后、局部层之前**；本实验用合成线性工作帧模拟该输入，没有迁移全局曲线/HSL或实际RAW解码。

九项局部参数使用与现有LocalPixelTransform相同的RGB白平衡、曝光、.18对比度支点、四区域色调增益、饱和度及按蒙版/层强度混合。源alpha原样保留，透明或零权重保持原像素，负值/HDR不提前截断。GPU混合使用float，CPU现有混合使用double中间值；不要求浮点SHA相同，沿用首轮RGB误差≤1e-5+1e-4×|参考|、alpha≤1e-6，不放宽容差。

5组正确性：恒等、九项混合、负曝光/去色、层强度0、亮部/阴影/黑白极端；每组完整读回比对串行CPU。另验证四工作线程CPU与串行结果QImage精确相等。性能测量两边各预热5次，再交替顺序100次；CPU包含深复制和四线程局部计算，GPU包含参数/增益表更新、提交、同步等待，不包含原生呈现、ICC、蒙版变更或全局前缀。CPU并非整个生产renderDocumentPreview基线，不能从此计算窗口提速倍数。

## 实际命令与结果

- 构建`./.tools/s3-build-targets.ps1 -Targets gpu_probe,local_combinations_tests` exit0，`build/private-qa/gpu-local-round2-build.txt`。
- `gpu_probe.exe --local --size small --iterations 100 --report <新JSON>`，257×131，exit0；`gpu-local-round2-small.json`。
- 同命令`--size 4k`，3840×2560，exit0，23.81秒（包括准备、验证及两边测量）；`gpu-local-round2-4k.json`。
- 设备实际为AMD Radeon(TM) Graphics、D3D11 feature level 0xb000；只对本机取证，没有NVIDIA或低内存跨设备认证。

小图及4K均5/5正确性通过、失败通道0、alpha最大差0。最大RGB绝对差3.0517578125e-5发生在极端HDR，最大误差/规定容差约0.00675；混合参数最大差9.5367431640625e-7。恒等与零层强度RGB精确一致。4K基础帧/蒙版上传计数均1。

| 4K隔离阶段 | median ms | p95 ms |
|---|---:|---:|
| CPU深复制+四线程局部变换 | 150.7588 | 166.8436 |
| GPU常驻单层局部计算（含参数/LUT与同步） | 22.6557 | 23.4244 |

第一轮上传+读回正确性调度332.78ms，后续验证读回约78.73–137.71ms，不是纯DMA计时。这些开销没有混入常驻计算，也没有隐藏为首次即可22ms。常驻优势不能用于宣称照片导入、完整源或实际画布已提速。

## 资源与释放检查

首次4K进程峰值working set1,082,884,096字节、私有提交1,539,796,992字节（约1.43GiB），包括CPU输入/对照、验证读回和驱动资源。结束时DXGI local usage687,243,264字节，这是UMA段统计，不能解释成独显实际物理占用，也不能代替进程树总RAM。

资源对象销毁后仍保留约687MB DXGI usage；新增显式`releaseCachedResources()+finish`后复跑100次，exit0/数值通过，CPU147.8082ms/GPU22.0993ms中位数，结果`gpu-local-round2-4k-release.json`。驱动段统计仍未下降，因此**不能宣称仅销毁纹理或trim就释放完资源**。

再新增整个QRhi设备销毁后的进程内采样，最后构建`gpu-local-round2-device-release-build.txt` exit0，完整100轮重跑exit0/5组数值通过，结果`gpu-local-round2-4k-device-release.json`。CPU中位数148.1588ms、GPU16.3624ms，GPU p95 23.1153ms（中位数变化不能只归因于本次清理代码，因为清理发生在测量之后）。设备销毁后同一进程私有提交222,564,352字节、working set45,285,376字节；这时没有存活设备可再查询DXGI段，不填0冒充测量。相对纹理销毁/trim后的保留有明显下降，但仍有CPU/驱动分配保留，尚无100轮创建/销毁或长期稳态趋势认证。没有把退出进程后的归零当成编辑稳态释放证明。

## 后续门槛

下一步优先把该常驻局部路径接到隔离原生呈现，保持source/mask版本与真实提交来源标记；实际计入显示ICC、显示、旧任务丢弃和资源生命周期。还需多层顺序/有界纹理预算、真实JPEG/CR3、蒙版增量上传、设备失败CPU回退及释放多轮趋势。4K实验不迁移45MP完整源到GPU，不因局部实验通过就替换生产图像引擎。

C6组合专项另见S3-C6-COMBINATIONS-RESULTS.md；全量60/61及色差拖动偶发失败仍保留，D尚待实现。没有截图、整幅图片输出、桌面包或推送。DeepSeek提供边界文档草稿；GPT纠正“pre-global输入”为实际所需的“post-global/pre-local输入”，自行完成GPU数值实现、API接线、构建与实测。
