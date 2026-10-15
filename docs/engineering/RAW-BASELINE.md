# S1 RAW 基线与 4K 预览

日期：2026-10-12（Asia/Shanghai）。S1工程切片已运行；阶段**有条件通过**：当前EOS R7样片的解码、几何、输出边界已验证，近45MP真实CR3、广泛画质与可调性仍待验证。本轮没有实现S2–S7。

## 确认的要求

当前照片预览长边至少3840像素，小图保持原尺寸，按比例生成，不放大低清图冒充4K；JPEG与RAW均适用，胶片条小缩略图独立处理。目前大图长边正好3840，用户样片均为2560×3840。4K指实际预览缓冲，不声称屏幕本身是4K，不等于原图100%检查或画质认证。

用户确认RAW基础预览偏暗/偏灰可接受，不要求复刻机内JPEG。质量重点是后续曝光/白平衡/色调可调、细节与高光可用。机内JPEG只用于几何和外观描述，不设像素误差通过阈值；偏灰本身也不证明后期空间完整。本轮未做调参实验。

## 固定依赖

沿用Qt6.8.3、MSVC2022 cl19.44.35227.0、CMake3.31.6。新增LibRaw0.22.2官方Win64 MSVC2022 SDK，动态libraw.dll/import libraw.lib，没有静态链接LibRaw。

- [官方发布目录](https://www.libraw.org/download)；[固定下载包](https://www.libraw.org/data/LibRaw-0.22.2-Win64.zip)。SDK附源码/许可证，未修改。
- 归档SHA-256：`ac64fa12bb00a7581332d4c6ab918c0533fb3f119d6b668d47a6875410dca948`（本轮固定哈希，非发布者签名）。
- DLL SHA-256：`6a459c22039abf0eac4d263673337c8ed5f223acbd372fcf77610debf80ac8cd`。
- 实际API `0.22.2-Release`；编译期头文件与运行时版本数均核对0.22.2。
- capabilities=0：没有报告RawSpeed/DNG SDK/GPR SDK/zlib/JPEG等可选能力。wchar_t中文路径实测通过；源码capabilities()的UNICODEPATHS报告在DNGSDK条件块内，bit未设置不等于中文路径不可用。
- 使用内置AHD/DCB，没有引入GPL demosaic packs。源码COPYRIGHT提供LGPL2.1/CDDL1.0选择，按动态LGPL2.1路线准备，保留LICENSE.LGPL、LICENSE.CDDL及COPYRIGHT；正式分发清单留到S5/S7，自有许可证未定，没有新增正式LICENSE。

本轮使用官方预编译DLL，不宣称自行编译LibRaw源码。CMake通过环境LIBRAW_ROOT寻找SDK并复制DLL到开发程序目录，不硬编码本机路径。实际链接/启动已验证；LCMS、SQLite、GPU、RAW隔离worker均未引入。

## 样本与几何

`tests/fixtures/manifest.json`记录路径、尺寸、哈希及用途。用户仅授权本地测试，照片及派生输出均在被忽略的pictures4test/build目录，未上传。

| 项目 | 实际结果 |
|---|---|
| 输入 | 4X1A9817.CR3，21,525,850 bytes，Canon EOS R7 |
| RAW SHA-256 | e8268916d0f88cab514fa57dfa2701dcbad428804221b251da9166278cfbff2f |
| 传感器容器尺寸 | 7128×4732 |
| 初始可见尺寸 | 6984×4660 |
| 厂商inset crop | 6960×4640，left156、top84；mask=1 |
| 方向/输出 | flip=5（90°逆时针），4640×6960 |
| 同名机内JPEG | 编码6960×4640，归一4640×6960，32.2944MP |
| 当前预览 | JPEG/RAW均2560×3840 |

流程：open_file → 有效尺寸预检 → adjust_to_raw_inset_crop(1) → 再预检 → unpack → dcraw_process → dcraw_make_mem_image。没有调用unpack_thumb或使用嵌入JPEG代替显影。qint64像素乘积≤45,000,000，unpack前raw缓冲分配限额1024MiB；这不是全进程所有分配总上限。真实45MP CR3仍缺。

## 显影与输出合同

仅user_qual不同：AHD=3、DCB=4。共同配置：camera WB，auto WB关闭；camera matrix1；half_size0；output_color1（sRGB基色）；output_bps16；gamma(1,1)；no_auto_bright1；no_auto_scale0（保留缩放）；bright1；highlight0；adjust_maximum_thr0；DCB iterations2/enhance0；median/FBDD/阈值降噪关闭；RawSpeed关闭；文件方向。完整配置见metrics。

RGB16线性是白平衡、黑位处理、矩阵和缩放之后的0…65535有界输出，不是传感器原始样本，也不是容纳负数/超白的完整scene-linear浮点母版。源CR3保留；S2必须验证可调性/高光/工作域并从源重算，不能把该中间图作为唯一真相。

CLI实验输出为全尺寸、方向归一的RGB16 PPM/P6，大端samples、无ICC；应结合JSON解释为linear sRGB，普通查看器可能误按gamma图解释。这是S1数值基线，不是正式JPEG/TIFF导出或S4事务。拒绝覆盖任何已存在输出/源图。

完整RGB16构成RGBX64 linear sRGB，先在线性值上缩小，再经Qt变换为encoded sRGB RGB32预览，未重复gamma或仅贴标签。显示器ICC、HDR/Advanced Color及S2的LCMS合同未认证。

## 实测结果

`evidence/s1-metrics.json`：AHD/DCB各一次，串行；未清文件缓存/未控制冷热，开发机见ENVIRONMENT.md，不是p95/屏幕时延/8GB目标机认证。

| 指标 | AHD | DCB |
|---|---:|---:|
| RAW管线总ms | 8031 | 11067 |
| open ms | 1 | 1 |
| unpack ms | 449 | 443 |
| dcraw_process ms | 3698 | 6851 |
| make_image+统计ms | 1622 | 1589 |
| 4K预览生成ms | 279 | 265 |
| 峰值私有提交MiB | 748.55 | 1056.26 |
| 峰值working set MiB | 755.14 | 1062.77 |
| RGB16 R/G/B在65535样本数 | 12760 / 880 / 11153 | 13659 / 464 / 11933 |
| RGB16 R/G/B在0样本数 | 3524386 / 6327845 / 6082396 | 3526510 / 6308548 / 6133478 |
| 机内JPEG encoded RGB8 MAE | 75.58 | 75.59 |

总耗时包含源哈希、RAW显影、统计、PPM写入/校验、4K转换，不包含函数返回后的参考JPEG解码/PNG编码和对照图写入；各项不是完整总耗时拆分。内存为RAW函数末尾查询的Windows进程生命周期高水位，包含此前步骤峰值，不覆盖随后PNG/参考JPEG的全程峰值，不与working set相加，不含整机/GPU。

两个RGB16有效数据均193,766,400 bytes，文件193,766,419 bytes（含19-byte PPM头）。独立Python核对尺寸、payload、大端samples恢复后的buffer哈希与报告一致，见evidence/s1-ppm-verification.json；算法结果不同，没有复用同一张缓存图。0/65535仅为输出边界计数，不能单独解释为传感器细节丢失或高光可恢复。

对照图已视觉检查：方向/几何一致；RAW整体较暗、背景较灰，用户接受此默认外观，不作为失败。AHD/DCB整体近似，不能用一张图宣布某算法画质更优。JPEG误差仅描述，不是DeltaE/肤色质量或通过门槛。保留no_auto_bright，不为拟合机内外观改配置；暂用较省时/内存的AHD作为实验默认，后续质量测试再决定。

## 实际命令与证据

设置QT_ROOT与LIBRAW_ROOT后，dev.ps1 configure/build均退出0（完整26步构建）；最终CTest 2/2通过。未设置LUMINA_REAL_SAMPLES时真实case跳过。原生JPEG case含init/cleanup为3通过0失败；S1原生case含init/cleanup为5通过0失败，包含中文RAW路径、损坏/缺失文件、RAW在途切回JPEG不被旧结果覆盖、标记保持。

两次probe（把ahd及对应文件名前缀改为dcb进行第二次）均退出0：

```text
build/dev/raw_probe.exe pictures4test/4X1A9817.CR3 --algorithm ahd --linear build/private-qa/s1-baseline/ahd-linear16.ppm --preview build/private-qa/s1-baseline/ahd-preview.png --report build/private-qa/s1-baseline/ahd.json --reference pictures4test/4X1A9817.JPG --comparison build/private-qa/s1-baseline/ahd-vs-camera.png
```

随后 `./tools/check-s1.ps1 -OutputDirectory build/private-qa/s1-baseline -SkipProbes` 退出0；分别启动实际lumina.exe打开JPEG/CR3，两次退出0、源哈希不变。`python tools/raw-probe/verify_outputs.py build/private-qa/s1-baseline`退出0。把--linear指定原CR3的负向试验退出2（预期拒绝），源哈希不变。

完整本地输出/报告/截图在build/private-qa/s1-baseline；共享工程证据仅数值与日志，不含照片。真实Qt窗口截图已检查尺寸标签、竖向显示与缩略图；不当作性能/画质认证。

## 未通过门槛与下一阶段

- 仅一张EOS R7/32.29MP，近45MP真实CR3、其他机型/模式/光照、高光/肤色/细节待验。
- 后续曝光/RAW WB/色调可调性未做；没有固定DPP/RawTherapee参考或受控色卡/光照，未测DeltaE。
- 没有目标8GB机/p95/十张流程认证。
- GUI单后台任务串行，CR3切入重新显影；缩略图从选中后的真实显影结果生成，未选中RAW先占位。关闭等待在途任务，尚无S3隔离worker/完善取消/进程崩溃保护。
- S2需建立源+参数和浮点工作域，验证正常后期调整与4K/全尺寸一致计算；不得放大代理当完整输出。本轮在S1停止。
