# S1 图片读取/显影速度检查

2026-10-12（Asia/Shanghai）。结论：有提升空间，已移除窗口RAW预览不需要的实验统计，保持显影配置、源完整性检查和4K像素不变。当前仍是S1，不实现S2编辑或S3完整缓存/调度系统。

## 同条件测量

同一开发机、同一32.2944MP EOS R7 CR3及同名JPEG，Qt6.8.3/LibRaw0.22.2/MSVC2022。串行测量，不写PPM/PNG、不解码参考图、不计算对照差异、不包括屏幕绘制；计时到4K像素缓冲返回，随后计算验证像素哈希，验证开销不计入时间。OS文件缓存未控制，不能叫冷启动或p95，也不能代表目标8GB机/45MP。

| 路径 | 各次ms | 中位数ms |
|---|---|---:|
| JPEG 4K | 384 / 232 / 233 / 232 / 230 | 232 |
| RAW AHD+旧窗口的完整统计内容 | 6304 / 6219 / 6291 | 6291 |
| RAW AHD窗口预览，跳过实验统计 | 4983 / 4976 / 4967 | 4976 |

RAW中位数减少1315ms，20.90%。前后都使用新基准工具的同一二进制、同一算法，只切换collectLinearDiagnostics；“旧”指旧窗口执行的处理内容，不是拿8秒写PPM的S1实验报告直接作基准。两种模式全部6次RAW预览RGB32像素SHA-256完全一致，都是2560×3840；原CR3/JPEG SHA-256与manifest一致。

JPEG首轮384ms、后续230–233ms，仅描述此次执行顺序，不把未清缓存的首轮称为磁盘冷读。5次样本不足以给稳定p95。

## 分段瓶颈

| RAW阶段 | 旧统计路径中位ms | 新预览路径中位ms |
|---|---:|---:|
| 源SHA-256（处理前） | 139 | 138 |
| 容器/元数据open_file | 0 | 0 |
| unpack解包 | 441 | 442 |
| AHD完整显影dcraw_process | 3651 | 3648 |
| RGB16输出缓冲 | 367 | 365 |
| RGB16扫描/范围/零值/饱和值/大缓冲哈希 | 1244 | 0 |
| 构造线性图、缩小及sRGB预览变换 | 234 | 232 |
| 源SHA-256（处理后） | 141 | 139 |

各阶段中位数不必相加等于总中位数。RAW主要热点是显影，约占新路径73%；unpack约0.44秒，容器识别本身很小。两个源文件哈希检查仍保留，约0.28秒，未为速度削弱源变化检测。JPEG约232ms主要在解码/方向/缩放，当前已通过QImageReader.setScaledSize请求4K解码，没有新增全尺寸JPEG中间图。

## 已落实的优化

RawOptions新增collectLinearDiagnostics，实验CLI默认true、窗口false；只有实验路径扫描整张RGB16并hash约194MB缓冲。任何要求linearOutput的请求强制启用统计，完整S1证据不被削弱。此开关不改变unpack、AHD、相机WB、矩阵、gamma、裁边、方向、4K分辨率和预览像素。源文件前后hash、45MP预检、过期结果保护仍保留。

新增speed_probe及汇总工具；JPEG结果记录header/decode/color时间；RAW分离makeImage与diagnostics时间，总计时现包含末尾源hash核对（原S1 report elapsedMs在末尾hash之前记录）。本次未改变SDK或显影算法，内存占用未作为该优化收益承诺。

构建初次缺QJsonArray include，已修复；最终构建退出0。CTest2/2通过；真实Windows RAW、中文路径及在途切回JPEG回归通过（见speed-native-tests.txt）。本轮没有照片上传、源文件改写、假导出或其他阶段实现。

## 进一步值得验证的方向

1. **AHD多核显影**：本包src/demosaic/ahd_demosaic.cpp具备LIBRAW_USE_OPENMP并行循环；官方[编译说明](https://www.libraw.org/docs/Install-LibRaw.html)支持启用OpenMP。SDK默认Makefile未启用/openmp，实际DLL无vcomp依赖，结合源码推断当前未启用MSVC OpenMP。值得在独立构建中测2/4/6线程，逐像素对照、记录峰值内存后决定；没有重编译实测，不承诺加速倍数。仍保持一个RAW任务在途。
2. **有界4K预览缓存**：当前切回同一CR3会再次显影。按文件身份/修改状态、参数、方向/预览规格命中小容量缓存可避免重复工作，需失效规则与内存预算，属于S3进一步工作；本轮没有实现，不给“零耗时”承诺。
3. **输出缓冲/预览复制**：RGB16到RGBX64、缩放约0.6秒，有减少全尺寸暂存机会；需保持线性处理、相同采样和几何，先验证再替换。收益优先级低于3.65秒显影。

不会通过降低4K到2MP、启用低质量半尺寸显影或用内嵌JPEG冒充RAW结果来制造加速结论。

## 复现

设置QT_ROOT/LIBRAW_ROOT、dev.ps1 build后，将输出JSON路径改成不存在的新文件：

```text
build/dev/speed_probe.exe pictures4test/4X1A9817.JPG jpeg build/private-qa/speed/jpeg.json 5
build/dev/speed_probe.exe pictures4test/4X1A9817.CR3 audit build/private-qa/speed/raw-audit.json 3
build/dev/speed_probe.exe pictures4test/4X1A9817.CR3 preview build/private-qa/speed/raw-preview.json 3
python tools/speed-probe/summarize.py build/private-qa/speed
```

工具拒绝覆盖报告。详细JSON在被忽略的build/private-qa/speed；共享数值汇总在evidence/speed-summary.json，构建/CTest/原生结果在evidence/speed-*。本轮均实测退出0；一致性汇总通过才产生summary。

2026-10-12后续已完成多核/直接缓冲/缓存的独立验证，见OPTIMIZATION-EXPERIMENTS.md：4线程候选2572ms、直接缓冲峰值私有提交约564MiB、缓存取帧含源hash为RAW138ms/JPEG60ms。以上“待测方向”保留为本次速度检查时的历史判断，以最新实验报告的范围/证据为准。
