# 第一批调参扩充与Qt工作区

2026-10-12–13（Asia/Shanghai）。用户授权“开干”后，按ADJUSTMENT-COVERAGE.md第一批顺序推进。此轮实现5个新参数和真实UI整理；不把全套商业软件交集或完整S3–S7写成已经实现。正式颜色/目标机门槛仍沿用S2未通过项。

## 实际功能

全局参数从5项增加到10项：原曝光、对比度、饱和度、RAW通道平衡/JPEG RGB暖冷两项，加高光、阴影、白色、黑色、鲜艳度。新参数均±1归一强度、0中性值；每图独立recipe，保持旧JSON默认兼容，单项↺复位可撤销。裁剪/校直另计，不凑入全局参数数量。

四个影调参数使用亮度区域权重，对RGB同比增益；白/黑控制亮暗端密度，不是硬剪切设白点/黑点。鲜艳度按颜色当前饱和程度衰减，低饱和色作用更大，灰色不染色。两者均为Lumina参考模型，不声称复刻Adobe/PixCake、物理高光重建或自动肤色保护。完整公式、次序和浮点边界见COLOR-CONTRACT.md。

新算子作用当前不可变float32 4K几何缓存，4线程融合到sRGB输出，不额外分配每帧约150MiB中间图。完整源按相同recipe计算完整目标；非线性算子不与缩小平均交换，预览和完整结果不承诺逐像素相同。源坐标和调整定义一致，不累计修改显示RGBA8或放大代理充当完整输出。

Qt工作区采用原设计/交接修订：深灰框架、属性面板和中性画布，少量紫色强调；手动深浅切换；选片隐藏侧栏、编辑恢复，切换保留参数/选片；可折叠白平衡/光线/色彩/构图，单项复位、数值输入、可滚动侧栏。收起参数组只改变可见性，不关闭效果。撤销按钮保持短文案，目标照片/命令在tooltip；画面行显示短文件名、实际预览尺寸和质量，完整源/颜色说明放tooltip。小手拖动/指针中心滚轮缩放继续有效。

默认窗口按当前屏幕可用逻辑尺寸收敛，预留任务栏/边框空间；小屏通过侧栏滚动访问控件，不固定启动为超高窗口。极小屏幕及跨屏改变缩放的人工体验仍未认证。

真实直方图：当前sRGB预览最多约16K抽样点、RGB256桶、对数高度，透明像素排除；黑/白端提示为任一通道接近0/255的比例，tooltip写明显示统计边界，不代表RAW传感器饱和或全源逐像素统计。

## 构建与验证

沿用Qt6.8.3 / MSVC2022 x64 / LibRaw0.22.2 OpenMP4线程PASSIVE / Little CMS核心。设置BUILD.md环境后执行tools/dev.ps1 build、test；最终构建退出0，CTest **3/3通过，37.43秒**。证据evidence/adjustments-build.log、adjustments-ctest-final.log。

Windows QPA下selectiveToneAndVibrance、fusedPreviewMatchesFloatReference及工作区/历史交互检查5 passed、0 failed；新参数分区方向、低饱和色/灰色/纯色、负值/超白、recipe范围/序列化及融合输出与float+LCMS参考差≤1个8-bit码值通过。补充精度保持/原生UI检查4 passed、0 failed、2600ms，证据adjustments-precision-ui.txt。

修复后真实集合realR7Edits重新单独执行，12张全部逐张应用新参数/裁剪校直，RAW WB源重算、Undo/Redo及30次快速切图检查通过；3 passed、0 failed、237645ms。证据evidence/adjustments-r7-verified.txt。实际lumina.exe Windows原生启动/正常退出smoke（无输入）退出0，见adjustments-startup.json；真实图片读取由原生QtTest同一Window类另行验证，不把无输入smoke当解码证据。最终12原片SHA均不变，见adjustments-source-safety.json。

真实4X1A9579.CR3连续拖动10个参数：旧五项每约2秒27–30帧，新五项各13帧，校直40次Qt绘制；共206个后台反馈帧，64–186ms、均值104.28ms（混合各参数，不是新算子单独均值）。RAW精算重叠拖动另21帧/约2秒。独立case通过，原始日志adjustments-r7-final.txt保留同一运行中真实集合case的精度失败；后续精度修复后默认合成10项拖动回归通过。计数包含Qt绘制，非DWM屏幕光子时延、p95或30/60FPS认证；本机系统负载未控制。

最终原生UI截图在build/private-qa/s2/adjustments-ui-final.png及-light.png、-browse.png；检查1366×768逻辑窗口的深/浅/选片布局，真实host当前缩放。参数文字不重叠，侧栏可滚动，选片画布扩大。尚未做独立150/200%跨屏与真实触控板/数位板人工验收，不把QWidget grab当完整屏幕输入/色准证明。

## 完整源计算

9579.CR3与同名JPEG，原6960×4640，recipe包含EV−.5、contrast .1、highlights−.3、shadows .3、whites .1、blacks−.1、vibrance .3、saturation .9、warmth .15、tint−.1、crop [.1,.1,.8,.8]、angle2°。实际工程render_probe两次退出0：

| 输入 | 源读取 | 4K渲染3840×2560 | 完整渲染5568×3712 | 含PNG总时间 | 峰值私有提交 |
|---|---|---|---|---|---|
| CR3 | 6931ms | 1304ms | 2728ms | 22440ms | 892.89MiB |
| JPEG | 5851ms | 1415ms | 3021ms | 20263ms | 893.01MiB |

两源SHA未变；PNG/JSON/recipe留本地被忽略的build/private-qa/s2/adjustments-full-raw、adjustments-full-jpeg和adjustments-recipe.json。证据evidence/adjustments-full-results.json与相应日志。部分测试并行运行，耗时不作受控性能对比；工程PNG仍不是正式导出UI/事务。

## 失败修复与剩余范围

第一次真实集合回归在第七张recipe保持检查失败。检查发现原editControls会重读所有数值框，连带把其他参数舍入到UI小数精度；已改为只更新触发变化的一项，对小于1e-12的数值解释差异不生成编辑。新增回归确保修改阴影保持曝光及高精度对比度、撤销返回原配方。原失败日志保留，真实集合重跑已通过。

初次深色截图发现Windows原生控件未完整继承外壳颜色，已明确Qt stylesheet各控件文字/背景并再次检查。可折叠组由箭头按钮控制，避免复选框被误解为开关效果。不同主题和工作区有实际状态，不展示尚未实现的局部/修饰/AI/导出占位按钮。

仍未实现：纹理/清晰度/去雾、曲线、HSL/精确取色、颜色分级、黑白混色、细节/降噪/颗粒、光学/透视/校准、局部/修饰、永久保存/同步/正式导出。自动跟随系统主题、更多图标/快捷操作、直方图交互式取样/全源统计也未完成。正式颜色质量/肤色参考、RAW高光边界、真实45MP RAW、目标8GB机、完整DPI/输入设备与发行许可仍待验证；第一批不等于交集全部补齐。
