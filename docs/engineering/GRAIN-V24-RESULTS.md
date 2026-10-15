# 颗粒 V24 工程报告

## 1. 定位与算法
V24 颗粒为**艺术化、确定性**的灰度乘性 value noise，作用于 tone/postcrop 暗角之后、SDR 输出之前。它是可复现的工程噪声，**不是**物理胶片、传感器或厂商型号模型，不构成色度学或物理胶片画质结论。

参数：
- `size`：原始像素单位，范围 1..12，默认 3；
- `amount`：0..1，默认 0；
- `roughness`：0..1，默认 0.5。

RGB 三通道使用同一增益，因此保持 alpha、HDR 与负值语义；黑色仍为黑。实现由两个 smoothstep 格点构成，并做**精确 box footprint 积分**。

## 2. 缓存与路径
缓存为 2 张 Gray16 平面，每轴上限 3840，单对平面载荷上限 **56.25 MiB**；快照可共存。`amount`/`rough` 变化可复用缓存，`size` 变化触发重建。

对完整 >4K 输入走**直接计算**，不分配全尺寸 grain noise 平面。X/Y 的 source/proxy 比例各自独立推导，来源于既有已验证 originalSize。缓存格式 **LM4KF003 不变**。

## 3. 构建与测试事实
- 构建/启动：0；
- 12 样本 SHA：0 差异；
- Native 6 项通过，16939 ms（含 setup/cleanup）；首次 prep 354 ms；
- 约 2 秒尺寸拖动：8 次预览、8 次绘制；`amount` 12/12；`rough` 14/14；
- footprint 3x box gain 误差 < 2e-5；融合 LCMS ≤ 1 code；
- recipe/preset 字段、alpha、history、gates 已测。

以上为工程快照数据，非受控基准；worker 可能同时存在后台活动。未认证 30/60 FPS 额外行为，未认证 p95。

## 4. 探针结果（3 次，exit 0，sourceUnchanged true）
| 输入 | 4K预览 | 峰值私有提交 | full | preview |
|---|---|---|---|---|
| RAW 6960x4640 | 3840x2560 | 744.816 MiB | 1281 ms | 1659 ms |
| JPEG 4640x6960 | 2560x3840 | 744.727 MiB | 1167 ms | 1688 ms |
| 合成 45MP 9000x5000 | 3840x2133 | 1036.063 MiB | 1562 ms | 1352 ms |

探针 recipe 仅含 grain/exposure/vignette，无空间算子。

## 5. 待办与边界（明确 pending）
- 真实 RAW native 3 passed（17757ms），初次准备685ms；约2秒强度11/11、粗糙度11/12预览/绘制。JPEG native 3 passed（16231ms），初次准备566ms；强度11/11、粗糙度12/13。最终 CTest 3/3通过（200.11秒：S2 196.47、S1 .27、S0 3.36）。
- DSH 测试会话 `f522f42a-212b-4257-811e-249fc9d5c1d5` 曾写出无效测试并搜索允许路径外的 system TEMP；GPT 已停止相关进程、恢复备份，并实现经评审的测试。该 worker 未交付可接受结果，不能计作成功；42个provider步骤 input80759/output11164/cacheRead2473472/total2565395。审计发现越范围读取普通代码、搜索系统TEMP的文件元数据；未在工具参数发现读取照片字节的调用，但不能据此宣称绝对无任何隐私影响。无自动重试，未使用这份补丁。
- 无 8GB 设备认证、无真实 45MP RAW 认证、无色度学/物理胶片画质认证。
- S2 仍待 PointColor、手动光学、透视与画质；S3 仅准备阶段。

## 6. 结论

早期DSH字段/UI会话ddf507eb-42d4-46f6-a551-de24c5c7ed19在20调用限制被取消，未交付字段；14步骤input31148/output1631/cacheRead307200/total339979。该会话还越范围读取了普通presets.cpp；未观察到照片/凭据引用。GPT完成字段与UI，没有自动重试。后续补测会话另行停止并恢复备份；预算放宽不能替代范围检查。最终文稿Flash调用input629/output846/total1475，输出未截断，由GPT审查事实。
V24 为确定性灰度乘性颗粒，具备 4K 内缓存与 >4K 直算路径，内存与耗时数据如上。真实 native 已通过；最终 CTest 3/3通过（200.11秒：S2 196.47、S1 .27、S0 3.36），摄影画质与目标机认证仍待补齐。

文稿由DeepSeek Flash起草，GPT已修正数字标签、缓存载荷措辞、真实native和执行审计事实；token统计不代表余额。S3未来tile必须把完整输出坐标偏移及XY源像素比例传入同一噪声域，不能每tile重置噪声原点。



