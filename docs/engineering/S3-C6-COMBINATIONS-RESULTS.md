# C6组合回归进展

2026-10-16。新增组合数值专项通过；完整回归60/61，不能宣布C6或S3全部完成。

## 新增组合专项

独立`local_combinations_tests`注册为第61项CTest。37×29合成线性RGBA32F工作帧包含负值、HDR、0/.4/1 alpha；JPEG解释及RAW解释各8组，共16行。RAW是合成工作帧，不是实际CR3解码证据。

- 四条master/RGB曲线、亮度曲线、参数曲线、HSL、原色、分级、点颜色。
- 全量及部分黑白。
- 裁剪、旋转、双翻转、透视、畸变、色差、镜头暗角校正。
- 纹理、清晰度、去雾、锐化/锐化蒙版、亮度/色彩降噪、去边。
- 几何与后置暗角/颗粒，以及所有上述类别的组合。

每组包含两层有序局部处理：径向Add+笔刷Subtract、源空间羽化、非满强度及九项局部参数；第二层反相线性渐变。测试独立串行使用既有全局renderLinear、标量蒙版参考及局部变换，再单次应用后置效果，对照生产分块结果。RGB容差2e-6+2e-6×参考绝对值，alpha逐位一致；全源与不缩放预览/缓存结果精确一致，最终sRGB与独立转换一致。要求局部处理确实改变像素、存在部分覆盖、后续编辑不改变冻结文档、交换层顺序与串行结果一致、源像素不变；暖渲染、局部参数修改和换序均不重建基础帧/覆盖缓存。

该参考用于验证S2与局部阶段的组合和顺序，不独立重新证明全部S2算法，不涵盖真实4K下prepared源的所有组合、实际R7组合或D进程树资源。

## 实际执行

- 新目标首次构建exit0：`s3-c6-combinations-build.txt`；专项18 passed（含init/cleanup），1515ms。
- `./.tools/s3-build-targets.ps1 -Targets all`全部目标构建exit0：`s3-c6-full-build.txt`，先重新链接当前代码再做全量。
- `ctest --preset dev --output-on-failure`：**60/61，362.19秒，exit8**，`s3-c6-full-ctest.txt`。失败项chromatic_aberration_window：2秒拖动中只有1个中间预览/绘制，要求至少2个；worker记录1431ms。全量日志保留失败状态；Qt详细日志被后续复测覆盖，已将此前工具实际读取的失败片段记入`s3-c6-ca-failure-excerpt.txt`，该文件是摘录，不冒称原始完整日志。没有放宽断言或把低帧率目标改成硬30/60FPS。
- 添加两条缓存build计数断言后重建组合目标exit0（与GPU实验一起的`gpu-local-round2-build.txt`）；最终`ctest --preset dev -R '^(local_combinations|chromatic_aberration_window)$' --output-on-failure`：2/2，22.47秒，`s3-c6-final-focused.txt`。组合18 passed，1485ms；色差窗口3 passed，20276ms。独立复测通过不等于首次反馈失败已定位或修复，不能将全量改写为61/61。

DeepSeek提供测试草稿及初步审查。首稿截断，包含非void函数使用QVERIFY、无效颜色范围、float与sRGB错误比较、遗漏JPEG/缓存等问题；GPT重写为实际接口、明确串行参考顺序、固定颗粒源尺度并执行检查。审查建议中的共享参考实现、仅不同曝光的换序对照等未采用；补入实际有意义的缓存build计数断言。worker只接收选定文本，没有照片访问。

后续补真实照片/原生组合和prepared路径。用户新增GPU常驻混合优化方向已作为隔离G3实验推进，不替换生产后端；见GPU-LOCAL-ROUND2-RESULTS.md。D恢复/RAW隔离/资源/局部导出桥、历史atomic_file偶发、交互33/100ms缺口均保留。没有截图、渲染图、包或推送。
