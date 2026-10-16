# GPU混合常驻：双层与显示ICC

2026-10-16。本轮限定的双层/静态ICC原生实验通过；仍未接入生产，不宣布全部GPU计划或S3完成。下一步回到C6实际窗口/prepared组合，再推进D可靠性；GPU生产接入仍需真实源、设备故障和动态上下文门禁。

## 两层常驻与数值

第二个LocalPipeline借用第一个输出纹理，仅持有自己的输出/覆盖/增益表，不另分配源纹理或读回中间结果。资源同属一个QRhi，第二层先释放；离屏数值实例与原生窗口实例完全分离。

两层为源空间羽化径向覆盖与反相线性覆盖，各自九项局部参数及.65/.55强度。进入原生计时前，257×131离屏硬件路径验证：正序、逆序、第一层强度0、第二层强度0。逐通道对照串行CPU，RGB容差仍1e-5+1e-4×|参考|，alpha1e-6；4/4通过，最大RGB差2.384185791015625e-6，最大误差/容差约0.03028，alpha差0。正逆序GPU与CPU结果各自确实不同，未用同一个结果冒充两种顺序。

`--local-layers`仅接受1/2；两层限定`--local-present`，不是64层无界常驻实现。4K原生源图上传1次，覆盖上传2次。CPU对照调用原有渲染器/256MiB缓存，builds3、hits315、retained235,929,600字节（225MiB），暖路径复用前缀和覆盖。

## ICC方法与边界

显示路径遵循生产`transformDisplay`的顺序：先生成8位sRGB规范帧，再按目标RGB显示ICC转换设备码。新增4096²RGBA8码表，枚举全部256³ RGB，Little CMS使用与生产相同的源sRGB ICC、相对色度、COPY_ALPHA|NOCACHE；GPU按RGB字节取表，**不插值**。地址为x=r+(g%16)×256，y=b+(g/16)×256；CPU逐行生成对应RGB，临时输入仅一行。

码表CPU图像64MiB、GPU纹理64MiB，另有驱动上传暂存开销，仅生成/上传一次。目标与标准sRGB按QColorSpace相等时，沿用生产直通语义，仅绑定4字节占位纹理，不分配64MiB。ICC上限4MiB，复用既有有效RGB显示/颜色空间配置、版本2/4及意图校验；这不是“ICC class 2/4”。静态ICC字节摘要写入报告。

此表只用于SDR屏幕显示，不能用于完整源广色域/CMYK导出；没有改动导出链或将显示ICC写入成片。P3是合成测试配置，不证明本机显示器物理呈现P3。有效ICC文件输入已接通，实际仅验证合成Display P3及本机安装的Windows sRGB ICM，不能宣称所有显示器配置兼容。

## 实际运行

每组5预热+100次曝光更新，3840×2560、DPR1.25。请求/提交epoch均105，旧反馈拒绝0；最终实际颜色缓冲逐通道对照CPU渲染+显示转换，所有组最大差1码值、超过1码值通道0。GPU终点frameSubmitted、CPU终点paint完成不同，不能将表中比值当物理端到端或产品体感加速倍数。

| 双层目标 | GPU median / p95 ms | CPU median / p95 ms | 运行总时长 |
|---|---|---|---|
| 固定sRGB（首次） | 35.995 / 38.8314 | 434.6956 / 508.9582 | 61.303秒 |
| 本机Windows sRGB ICC（直通） | 34.3436 / 39.1556 | 436.8696 / 474.4705 | 59.594秒 |
| 合成Display P3 ICC（最终） | 61.6372 / 68.4018 | 590.0138 / 625.1154 | 79.460秒 |

公开脚本命令：

- `./tools/run-gpu-experiment.ps1 -ReportDirectory build/private-qa/gpu-stack-native-first -LocalPresent -LocalLayers 2 -Iterations 100 -Size 4k`
- 同入口目录`gpu-native-system-srgb`，增加`-DisplayIcc 'C:\Windows\System32\spool\drivers\color\sRGB Color Space Profile.icm'`。
- 同入口目录`gpu-stack-icc-final`，增加`-TestDisplayP3`。

三组均exit0。构建`gpu-stack-native-build.txt`、`gpu-stack-icc-build.txt`及最终`gpu-stack-icc-final-build.txt`均exit0。P3初次运行也通过（`gpu-stack-icc-first`），其scope历史文字仍写固定sRGB，和实际displayIccActive=true矛盾；最终已修正文案并重跑，不能依据旧scope否认实际执行的ICC，也不隐藏该元数据错误。

P3最终码表准备295.6471ms、67,108,864字节、上传1次；系统sRGB为4字节占位，准备0.0586ms。双层蒙版首次CPU准备约2.12秒；这些冷准备和首次上传不计入暖交互。GPU约62ms较sRGB约34ms的额外成本表明准确ICC查表有带宽代价，并未免费获得。

## 资源与检查

P3验证读回阶段峰值私有提交1,448,599,552字节（约1.35GiB）、峰值working set770,277,376字节，DXGI local783,106,048字节；不能把UMA段统计当独立物理显存相加。包含CPU源、覆盖、码表及读回/参考，不是编辑稳态。GPU widget销毁后私有提交419,373,056字节，CPU对照结束后168,050,688字节（外层157MiB源仍存活）。没有100轮设备/文档创建销毁认证。

- PowerShell AST语法检查与三个非法组合（Local两层、离屏P3、两个ICC来源）在建报告目录前拒绝，通过；`gpu-invalid-{layer-mode,profile-mode,two-profiles}.txt`。
- 将LICENSE作为非ICC字节文件传给二进制，预期exit1，报告passed=false/Invalid display profile，0.134秒正常拒绝，非崩溃；`gpu-invalid-icc-file{,-process}.json`。
- 最终单层离屏small回归100次exit0、5组数值通过，`gpu-stack-single-regression`。
- QA图片数0；64MiB表仅在内存/显存，不写成图像或磁盘缓存。无包、ZIP、快捷方式或推送。

DeepSeek提供LocalLayers/DisplayIcc启动脚本草稿，GPT收紧两层仅原生模式、保留全部路径防护并实际验证；数值/GPU/ICC由GPT实现和复核。文档草稿把“4字节占位”误写成检查ICC第4字节，并混淆ICC class与版本，未采用这些表述。worker只接收源码文本，没有照片或预览访问。

继续限制：动态显示器/配置变化、设备丢失CPU回退、源/蒙版更新、真实JPEG/CR3、非矩阵ICC更多样本及跨设备均未认证。源/蒙版固定时的结果不能冒充新笔迹33ms目标。C6实照/prepared与D仍待完成；完整61项60通过，色差拖动偶发及历史atomic_file问题仍保留。生产CPU不变。
