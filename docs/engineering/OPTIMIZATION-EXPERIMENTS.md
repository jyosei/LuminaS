# S1 优化可行性：独立实验与候选窗口

2026-10-12（Asia/Shanghai）。用户授权继续探索优化。结论：**多核显影有明确速度收益；直接线性缓冲有明确内存收益；小容量缓存对重复切图有明显潜力**。仍保持长边3840、小图原尺寸、完整RAW/AHD显影及源文件检查。本轮是S1性能实验，不实现S2调参或完整S3缓存/任务系统。

## 实测决策表

同一EOS R7/32.2944MP CR3，各模式3次，串行、未控制OS缓存/冷热/CPU温度；取解码到4K缓冲的中位数，不含屏幕绘制/像素验证hash。不是p95、45MP或目标8GB机认证。

| 方案 | 中位数ms | 进程峰值私有提交MiB | 建议 |
|---|---:|---:|---|
| 固定官方DLL＋原缓冲路径 | 4974 | 748.94 | 保留基准 |
| 官方DLL＋直接线性缓冲 | 4971 | 563.88 | 主要用于降内存，未观察明显提速 |
| 重编译OpenMP＋直接缓冲，1线程 | 4977 | 563.88 | 作为重编译控制组 |
| 同上2线程 | 3310 | 563.30 | 可用于较少核心的候选策略 |
| 同上4线程 | 2572 | 563.37 | 当前开发机推荐候选起点 |
| 同上6线程 | 2358 | 563.61 | 比4线程再少214ms；更多核心/功耗/UI需另测 |

4线程较官方基准少约48.3%，1线程重编译控制组与官方差异很小，说明主要收益来自并行，而非更换算法。6线程比4线程只再少约8.3%，暂选4作为可调候选；没有测功耗、温度或输入时延，不能把该选择宣称为已证实更省电/更流畅。

全部6模式×3次共18张RAW预览，像素SHA-256一致，并与上一轮预览一致；尺寸2560×3840。RAW/JPEG源hash仍与manifest一致。汇总工具强制检查这些条件，见evidence/optimization-summary.json。每项memory取该模式3次中的最大进程生命周期高水位，不是每阶段实时峰值，不与working set相加。

## 1. 多核LibRaw

tools/build-libraw-openmp.ps1使用同一固定官方0.22.2归档，验证归档hash，展开到独立.tools/LibRaw-openmp目录。通过官方Makefile.msvc、VS2022 x64，使用`nmake /A /f Makefile.msvc COPT_OPT=/openmp bin\libraw.dll`，/A确保归档附带object不会被重用；只重建DLL及import lib，不引入GPL demosaic packs或RAW算法改写。

实际DLL SHA-256 `92ba89268ac74c9f6865266f941d9ba08d538cc4296cf1b7631480ab425bf6b7`；dumpbin确认新增VCOMP140.DLL依赖。这是本轮构建产物hash（PE构建时间等影响重建文件hash），不要求换机重建二进制逐字节相同。官方固定DLL、build/dev与SDK保留，候选程序在build/openmp/RelWithDebInfo。

测试通过环境OMP_NUM_THREADS指定1/2/4/6；设置OMP_WAIT_POLICY=PASSIVE，但未验证该MSVC运行时是否执行此等待策略，不把环境设置当功耗证据。每次只有一个RAW任务在途，任务内部并行。

AHD显影段中位数：官方3643ms；重编译1线程3644ms；2线程2208ms；4线程1566ms；6线程1339ms。算法/WB/矩阵/裁边/方向/gamma均不变，不降低分辨率或用内嵌JPEG替代显影。

## 2. 直接线性缓冲

原路径：LibRaw生成RGB16约193.8MB，再生成RGBX64约258.4MB，然后缩到4K。候选通过get_mem_image_format验证输出尺寸/通道/bit，用copy_mem_image直接向RGBX64分配写入每行的packed RGB16，再从右到左原地扩展为RGBX64，以免覆盖尚未读取的样本。颜色变换/采样仍沿用Qt原路径。

本样片峰值私有提交约749→564MiB，减少约185MiB（24.7%），符合省去RGB16暂存的尺寸；中位耗时4974→4971ms，差异太小，不宣称速度提升。额外测试宽1/2/3/17/4640及前后哨兵，验证原地重叠安全、RGB/alpha正确，真实候选像素与基准一致。

RawOptions.directLinearPreview默认false；编译选项LUMINA_DIRECT_RAW_PREVIEW默认OFF，独立候选ON。实验完整统计或linearOutput请求仍走原参考路径，不削弱S1输出/诊断证据。完整审计输出与真正编辑母版不被此预览缓冲代替。

## 3. 缓存可行性（尚未接入窗口）

独立cache_probe使用96MiB QCache，仅缓存不可变4K图。键包含规范路径、完整源SHA-256、固定AHD/WB recipe版本、3840规格、sRGB、Qt/LibRaw版本；每次hit仍重新核对源hash，不以文件名/mtime判断未变。

| 缓存实验 | 结果 |
|---|---:|
| RAW首次miss（含额外key hash） | 5265ms（仅1次，非中位数） |
| RAW hit 5次 | 138 / 140 / 138 / 138 / 139ms，中位138 |
| JPEG首次miss | 430ms（仅1次） |
| JPEG hit 5次 | 60 / 60 / 61 / 60 / 60ms，中位60 |
| 两张2560×3840 RGB32实际缓冲 | 75MiB |
| 缓存预算 | 96MiB |

hit耗时含读取源计算完整hash/查找，不含GUI、队列、painting及用于核验的像素hash。miss有key hash开销，不直接与speed_probe的4.97秒路径混比。cache cost是逻辑缓冲计费，不等于进程峰值RAM或GPU资源。

失效测试只改临时JPEG副本：同路径、同文件大小，修改一个字节并恢复mtime，内容hash变后必须miss；通过。加入第三项/降低预算触发LRU淘汰及容量检查通过，hit像素一致。没有修改用户照片，没有把该实验缓存接入实际Window或声称已经解决S3。

建议S3先做96MiB预算的小范围邻图缓存，后台完成hash/失效检查，避免把138ms核对工作放GUI线程；加入asset/session/revision、RAW参数、几何及显示合同版本键，保证编辑后不误用旧图。磁盘代理缓存、自动预取和完整缓存框架先不引入。

## 可运行候选

```powershell
$env:QT_ROOT = (Resolve-Path '.tools/Qt/6.8.3/msvc2022_64').Path
./tools/run-optimized.ps1 -Threads 4
```

会在本机原生窗口打开现有CR3；可用-Threads 2或6对照、-Photo指定JPEG/CR3、-Smoke完成后退出。该候选由独立CMake目录链接OpenMP SDK、直接缓冲ON。默认dev仍为官方SDK/原缓冲的可复现对照，未在用户未知情况下替换系统DLL或发布软件。

候选原生测试6通过0失败（含init/cleanup），包含真实中文RAW、坏文件、4K尺寸、在途切回JPEG、防旧结果覆盖、标记保持，以及packed行重叠测试；实际候选lumina.exe RAW启动/预览提交退出0。默认工程重新构建和CTest通过，见evidence/optimized-*与optimization-default-*。

## 复现构建/实验

```powershell
./tools/build-libraw-openmp.ps1
$qtPath = (Resolve-Path '.tools/Qt/6.8.3/msvc2022_64').Path
$rawSdk = (Resolve-Path '.tools/LibRaw-openmp/LibRaw-0.22.2').Path
./.tools/bootstrap/Scripts/cmake.exe -S . -B build/openmp -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$qtPath" "-DLIBRAW_ROOT=$rawSdk" -DBUILD_TESTING=ON -DLUMINA_DIRECT_RAW_PREVIEW=ON
./.tools/bootstrap/Scripts/cmake.exe --build build/openmp --config RelWithDebInfo --target lumina speed_probe s1_tests --parallel 2
```

默认工具的preview/direct各运行3次；候选speed_probe使用direct及OMP_NUM_THREADS=1/2/4/6，各3次，报告路径必须不存在：

```text
build/dev/speed_probe.exe pictures4test/4X1A9817.CR3 preview build/private-qa/optimization/official-preview.json 3
build/dev/speed_probe.exe pictures4test/4X1A9817.CR3 direct build/private-qa/optimization/official-direct.json 3
build/openmp/RelWithDebInfo/speed_probe.exe pictures4test/4X1A9817.CR3 direct build/private-qa/optimization/openmp-4.json 3
build/dev/cache_probe.exe pictures4test/4X1A9817.CR3 pictures4test/4X1A9817.JPG build/private-qa/optimization/cache.json
python tools/speed-probe/compare_optimizations.py build/private-qa/optimization
```

第三条分别在1/2/4/6线程环境执行，文件名对应线程数；所有构建、probe、cache、汇总、原生启动实际退出0。完整私有报告在build/private-qa/optimization，共享工程只保存数字/日志，没有照片图像。

## 下一轮验证门槛（按优先级）

1. 补真实接近45MP CR3及多种曝光/模式/方向，比较全部4K像素/完整RGB16或确定误差阈值；验证坏文件、关闭、重复切图及2/4/6线程稳定性。当前只有一张EOS R7，不能认证所有输入。
2. 代表性8GB机连续10张/多轮，控制冷热、后台负载、CPU温度/电源，测进程树内存、输入/事件循环及真实呈现时延。比较4/6线程，必要时按硬件选2线程；不预先把6线程最快等同于产品最佳。
3. 若采用直接缓冲，保留参考路径对照并补不同有效宽高/方向、其他CFA/通道的拒绝/回退、分配失败、裁边模式测试；确认OpenMP运行库离线部署及许可/源码构建材料。保持32MP实测与45MP待验分开。
4. S2建立源+参数/revision后，S3再接入缓存：修改曝光/WB/裁剪、重开/源替换、同mtime变更、预算淘汰、并发失效和旧任务结果均要有回归。缓存不能成为唯一编辑母版或完整质量导出来源。

S1原有近45MP/正式RAW质量/后期可调性门槛仍待验证；本轮优化不自动使这些条目通过。
