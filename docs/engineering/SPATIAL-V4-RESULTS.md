# 第四批：纹理、清晰度与解析/缓存检查

2026-10-13，LuminaS，Windows 11 / Ryzen 5 5500U / 可见5.87GiB，Qt6.8.3 / MSVC2022 / LibRaw0.22.2 OMP4 PASSIVE / LCMS官方2.19.1核心。仍为真实C++/Qt CPU图像引擎；未缩小4K要求，原片只读。

## 实现

“质感”卡片增加纹理/清晰度±1，不是摆放无效滑杆。线性工作域的log亮度高频/中频基底、透明度加权、三轮可分离扩展盒核、连续sigma方差与原图像素尺度全部实际实现，见render/spatial_operator.*及COLOR-CONTRACT。负值/超白保留到SDR；不是去雾、AI肤色保护或商业算法等价。

空间基底在几何采样之后、全局光色之前，最多保留一份不可变RGBA32F图与两张float细节图。曝光/曲线/HSL/分级等改变后仍复用；RAW精算或几何改变重建。初版放在全局之后会让基本参数拖动重复滤波，已调整顺序并复测组合。完整源与4K同序重放。唯一拥有的完整源构造阶段移交QImage，共享4K路径写时复制，避免额外复制一张45MP float。

SourceFrame新增sourcePixelsPerPixel，预解析/裁剪/旋转计算并传递原图尺度。工作缓存升级LM4KF003：header/pixel分别SHA-256，默认完整几何比例核验、旧/损坏拒绝；预算按原写入字节记账，截断后替换不会错误扣除截断尺寸。不是永久项目格式。RAW WB未变化时使用精确恒等draft矩阵，避免无意义逆矩阵与空间基底误失配。

## 实际结果

| 检查 | 实际结果 | 证据 |
|---|---|---|
| 最新构建 | 退出0，无编译警告 | evidence/spatial-v4-cache-budget-build.log |
| 最终默认CTest | 3/3通过，92.41秒 | evidence/spatial-v4-final-ctest.log |
| 方向/ICC/空间数学 | 8通过0失败，310ms | evidence/spatial-v4-stage-math.txt |
| 缓存损坏/截断预算/组合参考 | 5通过0失败，88ms | evidence/spatial-v4-cache-final.txt |
| 12张真实R7空间编辑 | 3通过0失败，131182ms；每次调参sourceRebuilt=false | evidence/spatial-v4-real.txt |
| 真实4K组合拖动 | 3通过0失败，37677ms；含RAW首次解析与WB松手精算 | evidence/spatial-v4-live-stage.txt/json |
| 12图全预解析+单RAW空间编辑稳定阶段 | 3通过0失败，124746ms；100ms采样 | evidence/spatial-v4-steady.txt、steady-resources.json |
| RAW/JPEG/45MP合成源完整计算 | 三次退出0，源不变 | evidence/spatial-v4-final-results.json、final-raw/jpeg/45mp.log |
| 源安全 | 12份源SHA与manifest均一致 | evidence/spatial-v4-source-hashes.json |

空间数学检查包含正负纹理趋势、灰阶/负值/超白/透明边缘、基底复用与失配、组合曝光/曲线/HSL/分级后的float+LCMS对照最大1码值。点扩散矩在两个采样尺度下的原图方差同为2.25，验证半径换算，不用截图替代坐标证据。

新增JPEG八种EXIF方向与镜像逐角浮点核对；有效乱序ICC片重组并对照AdobeRGB一次输入转换；缺片/重复/不一致计数/零序号/非法ICC/非RGB灰度ICC/超过4MiB均拒绝。负向非法ICC触发Qt的“Failed ICC signature test”警告，属于预期坏输入日志，不是编译或测试失败。

4K每项约2秒拖动：纹理10帧、清晰度14、曝光15、对比14、HSL色相7、RAW暖冷7；均有实际Qt绘制反馈。空间效果保持开启测试其他参数；不宣称30/60FPS、DWM光子延迟或目标机p95。RAW暖冷释放仍从源精算，测试总时间包含该过程。初版两项单独数据11/14帧保留在spatial-v4-live-raw.json，最终组合以stage日志为准。

## 完整源与资源

真实9579 RAW/JPEG原6960×4640，crop[.1,.1,.8,.8]、2°、EV.15、texture.4、clarity.3：预览3840×2560，完整5568×3712。RAW读取6965ms、预览1888ms、完整3888ms、峰值私有提交1288.35MiB；JPEG5825/1813/3790ms、1289.39MiB。合成45MP JPEG完整9000×5000、预览3840×2133，读取6912ms、预览1616ms、完整3133ms、峰值2241.84MiB。未放大代理图，诊断PNG不是正式导出事务/产品UI。

12张空间编辑循环峰值私有提交1733.14MiB、working set1442.87MiB。另一独立预解析12张+单RAW空间帧进程峰值1355.49MiB、working set1194.51MiB；准备完后的观察上界私有提交841.01MiB、working set822.81MiB。稳定marker采样尾部包含关闭释放，因此不把235MiB最小值当编辑占用；此处只用上界。测量对象是QtTest进程，当前无RAW外部worker，非8GB正式认证/全进程树未来worker认证。

基底按24bytes/像素计：R7 4K为225MiB，方形4K为337.5MiB；实际整体资源不等于此单项理论大小。合成45MP峰值低于当前2.5GiB峰值目标，仅证明该输入/开发机，不代表真实45MP RAW或高细节质量。冷暖/系统负载未严格控制；完整源各次单次计时不作p95。

原生窗口图在build/private-qa/s2/spatial-v4-real-ui.png，已检查LuminaS字标、真实12张胶片条、质感卡片与4K信息。初版后全局方案的完整probe日志spatial-v4-full-*保留，最终顺序使用spatial-v4-final-*；不将旧像素hash当当前顺序输出。

12张逐图空间编辑回归在算子顺序调整前运行，其他全局参数均中性；最终新顺序另外由真实RAW六项组合拖动、12张预解析/稳定场景、R7 RAW/JPEG/45MP完整源以及默认回归验证，没有把原12图记录改写成重跑后的记录。实际新LuminaS.exe无输入startup smoke也退出0，见spatial-v4-startup.log。

## 未完成

去雾、传统锐化/降噪、精确取色、光学/更多风格等共同类别仍需补；全源细密纹理抗混叠、摄影肤色/halo与噪声质量、目标8GB/真实45MP RAW、每屏ICC/HDR/色准仍未通过。当前是自有参考空间算子，负纹理是柔化局部反差，不冒称智能美肤。S3计划/架构已据现有基底预算和实际halo更新，局部/仿制/worker实现尚未开始。继续开发目标未完成。
