# V30 手动畸变校正（DISTORTION-V30-RESULTS）

状态：目标 active，S2 未整体完成。本报告记录冻结合同下的实现结果与 GPT 已执行检查的输入证据；不含 worker 自证。

## 1. 冻结合同与实现

- 手动有界各向同性、中心锚定的径向映射，`k ∈ [-0.4,0.4]`，缺省 0；零值直接旁路返回输入点。
- 回源半径比例 `1 + k·t/(1+t)`，`t = |P-C|²/R²`，`R²` 取半对角线平方；该比例为坐标缩放，不是亮度增益。
- 径向导数在有限域内恒 ≥ 0.55（比例最大值 9/8 于 t=3），全域单调、不折返。
- 链序：输出翻转/裁剪/角度 → 逆透视 → 径向回源 → 原图邻居 lens shade → alpha 加权双线性 → WB/空间/全局影调。
- `outputToSource` 与渲染器共用同一映射公式，未来笔刷/仿制沿用该链，不以 4K 显示像素代替源坐标。
- 当前没有镜头标定配置文件；抗混叠、局部Jacobian支持度量和摄影重采样质量仍待验证。

## 2. 状态、缓存与预设

- 装载器工作结果/last/proxy 均携带 distortion 身份；含 WB/源守卫；完整源不可变，仅计算目标 4K。
- 准备态清理可防止重复扭曲；中性 float 缓存键含 0；普通 LRU 不持有完整源。
- 调色预设保留当前几何（含畸变），构图复位清零。
- 原生 UI：一个 3 位小数 spin/slider/复位卡，标题「畸变校正」，保留既有镜头卡，导航纳入两者，共享历史/对比守卫与其它精度。
- Tooltip 使用平实照片编辑语言。

## 3. 实际验证（GPT 执行）

- final build 退出码 0；startup smoke 退出码 0。
- 单元 7 项通过，114ms：默认/严格 JSON/边界/非有限；预设序列化排除；预设强度几何保留；中心/角点/奇偶/各向同性/单调扩展半径；独立解析平面全几何；alpha/HDR/源不变/零值精确；混合 lens+texture+USM+grain/缓存变更刷新一致/线性显示 ≤1 code。
- 原生合成 3 项通过，31515ms：2sec/40 输入；3 preview/4 paint；最终 distortion .14，exact exposure .312345 保留；lens .1、crop .1/.1/.8/.8；其它精度、单手势 undo/redo/reset-only/compare/4K 门。
- 真实 RAW 3 项通过 43693ms（3/3 反馈）；JPEG 3 项通过 39571ms（2/3 反馈）；最终均为 .14 配方。
- 私有照片 QA 仅 GPT 本地进行，从未交给 worker。

## 4. 全源渲染探针（3 例全新，退出码 0，sourceUnchanged=true）

- RAW：源 6960x4640，preview 3840x2560(835ms)，cropped full 5568x3712(1866ms)，peak commit 893.0390625MiB。
- JPEG：源 4640x6960，preview 2560x3840(727ms)，full 3712x5568(1396ms)，peak commit 893.23828125MiB。
- 合成 45MP 9000x5000：preview 3840x2133(614ms)，full 7200x4000(1885ms)，peak commit 1242.95703125MiB。
- 三例均使用 .14 加 lens .1/contrast .2468/saturation 1.1357/RGB 曲线，cropped full 为有意设置。
- 边界：工程探针非正式导出 UI，不构成 8GB/全算子/真实 45MP RAW 认证。
- 12 张原片 SHA 校验，0 变化。完整CTest 11/11通过，327.56秒，退出0。30/60FPS仅额外成就，p95未认证。
- 实际日志：`build/private-qa/distortion-v30-domain.txt`、`distortion-v30-native.txt`、`distortion-v30-raw-native.txt`、`distortion-v30-jpeg-native.txt`、`distortion-v30-ctest-final.txt`，均在同一private-qa目录；GPT已检查产生的文本证据，文稿worker未读取。

## 5. 委派边界实际情况

- 会话 7fd52327-ffdf-4315-a425-19c6d1314b4b：达 max_tokens 停止，50 次调用，input 63743/output 19185/cache 1613440/total 1696368；参数/缓存/UI 部分未完成，误替换镜头卡，由 GPT 本地备份还原。
- 会话 6436cc74-e396-4fc1-8666-2c08daf93e32：80 次工具调用被取消，input 57549/output 19386/cache 2994304/total 3071239；期望错误（中心/反射/双线性/缓存相等），数值测试由 GPT 替换并实际运行检查。
- 会话 579af11e-ceac-48f9-9320-4cceb07aa35f：18 次调用完成，input 15282/output 6975/cache 264448/total 286705，产物未构建；GPT 修复游离块括号/曝光基线/实际条目后编译并运行。
- 未恢复任何失败会话，无自动 provider 重试。审计的工具参数未见读取照片/凭据的模式，但不构成绝对隐私保证。
- 计数不代表 RMB 余额；产品内无 Developer key、无在线 worker。

## 6. 变更文件（分类）

- 核心/渲染：畸变映射与 `outputToSource` 共用链、采样顺序、零值旁路。
- 缓存/装载：distortion 身份贯穿 ready/proxy/last/中性缓存，准备态清理与 4K 目标路径。
- 窗口/UI：畸变卡、导航、共享历史/对比守卫、tooltip 文案。
- 测试：数值与几何单元测试、原生合成 UI 测试、RAW/JPEG 原生用例。
- 构建：CMake 目标与测试注册。

## 7. 遗留

- S2：手动 CA、显示 ICC、摄影质量、抗混叠、局部 Jacobian、目标资源、UI 设备 QA。
- 镜头标定、物理 Kelvin、配置文件质量仍未接受。
- S3 仅计划：tile ROI 需携带全源尺寸/偏移，按完整非线性链计算采样范围与 halo，并处理取消/缓存且不重复扭曲。
- 色差类别的后续参考与数值/摄影验收边界见CHROMATIC-ABERRATION-NEXT.md，尚非生产功能。

文稿DSH会话dd388d02-6182-413b-95c5-921f0dda06ed正常完成：10次调用，input8588/output3878/cacheRead85760/total98226；GPT审查术语、SHA表述、日志路径并填入最终CTest结论。计数非余额。
