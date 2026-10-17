# S3-D2 EditLoader 接线与首绘核验

2026-10-17。DeepSeek按给定事实起草，GPT逐项核对、修正并补入实测终态。

## 状态

本批为生产 EditLoader 的前置加载路径验证，**S3 尚未完成**。本批未输出截图或独立成片，不打包、不推送。S3完成后按用户授权冻结功能、上传并制作迁移包，再停止等待换设备。

## 已确认的实现事实

- 当前生产 EditLoader 的 RAW 预加载与普通非交互精确读取走同包 worker Prepare。
- 4K 长边为 3840，小图不放大。
- 需要透视/畸变/通道配准/镜头阴影补偿(lensShading)或全源交互支持时，DecodeFull 保留完整 RGBA32F；JPEG 走原路径。
- preparedGeometry 保持旧分层：此处不烘焙翻转（翻转留后续配方），不烘焙曝光/曲线/局部。
- 保留代际/取消/原图 hash。
- 生产 lumina 构建依赖 worker，QtTest 目标也依赖 worker。
- 旧 JpegLoader RAW 备用 API 尚未退休；Window 已不调用该 RAW 分支，但**不能宣称所有 RAW 解析已隔离**。

## 构建证据

- 所有目标构建：`build/private-qa/s3-d2-edit-loader-full-build.txt`，exit 0。
- 随后 S2 基准修正目标：`build/private-qa/s3-d2-import-provenance-build.txt`，exit 0。

## 新增测试与结果（忠实记录）

- `raw_worker_loader_tests` 新增实际 R7 用例：两种路径与原核 4K 显示像素完全一致；非空局部笔刷的图像与去层结果不同；Full 分支完整浮点源 SHA 相同。
- 全套 49.1363378 秒，exit 0，3 passed，记录 `s3-d2-r7-production-loader.{txt,json}`。
- 两分支后端总耗时 9567 / 20168 ms。该值**含 WB/裁剪旋转翻转/局部及传输**，不是纯解码耗时，因此**不宣称提速**。
- normal 视图不返回 mask overlay（其 SHA 为空串 SHA），**不能称非空 maskOverlay 像素 SHA 通过**。
- 真实 R7 原生项目保存/重开 3 passed，32.7585833 秒，exit 0；预览 SHA 与 D1 相同为 `634b6f4ae7050f4f316147840f3c5205980ba4c41066e96d412263b59af4b62a`，源 SHA 不变；记录 `s3-d2-r7-project-native.{txt,json}`。
- 原生 12 张 R7 JPEG/CR3 首次预解析 76414 ms，拖动约 2 秒 19 次渲染回执（**不是实际屏幕 fps**），整套 84.1295513 秒，exit 0，3 passed；记录 `s3-d2-r7-import-native.{txt,json}` 及 `s3-d2-r7-import-numbers.json`。

## 性能限制说明

- 耗时数据为后端总耗时，包含处理与传输链路，不可作为解码性能或提速依据。
- 渲染回执次数不等于实际屏幕帧率。
- **完整73/73通过，404.66秒，exit0**；日志`build/private-qa/s3-d2-edit-loader-full-ctest.txt`。默认私有样片用例skip，实照由上列独立命令覆盖，不能把默认CTest说成含所有实照。

## 首绘基准修正

旧基准 framePainted 未按图片 key 核验，旧首绘值不能当作新照片的严格首绘。已改用 `canonicalFramePainted(key)`，只接受目标 cache 或目标 ready 的 key；循环从第二张起、最后回第一张，保证每次真实切换。严格原生实测3 passed、exit0、86.2676642秒，记录`s3-d2-r7-import-provenance-native.{txt,json}`及`s3-d2-r7-import-provenance-numbers.json`。12次均按目标canonical key核验，且每次真的切换照片，sourceRebuilt全部false；新照片实际首绘54–690ms，中位179.5ms。全部12张首次预解析78045ms；拖动约2秒收到22次渲染回执，不当作屏幕fps。本用例展开全部调整分区，属于当前设备的具体负载，不能推广为任意设备或日常界面的保证。此前未过滤key的42–62ms仅保留为历史数据，不能再当作已验证的新照片首绘。

## 待办

1. 公共 readSource；导出设置转换预览/全源导出仍存在父端 RAW 直接读；`export_metadata.cpp` 中 LibRaw.open_file 仍在父端。
2. 退休旧 JpegLoader RAW 备用 API。
3. 生产关闭/切图更多组合验证。
4. RAW元数据输入隔离后，保持并复验既有元数据筛选、GPS默认关闭和方向归一化策略，不重新定义产品政策。
5. 资源/磁盘/100 轮测试及含局部层全源出口。
6. 首批解析仍约78秒，带编辑的两条后端读取仍约9.6/20.2秒；传输、hash与缓存开销需要分阶段测量和优化，不能因数值一致或测试通过称速度已解决。真实45MP RAW及8GB设备继续保留未验。

另：DeepSeek 的流式 SHA 草稿仅忽略目录、尚未审阅采用，无性能结论。以上仅列事实，未声明全部问题已修复。


先前约40MB旧超时测试残留仍因自动审批拒绝保留，本批会话清理没有访问那些目录。测试私有帧与缓存正常析构，仅留文本/hash证据。
