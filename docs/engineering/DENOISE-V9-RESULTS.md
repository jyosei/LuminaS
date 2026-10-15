# 降噪统计优化与拖动历史修复

2026-10-13，LuminaS，沿用Qt6.8.3/MSVC2022/LibRaw0.22.2/LCMS2.19.1。目标是降低DENOISE-V8首次准备延迟，保持4K、算法公式、物理半径和完整源路径。

## 修改

- 同一遍RGBA读取提取Y、R−Y、B−Y及alpha；按行遍历，去掉每像素重复整数除法与重复RGB读取。
- Gaussian统计移交临时vector所有权，复用scratch，减少复制与分配。
- 部分透明图按亮度/色差半径各算一次alpha权重，两个色差共用。原三轮扩展盒核、eps、sigma、工作域与512/128行策略未改。

## 初始发现与修复

`denoise-v9-math.txt` 6 passed、0 failed、170ms：新实现与保留的原逐通道/重复alpha归一计算参考在负值/超白/透明图上最大误差<1e−6；既有噪声、条带/整幅、原位/缓存、范围/非法输入与缓存检查通过。

第一次真实RAW拖动`denoise-v9-raw.txt`失败：三段反馈13/10/6帧，曝光Undo实际回到.18而非0。原因路径为数值框editingFinished在任一滑杆仍down时调用endControlEdit，提交了中间draft。原来直接连接该信号；现在检测所有滑杆，拖动期间忽略数值框完成通知，由sliderReleased统一提交。显式Undo/切图/比较等调用仍可结束调整。

新增`spinFinishDuringSliderDoesNotSplitHistory`主动触发当前与其他数值框的editingFinished，检查一次Undo回初始、Redo回最终；原失败日志保留。最终构建/原生/完整回归与源计算将在实际运行后记录，不降低帧数或历史断言。

## 后续定位与干净原生结果

全滑杆完成通知保护的强制回归、原计算参考、非法输入/缓存、文字焦点/IME/Esc及窗口手势9 passed、0 failed、1342ms，见`denoise-v9-fixed-math-history.txt`。首次保护后仍出现一次Undo回到.4的失败（`denoise-v9-fixed-raw.txt`），其完整触发来源尚未捕获；不以一次成功覆盖此记录。

随后实时跟踪4轮、缓冲跟踪2轮均记录预热命令+三条滑杆命令，曝光Undo回0；关闭跟踪的RAW/JPEG也各通过。临时生产/测试跟踪代码已移除，源码中无LUMINA_HISTORY_TRACE/BufferedHistoryTrace。最终干净构建日志`denoise-v9-clean-build.log`退出0，RAW/JPEG独立检查各3 passed，21345/21049ms，见`denoise-v9-clean-{raw,jpeg}.txt/json`。

干净构建首次准备RAW1964ms（前批2581ms）、JPEG1926ms（前批2905ms）；分别约减少24%/34%，属于本机单次对照而非p95统计结论。三段反馈RAW14/14/15、JPEG14/15/15帧/约2秒，一次Undo回曝光0。此前未启用日志的复测也通过，1999/1875ms；初始失败与各追踪日志全部保留。默认完整回归与全源像素/资源结果另列，仍须关注偶发历史故障的再现。

## 全源像素、资源与启动

与V8相同的去雾/纹理/清晰度/锐化/两种降噪/曝光组合配方，从完整源计算；三条probe独立运行退出0，4K与完整输出像素SHA均与对应V8相同，见`denoise-v9-{raw,jpeg,45mp}-equivalence.json`。负值/超白/alpha参考与工作域合同未变。

| 输入 | 4K计算ms | 完整计算ms / 尺寸 | 峰值提交MiB |
|---|---:|---|---:|
| R7 RAW 9579 | 3702 | 9360 / 6960×4640 | 1738.94 |
| R7竖幅JPEG 0705 | 3461 | 9107 / 4640×6960 | 1739.27 |
| 45MP合成JPEG | 3462 | 12806 / 9000×5000 | 2418.15 |

V8对应完整耗时12310/13136/16573ms，当前单次分别约降低24%/31%/23%，仍非统计基准或正式输出UI性能。4K长边3840、小图原尺寸规则未改。合成45MP不替代真实RAW/摄影质量。12份原片SHA最终全部与manifest一致，见`denoise-v9-source-hashes.json`；最终LuminaS.exe原生启动/关闭退出0，见`denoise-v9-startup.log`。

## 尚未通过

干净构建完整CTest3/3、0失败、130.61秒（S2 126.91秒），见`denoise-v9-ctest.log`。强制完成通知回归与所有既有默认源/颜色/几何/历史/反馈检查通过；真实照片仍由独立原生与probe覆盖。当前检查无失败，但初始两次历史故障保留，上述防护与重复结果不构成所有物理设备/焦点事件无故障的证明。

V8首次启用仍约2.6–2.9秒；本优化尚未由最终性能复测证明达到即时目标。4K稳定帧率/p95、摄影降噪质量、目标8GB/真实45MP RAW仍未认证。传统降噪公式与范围、原图安全、完整源与资源边界见DENOISE-V8-RESULTS；S2其余交集/S3准备目标保持完整。
