# 去边色 V32 验证进度 — 2026-10-14

已接入 Recipe/JSON、渲染/缓存/预设及原生卡片。核心数值/架构由 GPT 实现；DSH 生成参数、UI 和窗口测试草稿，GPT 审查并实际运行。桌面便携包仍是已验收 V31，没有覆盖成验证中的 V32。

- 实际构建 exit 0：build/private-qa/defringe-v32-build-ui-fixed.txt。初次测试编译失败（初始化列表/Qt 宏逗号），已修正；不是工作者 UNBUILT 报告作为构建凭据。
- 核心组合 10 passed（241ms），合成 3840×2560 去色 111ms（10 passed 中的 fourKFrame），包含白平衡顺序、几何清零、空间/影调/临时范围视图、缓存源/WB/尺度失效及迟到提交、预设最短圆弧。
- JSON 7 passed（6ms），严格七字段和旧版本默认、拒绝/原子性。
- 原生合成窗口 3 passed（68398ms）：拖动约2秒有10次中间预览/10次绘制，已知紫边输出像素变化；最终强度 .49；精度、独立复位、Undo/Redo、比较门禁、几何复位保留去色、换图通过。无30/60fps硬门槛。
- 最初窗口检查失败：默认临时目录不能创建；设置工作区 TEMP/TMP 后发现测试 helper 没加 slider/reset 前缀，修正后通过。失败日志保留，未隐去。
- 真实窗口已实际通过：RAW 3 passed（103030ms，4次中间预览/4次绘制）；JPEG 3 passed（80686ms，6次中间预览/7次绘制）；两者最终强度 .49、loader 结果 22/24。证据：build/private-qa/defringe-v32-raw-native-final.txt、defringe-v32-jpeg-native-final.txt。
- 完整 CTest **16/16 passed，430.45秒**，实际输出 build/private-qa/defringe-v32-ctest.txt。摄影误去色/晕边与目标设备资源仍未验收。
- 构建为 exit 0（build/private-qa/defringe-v32-build-photo-fixed.txt，本任务不再读取内容）。
- 首次 RAW 窗口尝试失败：测试夹具断言 expected 6000×4000 vs normalized 4640×6960。GPT 判定为测试期望尺寸错误，已从既有夹具 manifest 环境独立取得期望尺寸修正；不是渲染器缺陷。失败尝试保留记录，未隐去。

证据：build/private-qa/defringe-v32-core-integrated.txt、defringe-v32-json.txt、defringe-v32-native-ui-fixed.txt（合成窗口）、defringe-v32-raw-native-final.txt、defringe-v32-jpeg-native-final.txt。

DSH 参数会话 06c0d7aa-b1bb-4ae3-8546-57574f6f0f3d：input21699/output7700/cacheRead429056/total458455。UI 会话 2e39e378-528b-45af-9527-379b4ce8fce6：76 calls，input70481/output24703/cacheRead3882240/total3977424。实际工具参数审计未命中私人图片/凭据引用，不是 OS 级路径保证；日志保存在 .tools/defringe-v32-*-audit.json。保留原有卡片和头文件声明的实际差异已审查；GPT 修复测试宏、查找辅助函数并新增已知色边及真实窗口输入入口。不由 worker 读取私人样片。

S2 尚未全部完成，本批不改变该结论；S3 仍仅准备；处理契约见 DEFRINGE-V32-CONTRACT.md。

## 完整源组合 probe

配方启用紫/绿去色 .65/.55、半径5，以及裁剪 .1/.1/.8/.8、畸变 .05、镜头暗角 .1EV、红/蓝横向色差 1.4/-.765432、曝光 .312345、饱和度1.1357及曲线。三次串行实际 exit 0，sourceUnchanged=true；12原片SHA全不变。

| 源 | 4K预览 / 耗时 | 裁后原尺寸 / 耗时 | 进程峰值工作集 |
| --- | --- | --- | --- |
| R7 RAW 6960×4640 | 3840×2560 / 1934ms | 5568×3712 / 4034ms | 1140.074MiB |
| R7 JPEG 4640×6960 | 2560×3840 / 1549ms | 3712×5568 / 3209ms | 1139.918MiB |
| 合成45MP 9000×5000 | 3840×2133 / 1239ms | 7200×4000 / 4134ms | 1505.953MiB |

证据 build/private-qa/defringe-v32-{raw,jpeg,45mp}-probe.json 和 defringe-v32-source-sha.json。该合成45MP是资源/尺寸验证，非真实相机画质凭据；单次计时非受控加速对照。这些诊断渲染不代表正式导出UI已实现，也不代表全部空间算子同时启用或8GB硬件认证。
