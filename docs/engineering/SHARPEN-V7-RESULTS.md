# 传统锐化扩充与验证

2026-10-13，LuminaS。生产Qt/C++ CPU float32路径，Qt6.8.3/MSVC2022/LibRaw0.22.2 OpenMP4 PASSIVE/LCMS2.19.1；未引入浏览器图像引擎或AI。

## 行为与模型

细节卡片新增锐化强度0–2、原图像素半径.5–4（默认1）、线性亮度边缘阈值0–.1。强度默认0不改变图片；参数、单项复位、每图历史与Undo/Redo沿现有交互。阈值不是独立蒙版，传统降噪尚未实现。

亮度USM：中性几何图Y−Gaussian近似(Y)得到D，alpha归一；最终RGB在去雾/空间增益后加amount×sign(D)×max(abs(D)−threshold,0)，再全局调色及SDR。负值/超白保留，透明RGB不污染邻域。半径按方向归一原图像素定义，4K按sourcePixelsPerPixel换算，不按屏幕缩放，不再次乘原图长边。数学/范围/算子顺序详见COLOR-CONTRACT。

强度/阈值变化复用D；半径变化须重新计算D，不能复用错误半径。首版RAW半径拖动仅4帧/约2秒后，增加四线程亮度提取、半径改变复用已有不可变几何图（仍重建统计，不反复做几何采样），复测结果随后记录。旧日志保留，不能拿优化前数字充当最终结果。

## 首版证据与完整源

- `sharpen-v7-math.txt`：数值/阈值/JSON、原图尺度方差/alpha、缓存、去雾组合，6 passed、0 failed、69ms。优化后缓存检查已更新为半径变化不再采样几何、翻转变化必须重采样，须以最终日志为准。
- `sharpen-v7-live-raw.txt/json`：R7 CR3原生三段各约2秒，强度13、半径4、阈值9帧；3 passed、0 failed、20739ms。连续4K反馈与一手势一条历史通过，非30/60FPS认证。
- 完整源使用同一配方：曝光.1、去雾.2、纹理.2、清晰度.1、锐化强度1/半径1.5/阈值.01，无裁剪。工程probe非正式输出UI。

| 输入 | 源读取ms | 4K尺寸 / 计算ms | 完整尺寸 / 计算ms | 峰值私有提交MiB |
|---|---:|---|---|---:|
| 4X1A9579.CR3 | 6893 | 3840×2560 / 2294 | 6960×4640 / 3378 | 1738.62 |
| 4X1A9579.JPG | 5822 | 3840×2560 / 2174 | 6960×4640 / 3121 | 1739.13 |
| 45MP合成JPEG | 6909 | 3840×2133 / 2051 | 9000×5000 / 4544 | 2417.33 |

首版三条probe退出0、sourceUnchanged=true，日志`sharpen-v7-{raw,jpeg,45mp}.log`，诊断PNG/JSON留本地build/private-qa/s2。构建成功。优化只改变并行提取与缓存复用，不改变公式；最终回归另列，不能据此假定未跑检查通过。

## 并行/缓存优化复核

`sharpen-v7-final-native.txt`：5 passed、0 failed、19257ms。数学与尺度/alpha/缓存检查通过；同一真实RAW下强度12、半径7、阈值9帧/各约2秒，半径由首版4帧增加到7帧。单次对照仍存在负载变动，未给出p95/30FPS保证。初次截图在Undo切图重新准备时抓取，不能作为最终照片显示证据；测试已增加最终路径/控件就绪等待，截图将以复测为准。最终默认CTest和完整源优化后复算继续记录。

优化核心完整回归：CTest3/3、0失败、115.28秒，S2为111.48秒，见`sharpen-v7-final-ctest.log`。之后只增加测试截图等待，没有修改生产算法。优化后RAW/JPEG/45MP组合完整源三条probe再次退出0，预览与完整输出像素SHA分别与首版完全一致，见`sharpen-v7-parallel-equivalence.json`；峰值私有提交1738.32/1738.46/2418.02MiB。最终probe部分与默认测试并行，不用其耗时宣称速度提升；上表首版性能为先前独立运行。12份原片SHA全部与manifest一致，见`sharpen-v7-source-hashes.json`。

最终就绪等待版原生RAW复测：3 passed、0 failed、22090ms，三段仍为12/7/9帧，见`sharpen-v7-ready-native.txt`与`sharpen-v7-ready-live-raw.json`。已查看`build/private-qa/s2/sharpen-v7-ready-raw-ui.png`，照片/4K信息/细节卡片正常显示，撤销后不再截取准备状态。最终LuminaS.exe原生启动/关闭退出0，见`sharpen-v7-startup.log`。

## 资源与质量边界

竖幅R7 JPEG 4X1A0705.JPG原生反馈补充：3 passed、0 failed、18930ms，强度11/半径7/阈值9帧/各约2秒，见`sharpen-v7-ready-jpeg.txt`与`sharpen-v7-ready-live-jpeg.json`。最终路径/控件准备、4K与两次Undo通过，已查看本地最终JPEG截图；不能把这些工程检查代替不同肤色/光照/噪声质量验收。

额外D图每像素4bytes；组合纹理/清晰度基底28bytes/像素，R7 4K262.5MiB、方形4K393.75MiB，去雾至多再8MiB。45MP组合峰值接近2.5GiB目标，真实45MP RAW/8GB目标机仍待测。合成平色JPEG仅验证尺寸/资源，不是摄影锐化质量参考。

阈值可抑制微小反差，但不能替代降噪；高强度可能强化噪声、肤质或光晕，未做商业等价/人像保护声明。4K预览不代表原图100%细节，完整源采用同一物理尺度，但先缩采样与锐化不交换，不能宣称逐像素相同。传统降噪、独立边缘蒙版、颗粒、光学/校准/吸管等缺口继续列于S2-COMPLETION-AUDIT；S3仍仅准备。

参考[Adobe官方Unsharp Mask参数说明](https://helpx.adobe.com/photoshop/desktop/effects-filters/smart-filters/sharpen-images-with-unsharp-mask.html)。使用本工程已有可分离扩展盒Gaussian近似，无第三方商业算法代码。
