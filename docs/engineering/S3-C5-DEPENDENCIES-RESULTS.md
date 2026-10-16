# C5缓存依赖复核

2026-10-16。本批完成缓存依赖专项修正，后续进入C6组合回归；整个C/S3仍未完成。性能目标仍未达到，不把缓存正确性通过当作交互速度通过。

## 实际缺陷与修复

`baseKey`原来保存有效X/Y采样尺度，却未保存`sourcePixelsPerPixel`。当SourceFrame同时有显式轴向尺度时，修改旧版整体尺度不会改变有效X/Y；CPU空间滤波仍读取整体尺度，因此缓存会复用错误的基础帧。

新增`legacySpatialScaleInvalidatesBaseWithExplicitAxes`：固定X/Y=1、全局锐化与局部曝光，先缓存，然后将整体尺度1→4。独立重算像素确实改变，修复前缓存结果与独立结果不相等，实际exit1，日志`build/private-qa/s3-c5-scale-before.txt`。修复在基础帧key补入整体尺度，保留当前滤波数值算法；覆盖场不受此滤波尺度影响，因此仍可复用。修复后断言像素精确一致且只新增一个基础帧build。

## 依赖矩阵

新增源依赖矩阵逐行创建独立缓存；先填充，再只修改指定属性，逐一比较缓存结果和无缓存线性重算，最后再验证暖调用不重建。当前13×9合成fixture含负值、HDR和混合alpha，无照片或磁盘图片。

| 修改项 | 基础帧 | 覆盖场 | 检查 |
|---|---|---|---|
| draftBalance | 重建 | 复用 | 独立结果确实变化、缓存精确一致 |
| RAW解释标记及asset.raw（已有warmth） | 重建 | 随文档来源重建 | 同上 |
| RAW相机矩阵（已有色差校正） | 重建 | 复用 | 同上；alpha采样位置不依赖通道矩阵 |
| cameraWB / rawWarmth / rawTint | 重建 | 复用 | 来源保守失效，当前算法不要求只改这些元数据就改变像素 |
| 源alpha像素 | 重建 | 重建 | 结果变化、精确一致 |
| 相同像素的新QImage缓冲 | 重建 | 重建 | 图像身份改变，像素保持一致 |
| 源hash和文档hash同时更新 | 重建 | 重建 | 来源身份改变，像素保持一致 |
| 有效X/Y支持（已有颗粒） | 重建 | 复用 | 结果变化、精确一致 |
| 显式X/Y下旧版整体支持（已有锐化） | 重建 | 复用 | 单独修复前失败/修复后通过 |

既有同次运行的渲染测试继续验证：局部参数/名字/层强度/显隐复用覆盖；蒙版版本、强度、补减、半径、羽化、反相重建覆盖；全局色彩重建基础帧而复用覆盖；几何变化重建；后置颗粒/暗角不重建前缀。缓存容器的ready/inflight/pinned、取消、清理generation及预算测试继续运行，loader旧结果提交隔离同时运行。未声称这些小图测试覆盖100轮资源趋势或完整S2组合。

## 本次执行

- `./.tools/s3-build-targets.ps1 -Targets local_renderer_tests,local_preview_cache_tests,local_loader_tests,lumina` exit0，`s3-c5-dependencies-build.txt`；增加矩阵后的测试目标重建exit0，`s3-c5-matrix-build.txt`。生产目标已包含key修复。
- 原生Qt定向两个新slot：4 passed含init/cleanup，53ms，`s3-c5-dependencies-detail.txt`。
- `ctest --preset dev -R '^(local_renderer|local_preview_cache|local_loader)$' --output-on-failure`：3/3，1.09秒，`s3-c5-dependencies-check.txt`。各Qt套件分别12/24/5 passed含init/cleanup。
- 更新后的`LuminaS.exe --smoke-test`原生窗口启动、绘制和正常退出exit0，`s3-c5-dependencies-startup.json`；没有以启动冒称真实照片重验。
- 无新增截图/渲染图或包，无推送。没有重复本轮之前的R7/全源检查，也未把旧结果冒称本轮新证据。

DeepSeek首次测试草稿请求因选择行范围无效被工具拒绝，未自动重试。另一个独立的矩阵草稿调用input2712/output1430 tokens；草稿错误使用longEdge=0、遗漏实际局部层和先填充缓存，且RAW矩阵用例没有正确设置基线。GPT未直接采用，按实际接口重写、运行并检查结果。worker仅收到选定文本，无照片访问。

后续C6补齐全局曲线/分色/原色/黑白与几何、光学、空间效果组合；D恢复/RAW隔离/资源/局部导出桥仍待实现。完整60项尚未重新构建测试，atomic_file偶发替换拒绝访问与33/100ms性能缺口仍保留。C5缓存依赖专项已有本次证据，脏瓦片、复制和独立覆盖交付仍作为性能优化方向保留。
