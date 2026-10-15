# 中性白平衡吸管

2026-10-13，LuminaS。白平衡卡片新增吸管，点击当前预览的中性区域，提交暖冷/色调EV两参数为一条历史。Esc、返回小手、切图、比较与新编辑请求退出取样模式。裁剪/校直已在工作帧完成；水平/垂直翻转逆映射取样UV，缩放与平移由Canvas逆映射。临时校直反馈未完成时拒绝取样。

取样来自不可变、全局调整前的RGBA32F线性Rec.2020工作帧，以中心3×3像素的alpha权重平均；不从屏幕8位RGB或已加曲线的图像估算。JPEG按红/蓝对称增益与绿色增益求中性；RAW反转cameraToWork矩阵，在相机域求目标中性相对增益，再叠加工作帧中已烘焙的rawWarmth/rawTint。RAW提交后沿已有源精算路径更新，草稿近似不作为最终结果。

JPEG公式w=−0.5log2(R/B)，t=log2(sqrt(RB)/G)。RAW令c=M⁻¹P、n=M⁻¹(1,1,1)，g=n/c，w=baseW+0.5log2(gR/gB)，t=baseT+log2(gG/sqrt(gRgB))。归一化共同亮度增益不影响这两个比值。矩阵退化、非有限、透明、非正/过暗或结果超出现有±1EV范围均失败并保留recipe。明确选择明亮、未过曝的中性材料；没有可靠的传感器剪裁识别、自动场景WB、物理Kelvin映射或色卡色准认证。

当前图路径、完整请求标签、工作帧crop/angle/RAW平衡和源字节/mtime必须匹配；准备中、旧请求、比较/裁剪或错误预览不可采样。该操作不写原片，不改变曝光、曲线、原色与构图。

已运行：初版构建退出0；原生数学/坐标/历史4 passed、367ms；真实R7 RAW9579与JPEG0705，裁剪(.1,.1,.8,.8)、角度2°、水平翻转后取样与最终渲染、Undo/Redo，3 passed、62633ms。日志wb-picker-v12-{build,native,real}。最终新增缩放中心、临时角度拒绝与返回小手检查，最终构建/完整回归结果随后追加。

最终像素边界改为先floor显示像素，再N−1−x/y翻转，新增偶数宽度精确镜像测试。返回小手/选片退出吸管，比较/裁剪下禁用按钮。wb-picker-v12-mode-build退出0，mode-native为5 passed、0失败、52550ms，包含全部新增数学、缩放、模式/历史与两种真实格式。此前final-pixel一次真实case未就绪拒绝而失败，日志保留；未捕获完整触发链，不将它算通过。就绪保护未放宽，增加了更具体的模式、最新帧、有效显示与浮点准备提示；后续组合运行通过，仍须继续观察该时序。

原生UI定向回归presetsWindowHistoryAndRoundTrip 3 passed（wb-picker-v12-ui.txt），其QWidget.grab截图build/private-qa/wb-picker-v12-ui.png已本地查看：白平衡卡片吸管按钮可见，既有stage/预设/光线卡片与侧栏滚动保持布局。最终LuminaS.exe --smoke-test正常退出0（final-startup日志可能为空，退出码由命令工具证实），不是DWM延迟或跨屏DPI认证。

最终模式/错误保护版本完整默认CTest3/3、0失败、129.38秒（S2 125.71秒），见wb-picker-v12-final-ctest.log。真实素材可选case的默认跳过不算通过，已由上述mode-native独立覆盖。最后检查无遗留LuminaS、s2_tests或render_probe进程。
