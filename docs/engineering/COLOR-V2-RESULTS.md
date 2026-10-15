# 曲线与 HSL 第二批结果

2026-10-13。生产路径仍为 C++ / Qt Widgets / LibRaw / Little CMS；不是浏览器图像引擎。当前结果为本机工程验证，S2完整交集、UI改版及S3准备仍按 FIVE-HOUR-WORK-PLAN.md 推进。

## 实现与曲线纠正

新增 RGB 总曲线与 R/G/B 三条通道曲线、八色段 HSL（红、橙、黄、绿、青、蓝、紫、洋红）。各色段色相界面 ±30°、饱和度 ±1、明度 ±1EV；每图独立配方，旧JSON缺省身份曲线/零HSL，新字段损坏明确拒绝。曲线点击加点、拖动、Delete删内部点、每通道复位；一次拖动一条撤销命令。

用户指出初版是折线后，已改为共享 PCHIP 三次 Hermite 插值。界面用同一导数转换成三次 Bezier，像素算子也使用同一插值；控制点之间 C1 连续，单调点列无过冲。仅有两个身份端点或共线点时自然仍为直线。区间外沿端段割线外推，保留工作域负值/超白，不因曲线显示框截断。方法参考[官方 PCHIP 数学说明](https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.PchipInterpolator.html)，实现无 SciPy 依赖。

核心代码：src/core/curve_spline.h、document.h/.cpp、src/render/color_operator.h、tone_operator.h、cpu_renderer.cpp；交互 src/app/curve_editor.h/.cpp、window.h/.cpp；CMake与 tests/s2_tests.cpp 接入。顺序与数值合同见 COLOR-CONTRACT.md。

## 实际证据

| 检查 | 结果 | 证据 |
|---|---|---|
| 最新增量构建 | 退出0，无编译警告 | evidence/color-v2-build.log |
| 最终默认CTest | 退出0，3/3通过，52.09秒 | evidence/color-v2-ctest-final.log |
| 实际lumina.exe无输入启动/退出smoke | 退出0 | evidence/color-v2-startup.log |
| 数学/配方与融合显示参考、连续4K反馈 | 6通过0失败，90011ms（含init/cleanup） | evidence/color-v2-optimized.txt |
| 12张真实R7曲线/HSL、4K曲线/13根参数、焦点窗口销毁 | 6通过0失败，227216ms | evidence/color-v2-real-final.txt |
| Windows原生曲线/HSL手势、焦点窗口销毁最终回归 | 4通过0失败，1508ms | evidence/color-v2-native-final.txt |
| 源文件安全 | 12份源文件SHA-256均与原manifest一致 | evidence/color-v2-source-hashes.json |
| 同一配方完整源与4K预览，CR3与JPEG | 两次退出0；完整5568×3712，预览3840×2560，源6960×4640；源hash不变 | evidence/color-v2-full-raw.log、color-v2-full-jpeg.log、color-v2-full-results.json |

最新Windows QPA QWidget截图保留在 build/private-qa/s2/color-v2-smooth-final.png 与同名前缀-hsl.png，已查看曲线确实弯曲；截图不是屏幕延迟或目标机认证。

完整源probe使用曝光0.1、四点总曲线、橙/蓝HSL、0.1/0.1/0.8/0.8裁框、2°校直。RAW源读取6895ms、预览1740ms、完整渲染3556ms、峰值私有提交892.93MiB；JPEG源读取5733ms、预览1682ms、完整3434ms、峰值893.02MiB。包含诊断PNG写盘的总耗时不能当交互帧率；缓存状态未控制，非p95。

## 速度与失败修复

去掉未启用曲线的重复映射以及逐像素色相取模。对真实R7的每项2秒连续拖动，曲线8→10帧，HSL各6→8–9帧；优化后曝光/对比度/饱和度/暖冷/绿色25–28帧，分区影调/鲜艳度11–12帧。带非线性参数的工作量更大，各参数测试按顺序叠加，不是独立零配方基准。见 color-v2-curve-optimized.json、color-v2-live-optimized.json；目前仍不是30/60FPS。

初次真实配方断言失败后，窗口焦点销毁触发编辑回调读取已析构的历史成员，导致崩溃。已在派生Window析构时断开子对象回调、清理窗口MetaCall，并加入聚焦编辑窗口直接删除回归。首次配方差异没有足够trace，不能宣称已定位其独立根因；后续测试增加配方诊断并等待目标照片/当前配方完成，严格真实集通过。最初失败日志 color-v2-real.txt 保留。

## 限制与后续

HSL为本产品线性Rec.2020 RGB色相/色差算子，带近中性衰减；不声称商业工具感知HSL等价或人脸保护。曲线采用带符号sRGB式感知刻度，不是传感器高光恢复。非线性编辑在4K工作帧上计算，完整计算从源重放同一配方；不能要求完整结果缩小与4K结果逐像素相同。

还需推进主流工具参数交集与生产UI、S2逐条审计及S3组织/实施计划。未通过事项包括真实45MP RAW/目标8GB机/每屏ICC与HDR/正式色准/广泛摄影质量；正式保存与导出仍在后续阶段。没有以补齐曲线/HSL宣称全部S2或持续开发目标已完成。
