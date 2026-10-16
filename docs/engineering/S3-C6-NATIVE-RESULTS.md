# C6 原生组合与旋转边界

后续产品操作与完整回归：见 [S3-CROP-CONSTRAINT-RESULTS.md](S3-CROP-CONSTRAINT-RESULTS.md)。现已新增显式「去除空白边缘」、真实R7预览/完整源无缺口验证，最新完整62/62通过。下述“本批未修改生产”描述的是此前C6测试批次；默认自动约束策略仍未实现，历史偶发问题仍未定位。

2026-10-16。本批只新增测试与文档，没有修改生产裁切、旋转或颜色算法。C6/S3 尚不整体勾选完成。

## 用户观察到的缺口

用户确认缺口出现在自动弹出的组合测试窗口。原测试在 R7 的 6960×4640 源上设置 crop=(.08,.06,.84,.87)、angle=7°，还叠加透视、畸变和色差。单看旋转、尚未叠加光学校正，四个边界角的源 Y 为 649.69、4656.40、-62.80、3943.91；中间两项越过源的上下边界。现有渲染合同对无源像素区域输出透明度，不填充虚构像素，所以会露出画布背景。这不是原文件内容被删除，也不是只发生在一条渲染路径的随机漏算。

此前组合测试只证明串行/缓存路径相等，没有证明正常成片组合的完整覆盖，这是验收遗漏。现在将可见正常组合改为 crop=(.2,.22,.6,.56)，保留全部其他全局/局部效果，并要求 prepared 工作帧所有像素 alpha>.9999。越界旋转另立独立数值检查，不能仅更换测试参数便声称产品交互问题已经修复。

**产品限制仍保留：**校直/裁切没有自动约束到有效源区域；交互旋转也沿用固定矩形裁切。后续若提供自动约束或改变坐标语义，必须单独处理预览、完整源、蒙版坐标、历史与已保存配方兼容。这里不把自动约束宣称为已实现。

## 新增检查

- `local_combinations_tests::rotatedCropMatchesIndependentSourceSupport`：600×400 全不透明合成浮点图；角度 -45/-7/0/7/45、完整/安全裁切、四种翻转、完整/200长边，共80组。参考手工展开旋转和双线性取样，对整个输出逐像素核对 alpha 和有效 RGB，不调用生产坐标映射作为参考。32组完整画幅的非零旋转确有无源像素；安全±7°及零角度无缺口。最大 alpha 误差1.19209e-7、RGB误差9.53674e-8，安全组最小alpha=1。
- `local_mask_window_tests::globalCombinationsKeepLocalHistoryAndPreparedPixels`：原生窗口创建径向局部曝光、反相和羽化；叠加四通道曲线/亮度曲线/参数曲线/HSL/原色/分级/点颜色、几何光学、空间效果与暗角颗粒；彩色及部分黑白均与不使用缓存的 prepared 重算精确相同。撤销/重做恢复对应像素哈希并保留局部层；冻结文档在后续修改与窗口析构后仍可重放。
- 可选 `LUMINA_C6_FULL_REPLAY=1` 从原始源重新解码，要求匹配源 SHA 和完整归一尺寸，再两次完整源局部重算、比较浮点输出 SHA。原生窗口/缓存先释放，因此该步骤不认证 GUI 与完整计算同时存活的资源峰值。它不是已经接通产品导出队列的证明。

## 实际构建与检查

- 最终边界目标构建：`./.tools/s3-build-targets.ps1 -Targets local_combinations_tests,local_mask_window_tests`，exit0，`build/private-qa/s3-c6-boundary-build.txt`。随后只修正蒙版比较的行尾非像素字节，重建窗口目标exit0，`s3-c6-native-build3.txt`。
- 专项 `ctest --preset dev -R '^(local_combinations|local_mask_window)$' --output-on-failure`：2/2，5.78秒，exit0，`s3-c6-boundary-final-ctest.txt`；Qt分别19/6 passed（含init/cleanup）。
- 初次真实 JPEG 原生调用崩溃在测试的 `QImage::operator==`：无调色板的 Indexed8 蒙版不能按颜色比较。改为核对尺寸/格式/有效索引。小图专项又暴露行尾对齐字节不稳定，进一步限定每行有效宽度；首次失败日志保留在`s3-c6-jpeg-native-first-failure.txt`、对应stderr以及`s3-c6-boundary-first-window.txt`，不把失败删除或改写为通过。测试临时目录生命周期也延长到完整源重放结束。
- 真实 R7 JPEG（4X1A9579.JPG）：最终安全组合原生及完整源重放3 passed，73404ms，父进程exit0。prepared 3840×2388、alpha最小1，彩色/部分黑白效果计算约7127/6384ms；完整源裁后4176×2598，两次float SHA `d2b6f5fac496cd37f5c5614be8a70c1e434489df42142e797f886be04050a0ce`。见`s3-c6-jpeg-safe-native.txt/.json`。这组同时启用大量空间算子的耗时不满足即时交互，不能用缓存路径像素一致冒称性能达标。
- 真实 R7 CR3（4X1A9579.CR3）：3 passed，77220ms，父进程exit0；相同预览尺寸，alpha最小1，彩色/部分黑白效果计算约7010/6273ms。完整源裁后4176×2598，两次float SHA `375cb1bdcb849e931aab051882edb25a151a78df141d589c74fdcc5986a57c19`。见`s3-c6-cr3-safe-native.txt/.json`。两种实际源均验证原文件SHA未变化；这里没有声称重新复查全部12张原片。

日志均在 `build/private-qa`；不保存截图或实际照片渲染文件。历史完整CTest仍为60/61、色差中间反馈失败独立复测通过但未定位；atomic_file历史偶发拒绝访问未定位。D恢复/RAW隔离/资源/局部导出桥及真实45MP RAW、8GB设备未验项继续保留。

DeepSeek仅接收选定文本，提供测试/审查草稿；GPT审查并执行。草稿中的虚构SourceFrame字段、M_PI依赖及把安全裁切错误要求为有透明像素等未采用。一次审查截断且多为无实质接口猜测，没有当作通过证据，也未自动重试。没有生产GPU切换、推送或打包。
