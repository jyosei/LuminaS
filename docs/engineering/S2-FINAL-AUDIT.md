# S2 最终验收审计（软件范围已满足）

2026-10-14。此页重新按实际交接文件核对，最终证据支持S2软件范围已满足，外部验收与后续阶段不计作已通过。最新范围是用户要求“持续工作到S2全部完成后停止”，不继续S3。旧S2-CLOSEOUT-DRAFT中不存在的01-DECISIONS.md不构成缺失输入；权威文件为01-REQUIREMENTS.md。

## 原始与追加范围

02-PLAN的S2要求CPU float32、ICC输入/显示/输出合同、基本参数、裁剪旋转、代理与全尺寸计算；通过条件为不双gamma、几何一致、源不变、一拖一撤销、文字焦点不误撤销、JPEG完整尺寸不放大代理。01-REQUIREMENTS U11–U18扩大了4K下限、导入预解析、持续反馈、摄影调参交集及原生UI范围。摄影调参的第二/第三批是实施顺序，不能据此将颗粒、预设、光学等从S2扩展范围删除。

03-CONTRACTS同时描述后续存储、同步、局部修饰、RAW隔离与发行；按02-PLAN/04-ACCEPTANCE阶段归属核对，不能将整个v0.1验收表混称S2。

## 软件义务与当前证据索引

| 要求 | 当前实现与需核实的证据 | 审计状态 |
|---|---|---|
| C++20/Qt Widgets、真实构建运行，无浏览器引擎 | CMake、src/app；ENVIRONMENT/BUILD、桌面V39清SDK启动/JPEG/CR3 | 已有实际证据；最终V39构建0 |
| 线性Rec.2020 D65直float32；JPEG/RAW语义与源不变 | document/source_reader/color_pipeline；s2.floatHeadroomAndInvalidValues/iccGammaAndRoundTrip；12样片SHA；LibRaw相机RGB16边界明确保留 | 最终27/27回归通过；不宣称无截断scene-linear RAW |
| ICC输入/显示/输出分离、不双gamma、灰阶/高饱和/负值/超白/alpha | s2 ICC/融合参考/分片故障；display_transform/presenter/queue；V36实际屏幕profile3144字节；V37横竖JPEG输出隔离4 passed | 数字工程证据已检查；最终27/27回归通过 |
| 原图方向/像素上限/原图坐标、裁剪旋转及完整源重算 | s2.allExifOrientationsAndMirrors/previewAndFullUseSameCoordinates/flipsAndSourceCoordinates；透视/畸变/色差专项；V37三条完整源probe均与V32像素SHA一致 | 有实际证据；45MP合成不是实际45MP CR3 |
| 长边3840、小图原尺寸、不放大低清代理 | readSource/previewSize/EditLoader；横竖R7集/全源probe；V37裁后4000宽→3840及小图蒙版测试 | 已有实际证据 |
| 持续拖杆反馈、单手势历史、跨图独立、文本/IME/Esc | s2.windowGesturesAndSwitching/textFocusImeAndEscape/共享历史/精度；各算子原生RESULTS；V37 R7蒙版11/11与12/13预览绘制 | 合成IME事件不是系统输入法实测；最终27/27回归通过 |
| 导入即全部后台预解析、有界缓存、点击直接显示 | EditLoader 512MiB显示/4GiB临时工作数据；s2.realImportPreparesAllBeforeClicks、缓存淘汰/故障、RAW精算重叠 | 真实12张专项3 passed；预解析44086ms、首绘36–52ms、源重建0 |
| 默认小手/滚轮、选片/编辑、原生视觉规范与DPI | Canvas/S0浏览测试；UI-V2、导航V33/数值框V34/启动V35；V37生成图原生截图 | 本机证据；跨设备/校准多屏另列 |
| 白平衡 | RAW通道平衡/JPEG暖冷与色调、原照复位、吸管、统计自动；WB-PICKER-V12/AUTO-WB-V20、s2相关测试 | 有实现与既有证据；物理Kelvin不伪换单位 |
| 基本影调与色彩强度 | 曝光/对比/高光/阴影/白/黑/鲜艳度/饱和度；ADJUSTMENTS-V1、ToneOperator、灰阶/范围与原生反馈 | 最终27/27回归通过 |
| 曲线 | RGB/R/G/B/亮度平滑PCHIP、参数四区域/三分界、独立复位/预设切线；COLOR-V2/LUMA-CURVE-V13 | 最终27/27回归通过；不是折线假控件 |
| 分色 | 八色HSL、精确取色最多8样本、范围/羽化/临时范围查看、历史/取消/比较；POINT-COLOR-V26 | 八样本合成及R7 JPEG/RAW均3 passed，真实拖动2预览/2绘制 |
| 分级与黑白 | 三范围着色/明度/混合/平衡；八色黑白混色/模式/混合比例；STYLE-V3/MONOCHROME-V22 | 最终27/27回归通过 |
| 质感与细节 | 纹理/清晰度/正负去雾、亮度USM/半径/软阈值/独立边缘系数、双通道降噪；SPATIAL/DEHAZE/SHARPEN/DENOISE；V37灰阶查看 | 最终27/27回归通过；源像素尺度/alpha/条带有数值对照 |
| 风格/预设/颗粒 | 内置/自定义预设、原子库/改名/删除/强度、裁后暗角三参数、颗粒三参数；PRESETS-V10/V18/V21/V23、VIGNETTE-V19/GRAIN-V24 | 最终27/27回归通过；预设保存不等于照片项目保存 |
| 光学/几何与原色 | 透视、手动畸变/源平面暗角/红蓝横向色差/紫绿边；RGB原色色相/饱和度；V27–V32/PRIMARY-V11 | 工程证据；自动镜头资料库/相机标定未实施 |
| 临时视图不污染Recipe/预设/缓存/输出 | PointColor范围与V37锐化蒙版；新隔离/原生历史/缓存测试；normal RenderView默认空 | V37专项通过，最终27/27回归通过 |

## 必须分开列明的外部或后续验收

- 04-ACCEPTANCE A14目标8GB核显/24–45MP资源为S5；A04按阶段、S5完整；U13明确实际近45MPRAW覆盖不阻塞S2。当前开发机与合成上限证据不能替代该实机认证。
- A06为S2/S5：当前承担软件数值与ICC合同；固定光源/曝光/参考数据的物理色卡ΔE是04中的建议量化目标，校准屏/HDR/跨屏真实设备仍未验。不得宣布整体颜色/RAW质量已认证。
- A08为S2–S4：当前拖杆、文本焦点/负数输入/IME事件/Esc；真实系统IME人工实测仍未覆盖。笔划失焦/组命令分别随S3/S4实现，不能提前声称通过。
- U09及01-D明确相机/镜头/固件专属优化、镜头资料库、机内风格匹配延后；物理Kelvin需要真实相机映射，不以EV改标签冒充。手动镜头暗角中点属于可改进参数，不是原S2明确门槛；裁后风格暗角中点已实现，两者不能混称。
- A07的笔刷/仿制、局部命名/渐变/范围蒙版与去瑕疵属S3；S2不显示占位按钮。
- S4负责保存/恢复/组同步/导出事务及JPEG/TIFF输出检查UI；S2仅证明源完整计算与输出ICC合同。当前照片会话不能持久化、没有正式导出UI，桌面README明确。
- 干净跨机离线部署S5/S7、发行许可/5位用户流程S7；V36本机便携包不是正式发布认证。

## 决策复核

DeepSeek直接API草稿仅作要求整理：本页纠正其将第二/第三批摄影功能排除S2、将文档历史“待实施”当当前状态及若干来源归属错误；不把该草稿当完成判定。原始回答保存.tools/s2-v37-matrix-worker-draft.json，实际输入9368/output3024、无cachehit；未发送私照或完整聊天。

最终构建0、27/27 CTest通过309.48秒（load-v39-final-ctest.txt）。12张真实导入与八样本真实组合已补齐，7次完整源probe全部exit0；三条基线preview/full像素SHA与V32完全一致，组合输出长边3840/完整裁后尺寸正确且源不变。R7彩色组合峰值1688.7MiB、黑白RAW1688.6MiB、合成45MP2327.3MiB；组合4K计算约5秒，不能宣称任意复杂组合均实时。V39桌面包清SDK烟测与33项ZIP核对通过。范围及证据逐项复核后，判定S2软件范围已满足，在此停止，不实现S3。物理/跨机/系统IME及后续功能仍按上表保留。
