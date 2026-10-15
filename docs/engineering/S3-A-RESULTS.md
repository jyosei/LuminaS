# S3 A 切片结果

2026-10-15。A0 与 A1 已完成本机验收；A2/A3/B/C/D 未实施。GPU 首轮实验已按阶段停止规则独立收口（有限研究结论），生产渲染后端仍为 CPU。

## 基线与范围

- 基线 git `3e214bd`，分支 `feat/s3-local-editing`。此后仅有基线 CMake fixture 接线修复；A0 采集时无生产像素改动。
- 实验原始证据位于 `build/private-qa`，且被忽略。未向任何 worker 上传图像或截图。
- 本文件为新增文档；A1 后续结果由 GPT 追加。

## A0 实际证据

- 原始完整 CTest：`build/private-qa/s3-a0-ctest.txt`，exit 8，13/33 通过、20 失败，原因是 QTemporaryDir 无法在默认系统 TEMP 创建目录；`s3-a0-diagnostic-s0.txt` 证明 initTestCase 中 `temp.isValid` 为 false。
- 覆盖进程 TEMP/TMP 到工作区 `build/private-qa/v18-temp`（需预先存在）后完整重跑：`s3-a0-ctest-temp-fixed.txt`，exit 8，32/33 通过，331.14 秒；仅 `export_engine` 失败，原因是 `LCMS_SOURCE_DIR` 未继承，缺少 CMYK 测试 ICC。
- 该目标诊断：`s3-a0-export-diagnostic.txt`，实际 5 通过 / 3 失败 / 1 可选跳过（三例 CMYK）。
- CMake 中 `export_engine` 的 ENVIRONMENT 现为 `LCMS_SOURCE_DIR=${LCMS_SOURCE_DIR}`（来自已配置 SDK），无运行时键，无生产导出改动。
- 重新配置：`s3-a0-fixture-configure.txt`，exit 0；故意移除运行器的 LCMS 环境变量后执行 `ctest --preset dev -R '^export_engine$' --output-on-failure`，通过 1/1，1.81 秒，见 `s3-a0-cmyk-fixture-recheck.txt`。
- 正确口径：全部 33 个既有目标在本轮均有通过证据，来自一次完整运行中的 32 个通过目标及另一次 1 个目标专项；不声称 A0 单次完整运行 33/33。测试包含常规跳过的 opt-in 真实用例，真实探测另行记录如下。

## R7 源与原生窗口

- 源 A0 用既有 `render_probe` 对一张 JPG 及其匹配 CR3：输出 `build/private-qa/s3-a0-source/baseline-JPG.json` 与 `baseline-CR3.json`；预览 2560x3840，全尺寸 4640x6960。
- 实际进程记录：`full-JPG-process.json` exit 0，wall 16517 ms；`full-CR3-process.json` exit 0，wall 29838 ms（wall 含读取与 PNG 编码，不是纯交互延迟）。
- 原生 `LuminaS.exe --smoke-test --smoke-report` 单独输出 `native-JPG.json`、`native-CR3.json`：两窗口均为 Qt 6.8.3、`photoCommitted=true`、`nativePhotoPaints=1`、exit 0；`native-JPG-process.json` wall 3987 ms / 报告 elapsed 3797 ms，`native-CR3-process.json` wall 5350 ms / 报告 elapsed 5164 ms。
- 无局部完整源与预览像素哈希已冻结在既有报告中，用于 A1/A2 前后对比；不声称浮点全帧哈希，也不声称这些照片的颜色质量已认证。
- 原图校验：全部 12 张对照 `tests/fixtures/manifest.json`，结果未变，`original-source-checks.json` 12/12。

## A1 实现与实际验收

- EditTag 增加 localIdentity/viewEpoch；统一同代比较，保留同代交互旧 draft 的反馈，路径/视图/局部代次变化时拒绝旧结果。请求失效先关闭门禁，再取消实际在途任务。
- 任务独立持有共享取消 token。源哈希分块、LibRaw progress、ICC 与 CPU 行块轮询；默认空 token 保留原调用。取消不提前释放其他线程持有的不可变图像。
- 当前/live/预解析通道与 Window 提交处均校验；过期任务不能更新 lastBase、取色能力或编辑显示缓存。导入预解析保留，不随普通切图整组取消。
- 最新构建 `s3-a1-closeout-build.txt` exit 0。新增专项 `s3-a1-new-tests-final.txt` 2/2、4.51 秒，涵盖实际异步交付、ABA、失效后新请求、在途取消及无取消像素一致。
- 完整回归 `s3-a1-full-ctest.txt`：35/35、326.35 秒、exit 0。运行器特意移除 LCMS_SOURCE_DIR，并设置不可用 TEMP；CTest 从配置 SDK 注入 CMYK 测试 ICC，并对测试进程设置 `build/dev/test-temp`。这些是测试环境，不改变系统配置。
- 后续只增加真实 RAW 取消的可选检查：`s3-a1-raw-cancel-build.txt` exit 0；`s3-a1-raw-cancel-default.txt` 1/1；实际 `s3-a1-real-raw-cancel.txt` 为 3 passed/0 failed/0 skipped。R7 RAW 在途取消后无 SourceFrame、原片 SHA 不变。该次 285ms 包含结果轮询及后续原片哈希复核，不作为纯解码取消时延或各阶段认证。
- `s3-a1-source/pixel-comparison.json`：R7 JPEG/CR3 的 4K 与完整源共四个像素哈希均与 A0 完全一致。两个 full 探针 exit 0（wall 15795/28356ms，包含读取与 PNG 编码）；两个原生窗口 exit 0（wall 4110/5294ms），均真实提交照片并绘制。
- `s3-a1-source/original-source-checks.json`：12 张原图哈希不变。

## 限制与未覆盖

- 真实限制不变：45MP RAW 与 8GB 外部安全边界仍为真实约束；A0 的 R7 图像是 32MP，不是 45MP；不声称桌面安装包或 S3 UI 已可用。
- A1 的 GPT 核心与 DSH 接线/测试草稿已复核：DSH 首次触发 45 次调用上限并产出部分草稿，由 GPT 修正信号/方法声明、取消来源、视图版本和部分测试断言；相关后续只产出测试草稿。实际构建、测试与原生验证由 GPT 执行。
- 接线/测试会话 `dc5b594b-943c-42c4-9a73-74320fac9792` 审计 39 个 provider 步骤，输入 49742／输出 17711／缓存读取 1676416；越界读取了 canvas.h/.cpp、window_point_color.cpp 并列举源码目录，但未发现私人照片/截图或凭据读取。结果文档草稿 `bf9b790d-78bc-4669-897d-6adaf10816a1` 为 6 步，输入 4689／输出 2321／缓存读取 37376，读写保持指定文档范围。
- 已知限制保持：第三方 JPEG 与空间阶段仅有有界协作式检查；RAW 硬进程隔离仍属 D2。

阶段验收不代表整个 A 或 S3 完成；后续按 A2/A3→B→C→D 推进。完整源与原生检查均为本机证据，不能代替真实 45MP RAW、其他设备或 D2 故障矩阵。
