# 第三批：颜色分级与黑白混色

2026-10-13，当前产品已更名LuminaS，仅英文名。保持Qt/CPU float32/4K工作缓存与完整源重放，新增三范围颜色分级、八色黑白混色、配方类型检查和原生卡片。S2完整类别仍有质感/细节/光学等缺项，见S2-COMPLETION-AUDIT。

## 行为与模块

阴影/中间调/高光分别保留色相0–360°、着色强度0–1、明度±1EV，另有混合0–1、平衡±1。在工作域使用保持亮度的颜色方向与平滑亮度分区；单独着色保持Rec.2020 Y，明度另乘增益。黑白模式在转灰前根据八个颜色段选择明度±1EV，近中性渐弱；关闭模式保留配方并禁用混色控件。黑白之后仍可用通道曲线和颜色分级着色，因此不等同最终文件必须完全灰度。公式和顺序在COLOR-CONTRACT.md，不冒称商业算法等价。

新增render/style_operator.h，接入ToneOperator；core/document新增值字段与JSON/验证，Window新增两张真实卡片与单手势/跨图历史。存在但类型错误的旧数值字段也拒绝，不把字符串曝光静默变成0；缺失新字段继续取中性默认。模式开关是一条历史，色段/范围切换只改变面板视图。

范围参考[Adobe Lightroom桌面调色](https://helpx.adobe.com/lightroom/desktop/edit-photos/edit-photos.html)和[像素蛋糕颜色分级](https://wiki.pixcakeai.com/guide/14724.html)，工程算子为独立实现。原始商业帮助是功能/交互参考，不是其源代码或算法复刻。

## 实际证据

| 检查 | 实际结果 | 证据 |
|---|---|---|
| 增量构建 | 退出0，无编译警告 | evidence/style-v3-build-typed.log |
| 最终默认CTest | 3/3通过，73.15秒 | evidence/style-v3-ctest.log |
| 数学、配方、原生手势及LCMS融合对照 | 5通过0失败，738ms | evidence/style-v3-native-fixed.txt |
| 12张真实R7+4K连续拖动 | 4通过0失败，155989ms | evidence/style-v3-real.txt |
| 源安全 | 12份SHA均与manifest一致 | evidence/style-v3-source-hashes.json |
| RAW/JPEG完整源probe | 两次退出0，源不变；完整5568×3712、预览3840×2560，原6960×4640 | evidence/style-v3-full-results.json、style-v3-full-raw.log、style-v3-full-jpeg.log |

真实集每图分别使用一个范围的分级与红橙/蓝黑白混色（部分黑白、部分彩色），参数更新均sourceRebuilt=false：使用当前4K工作帧，没有逐次重解原图。色相/强度/明度/混合/平衡各2秒10帧，黑白混色2秒9帧，Qt绘制均持续反馈；不是30/60FPS，也不是DWM/p95或目标机认证。完整probe：RAW源读取6967ms、预览1530ms、全尺寸3000ms、峰值私有提交892.93MiB；JPEG5714/1397/2863ms、894.02MiB。包含PNG写盘的总时间22529/21774ms不作交互指标。

原生卡片截图在build/private-qa/s2/style-v3-ui-final.png及-bw.png，已检查。首版手势测试在Undo/Redo后未等编辑数据恢复就点击被禁用checkbox，断言失败；改为等待控件可用再操作，最终通过，未假改模式行为来满足测试。失败日志style-v3-native.txt保留。

## 后续

目前缺纹理/清晰度/去雾、精确取色、细节/传统降噪、光学与更多风格控制等共同类别；仍须质量、速度与目标机证据。S3准备文档已完成初稿：S3-IMPLEMENTATION-PLAN.md、S3-ARCHITECTURE.md，包含切片顺序、模块组织、源坐标笔迹/稀疏仿制、256/512实验、取消、worker协议/资源/故障检查和官方参考；尚未实现S3局部工具。继续开发目标保持活跃。
