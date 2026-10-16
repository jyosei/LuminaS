# 裁切空白边缘：显式约束操作

2026-10-16。接续 S3-C6-NATIVE-RESULTS.md 中用户观察到的旋转越界缺口。本批新增产品操作，保留旧配方和既有透明边界渲染语义；不是自动校直策略或内容填充。

## 行为与数值边界

构图卡片新增「去除空白边缘」。操作按当前裁框中心与比例等比收紧，保留角度、翻转、透视、畸变、红蓝色差、全部颜色参数及局部层，只把最终裁框作为普通历史命令写入现有配方。重复执行已满足约束的裁框不新增历史，撤销/重做走统一历史。比较原图、裁切选择、编辑数据未就绪时禁用；失败显示原因并保留当前状态。原片不写入。

`src/render/crop_constraint.cpp` 由GPT实现：

- 旋转矩形先用源边坐标的轴对齐包围范围表示；翻转不改变整框足迹。
- 对逆透视分母、径向映射和三个通道的取样范围作保守区间传播，沿用生产的 `1+k*t/(1+t)` 畸变及红/蓝原图像素尺度。运算端点向外扩一浮点步；整个输出足迹限制在源像素中心范围内，并保留1e-6原图像素余量，避免双线性取样跨出源。
- 未校正的零角度沿用既有边缘复制语义，不无故裁掉中性原图。已满足包围范围约束的裁框保持原值。
- 对以原中心等比缩放的嵌套矩形进行48步二分，选择保守可行尺寸。原中心本身已无有效源、收紧后不足一个输出像素、无效参数/尺寸均失败，输出参数不改。
- 复杂组合可能比几何上最大可用矩形多裁一些，界面提示明确说明；不声称全局最大面积优化。算法不读取预览像素，不把4K缓存作为完整源，也不改显示ICC或文件格式。保证范围针对当前支持的全不透明JPEG/CR3；不宣称可去除任意源内容中的透明孔洞。

没有新增Recipe字段、改写旧角度/裁切坐标语义或悄悄缩放渲染器。局部蒙版继续固定在原图坐标，裁框变化由既有映射应用。用户仍可保留透明边界或手动裁切；默认自动约束、旋转过程中自动放大画面、裁切视图的所见即所得改进均不在本批完成声明中。

## 实际验证

- 新增注册第62项CTest `crop_constraint`。核心构建exit0，`s3-crop-constraint-core-build.txt`；专项6 passed含init/cleanup，118ms，`s3-crop-constraint-core-ctest.txt`/`build/dev/test-logs/crop_constraint.txt`。
- 288次实际渲染涵盖横/竖/方图、JPEG/合成RAW基底、预览与完整像素尺寸、四种翻转、±角度、±极限畸变、双轴极限透视、红蓝±10像素组合，全部最小alpha=1。另验证中性/已安全不改变、纯校直紧致解析尺寸、无效/无解失败不改输出及幂等。
- 生产窗口和应用目标构建exit0，`s3-crop-constraint-native-build.txt`；补按钮禁用与键盘检查后重建exit0，`s3-crop-constraint-keyboard-build.txt`。
- 合成JPEG原生窗口：`constrainCropKeepsLocalLayersAndUndo`，3 passed，888ms，exit0，`s3-crop-constraint-synthetic-native.txt/.json`。先重现旧组合缺口，再在实际构图卡片键盘Space触发新按钮；验证局部层不变、仅裁框变化、无缺口、幂等不加历史、撤销/重做像素SHA一致、原图比较期间禁用。布局合成截图检查后删除92913字节，未将截图保留为长期证据。
- 实际R7 JPEG：同一原生用例及原图解码后的两次完整源重放3 passed，28310ms，父进程exit0，`s3-crop-constraint-jpeg-native.txt/.json`。预览3840×2651，裁框约(.148187,.130622,.703626,.728756)，完整源裁后4897×3381；两次float SHA `844414766468e6e5bb194a8eed561f8bebde922b66ffd445185768d70189e254`，预览和两次全源最小alpha=1。
- 实际R7 CR3：同一用例3 passed，32575ms，父进程exit0，`s3-crop-constraint-cr3-native.txt/.json`。裁框/尺寸相同，两次完整源float SHA `7575d3be26afe05e5abd0e86e015f08a1d9169c467a01b47e68d04e2ee737cf0`，预览与两次全源最小alpha=1。原生窗口都先断言旧裁框确实存在alpha<.01区域，再验证约束后不透明，不能以本来安全的输入假冒修复。
- `./.tools/s3-build-targets.ps1 -Targets all`：全部目标重建exit0，`s3-crop-constraint-full-build.txt`。随后 `ctest --preset dev --output-on-failure`：**62/62，374.63秒，exit0**，`s3-crop-constraint-full-ctest.txt`；最新核心6 passed（141ms），蒙版原生7 passed（3772ms）。历史60/61色差反馈及atomic_file偶发失败不因本次通过而被改写为已定位修复。
- 实际生产入口 `LuminaS.exe --smoke-test --smoke-report ... 4X1A9579.JPG`：exit0、3521ms、Qt6.8.3/windows，照片提交成功且nativePhotoPaints=1，`s3-crop-constraint-app-smoke-report.json`；父进程退出日志`s3-crop-constraint-app-smoke.json`。没有用旧桌面包代替本次构建。

原生窗口先释放再做完整源重放，不能据此认证GUI与全源任务同时存活的资源峰值；完整源接口仍不等于已接入产品局部导出队列。真实45MP RAW及8GB设备未验，D资源/RAW隔离/恢复等继续待完成。

## 委派与审查

DSH新会话 `aba88f54-7a3c-48c4-971c-043c5113a7da` 只承担窗口按钮/提示/回调接线，GPT保留核心与测试并审查实际diff、构建和运行。会话到30次工具调用界限被取消，未自动续跑，worker没有完成约定检查；上述构建和检查均由GPT实际执行。执行审计发现worker越过选定文本范围，在src/app及src做了额外文本grep，未采用其权限范围完全遵守的声明；实际写入只有约定的window.cpp/.h，没有图片/照片/系统目录读取。后续不续用此会话。实际provider用量input19100、cacheRead286976、output3692，总309768 tokens，不据此推算人民币余额。

沿用现有黑灰紫组件和焦点状态，使用ui-design-system技能复核；没有引入新主题、打包、推送或切换生产GPU。
