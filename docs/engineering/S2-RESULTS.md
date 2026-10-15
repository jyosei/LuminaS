# S2 实际实现与验证

2026-10-12，Windows 11 / Ryzen 5 5500U / OS可见5.87GiB内存。用户已授权S2，并追加4K缓存调参、拖动实时反馈和导入即预解析。当前结论：**本机R7工程范围有条件通过**；S3–S7未展开，工程PNG不等于正式导出功能。生产引擎为C++/Qt/LibRaw/Little CMS，没有使用历史浏览器原型。

## 已完成

- 每图独立recipe：曝光、对比度、饱和度、RAW相机通道WB/JPEG RGB平衡、裁剪和校直；源片只读。跨图共享Undo/Redo，一次拖动一条历史；原图比较、文本Ctrl+Z/IME/Esc和过期结果保护。
- JPEG保留方向/ICC，一次转换到float32线性Rec.2020 D65；RAW AHD相机RGB16桥接后转工作域，WB释放从原CR3重算。float工作域保留矩阵产生的负值和超白，显示才进入sRGB SDR；详细边界见COLOR-CONTRACT.md。
- 当前几何的4K工作缓存供基本调参。独立即时反馈线程读取不可变快照，当前图精算和导入预解析不阻塞该线程。RAW WB拖动采用相对通道增益反馈，松手精算；校直拖动采用画布变换，松手从完整源重新采样。
- 导入立即排队所有新图片，后台串行解析完整源并生成4K缓存，胶片条显示就绪/失败和总进度。已准备的显示帧点击同步显示；后台核验源SHA和加载编辑数据，期间面板明示准备中。显示缓存512MiB LRU；本地会话float32工作缓存4GiB LRU，正常退出清除。
- 完整源不随导入张数驻留；显示缓存不携带每图float工作帧。工作缓存包含源身份、原尺寸、RAW矩阵/WB和像素校验；损坏、源变化及预算淘汰会重新处理。首次尚未预解析完成的照片仍需等待，预算不是无限容量承诺。
- 工程render_probe从完整源应用同一recipe输出完整尺寸，预览长边3840、小图保持原有效尺寸；完整输出不放大代理。有效像素上限45,000,000；RAW厂商有效裁框后检查，真实R5仍未验证。

## 实际构建与测试

Qt6.8.3 / MSVC2022 cl19.44.35227 / CMake3.31.6 / LibRaw0.22.2 OpenMP，4线程PASSIVE；Little CMS官方2.19.1标签核心静态库。该标签头文件及运行时编码为2190，测试按实际值验证；未启用GPL加速插件。固定依赖与命令见ENVIRONMENT.md、BUILD.md。

设置QT_ROOT、LIBRAW_ROOT为OpenMP SDK、LCMS_SOURCE_DIR后实际执行tools/dev.ps1 configure、build、test。最终构建退出0；最新CTest **3/3通过，24.34秒**，见evidence/s2-build-final.log、s2-ctest-final.log。默认测试的真实照片case需显式环境变量启用，跳过不当作已测。

独立实际lumina.exe --smoke-test分别打开9579.CR3及JPG，Windows QPA，均退出0；总运行11934/9891ms，含启动、解析、提交和关闭，非单独画面呈现延迟。见evidence/s2-startup-results.json。最终原生缓存窗口截图已检查，12/12就绪、画面与中文控制面板正常，私人截图留build/private-qa/s2/native-prefetched-final.png。

| 实际验证 | 结果与证据 |
|---|---|
| ICC/数学/浮点 | 灰阶与曝光、嵌入AdobeRGB/无ICC/非法ICC、负值/超白/alpha/NaN/Inf、融合参数；65536组快速SDR与LCMS参考最大差1个8-bit码值 |
| 几何/状态/UI | 裁剪、校直、预览/完整源坐标、透明边缘、每图recipe、跨图Undo/Redo、拖动合并、Ctrl+Z/IME/Esc、快速切图和错误文件、缓存预算/身份回归 |
| 12张真实R7编辑 | 六组CR3/JPEG，32.2944MP，横竖图、4K、参数/几何/WB重算及快切；3 passed、0 failed、138838ms；evidence/s2-native-r7-final.txt |
| 实际RAW连续拖动 | 五个基本参数每约2秒27–29帧，共141帧；后台单帧60–112ms，均值68.91ms；校直40次Qt绘制；evidence/s2-live-r7-final.json/txt |
| RAW精算期间继续拖动 | 约2秒曝光拖动22帧，后台单帧67–110ms；两个真实交互case及init/cleanup共4 passed、0 failed；evidence/s2-live-r7-final.txt |
| 导入自动预解析12张 | 未逐张点击就生成全部缓存，完成114229ms；预解析期间拖动16帧，后台69–93ms；随后12次点击均sourceRebuilt=false；3 passed、0 failed、130677ms；evidence/s2-prefetch-r7-final.json/txt |
| 缓存点击与内存 | 11张同步显示24–38ms，首张350ms；Qt绘制派发25–353ms；源核验/工作数据准备439–1486ms。目标QtTest进程峰值工作集1216.19MiB，私有提交1411.90MiB；evidence/s2-prefetch-resources-final.json |

帧计数包含实际Qt绘制检查，但不是DWM屏幕光子时延；不宣称达到30/60FPS。计时为本次本机运行，缓存/系统负载不受严格控制，不代替目标8GB机器p95认证。RAW WB松手精算仍为秒级，基本调参实时反馈不会等待该精算。

## 完整源计算证据

真实4X1A9579.CR3及JPEG使用同一recipe：EV−.5、contrast .1、saturation .9、warmth .15、tint−.1、crop [.1,.1,.8,.8]、angle 2°。preview3840×2560；full5568×3712，从原6960×4640源计算。RAW源读取6847ms、预览830ms、完整渲染1628ms、包含PNG写入总20585ms；峰值私有提交892.91MiB。JPEG源读取5767ms、预览766ms、完整1590ms、含PNG总17105ms；峰值私有提交892.96MiB。两源SHA不变，工作RAW范围−.0780至2.0377。

详细JSON及诊断PNG留在本地被忽略的build/private-qa/s2/final-raw、final-jpeg前缀；未上传照片。不把此高光范围称为恢复传感器已丢失信息，亦不把诊断工具称为正式导出事务。

45MP合成JPEG（9000×5000）通过当前float入站及完整9000×5000计算，预览3840×2133；源读取6887ms、预览764ms、完整298ms、含PNG总11035ms，峰值私有提交1036.29MiB。该合成图内容简单，不能代表真实高细节RAW速度/质量。45,005,000像素JPEG按预期退出1拒绝；证据evidence/s2-45mp-probe.log、s2-size-results.json，JSON位于build/private-qa/s2/final-45mp.json。12张真实原片最终SHA全部不变，见evidence/s2-source-safety.json。

## 已修复的失败与未通过事项

初始LCMS版本断言错误假设2191，修正为实测2190。同步缓存展示令旧S0测试的异步信号数量假设失效，改为检查明确的最终成功/错误结果；最终默认测试已通过。预解析内存初测的QSignalSpy保留每帧图像造成测量额外占用，改用轻量计数后复测，不用旧峰值代表应用。相关早期日志保留作排查历史。

尚未通过：更多相机/真实45MP RAW和正式质量参考、色卡ΔE/校准屏/每屏ICC/宽色域HDR、细纹抗混叠及正式输出锐化、不同DPI/设备人工操作、目标8GB机器资源和p95认证。RAW仍有相机RGB16/传感器/黑电平内部边界；暖冷参数不是物理开尔文色温。参数/选片仅内存会话，永久保存/恢复、RAW硬取消与隔离、强杀缓存残留清理、局部/仿制、组同步、正式导出和干净机器发行未实现，正式源码LICENSE未决定。
