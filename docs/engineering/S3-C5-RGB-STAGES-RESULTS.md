# C5 RGB阶段测量与转换并行度

2026-10-16。C5继续进行；本批没有完成整个C/S3，没有打包或推送。

## 方法与实现

`local_mask_probe --overlay --stages` 在正常计时及像素检查之后，额外分别计时缓存线性结果、浮点帧深复制和独立sRGB转换。round0不额外剖面，避免预热冷缓存。独立转换必须与正常预览QImage精确相等。默认不带此选项的测量行为保持。

固定合成3840×2560 RGBA32F工作帧、6000×4000源坐标、2笔1000点，OMP4/PASSIVE。缓存线性处理已包含detach复制；独立copy是另外的带宽基准，不能将三项简单相加。额外剖面处于正常渲染之后的allocator/cache状态，不是同一次调用的内部时间线，也不是原生绘制或源解码计时。

初次4线程结果：缓存线性80.115/58.0036/57.1946ms，复制41.836/39.2678/41.3509ms，转换77.8341/83.5549/78.4036ms。因此只改变SDR转换的行分区与线程池上限4→6；矩阵、LUT、每像素运算顺序、alpha与取消检查均保持。整个转换共用有界线程池，未给每个图像另起6线程。适用范围是当前开发机，未认证低核数设备或多个并行请求的吞吐。

## 顺序对照

每个配置先构建完成再运行，无并行编译或性能探针。每次进程3个暖样本，不足以推导稳定p95。

| 配置/顺序 | 独立sRGB ms | 缓存整体RGB ms |
|---|---|---|
| 4线程首次 | 77.83 / 83.55 / 78.40 | 174.23 / 162.99 / 170.98 |
| 6线程候选 | 72.64 / 74.26 / 63.90 | 160.41 / 151.95 / 156.91 |
| 回切4线程 | 91.81 / 80.26 / 83.15 | 164.45 / 172.68 / 163.29 |
| 最终6线程 | 72.90 / 69.89 / 72.99 | 174.23 / 147.87 / 152.12 |

四轮RGB SHA及索引SHA完全相同，标量参考mismatches均0；保留6线程实现，但不将这组短测量当成普遍性能承诺。日志`build/private-qa/s3-c5-stage-{profile,six-profile,four-repeat-profile,six-final-profile}.json`，汇总`s3-c5-stage-comparison.json`。

## 实际检查

- `./.tools/s3-build-targets.ps1 -Targets local_mask_probe,local_renderer_tests,perspective_tests,point_color_tests,s2_tests,local_native_probe,lumina`：exit0，`s3-c5-stage-final-build.txt`。生产LuminaS.exe已更新。
- `ctest --preset dev -R '^(local_renderer|perspective|point_color)$' --output-on-failure`：3/3，1.78秒，`s3-c5-stage-check.txt`。
- 原生`s2_tests`定向6项：ICC/gamma、HDR/非法值、Little CMS参考、融合效果参考、翻转/隐藏alpha、几何/alpha，8 passed含init/cleanup，108ms，`s3-c5-stage-sdr-check.txt`。
- 当前R7 JPEG/CR3实际原生探针均exit0/ok，3840×2560、DPR1.25，源锚点误差0，前后源SHA不变。参数覆盖阶段9/9次复用已有蒙版，没有伪造新覆盖绘制样本。完整日志`s3-c5-stage-six-r7-{jpeg,raw}.{json,stdout.txt}`。

| 实际照片阶段 | JPEG照片绘制 n / median / p95 ms | CR3照片绘制 n / median / p95 ms |
|---|---|---|
| 新画笔+覆盖 | 4 / 438.28 / 575.83 | 4 / 515.60 / 683.57 |
| 调参+已有覆盖 | 9 / 269.90 / 335.23 | 9 / 260.01 / 500.42 |
| 调参仅照片 | 8 / 256.48 / 329.24 | 8 / 240.83 / 457.34 |

调参+已有覆盖的效果计算中位数167.71/169.91ms，覆盖缓存读取0.37/0.38ms。峰值working set815.75/853.88MiB、私有提交794.32/831.17MiB，仅本次单进程。首次导入到绘制3210/4741ms，探针前置源hash读取可能使OS文件缓存变暖，不能称磁盘冷启动速度。没有本轮完整源/45MP重算或进程树/100轮认证。

## 限制与下一步

转换并行度只解决部分耗时；33/100ms目标仍未通过。继续缓存依赖矩阵、帧复制、队列/取消、脏瓦片与覆盖独立交付；C6组合、D恢复/RAW隔离/资源/导出局部文档桥仍待完成。完整60项未重新构建测试，既有atomic_file偶发替换拒绝访问未解决。

DeepSeek仅参与阶段测量文档草稿（实际input2597/output582 tokens）；GPT检查实现、选择线程数并执行全部本地构建与验证。worker没有照片访问。本轮无截图或整幅图像输出，保留精简JSON/日志；先前清理记录单独更新。
