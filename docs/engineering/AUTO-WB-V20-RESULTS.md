# AUTO-WB V20 结果报告

范围：S2 自动白平衡实现与本机验证。DeepSeek 完成 UI/文档草稿，GPT 负责算法、复核修正与实际构建测试。

## 实现要点

- 在当前已裁剪/已旋转的图像上执行有界 trimmed gray-world；输入为 pre-global linear Rec2020 RGBA32F。
- 采样上限 128x128，对称采样；接受条件：alpha >= 0.1、RGB 均为正且 <= 1、Y 在 .01.. .8 区间。
- 联合统计 20%..80% 分位的 log R/G、log B/G，并按 alpha 加权；保留样本数须 >= 32。
- RAW 路径按 cameraWB 与 M 的逆矩阵近似还原到 as-shot 状态；无法恢复传感器/16 位显影截断，亦不能撤销白平衡影响的 AHD 插值变化。
- JPEG 在同一几何和工作帧上重复估算结果严格相同，因此重复点击不产生新渲染或历史记录；首次有效修改仅改变 warmth/tint，其余字段保持不变，支持一次 Undo/Redo。
- 共享的精确预览就绪（preview readiness）守卫会阻止 compare/crop/draft/stale 操作。
- UI 自动白平衡按钮成功后退出吸管取样；提示主体色彩可能影响结果，错误路径不产生历史记录变更。

## 构建与冒烟

- GPT 侧 MSVC 19.44 + Qt 6.8.3 构建：exit 0，启动冒烟 exit 0。
- 原生 model/UI 测试 4 项通过，耗时 1521 ms。
- 首次原生尝试失败：临时目录无效（指向 system TEMP）；改用项目内 TEMP 后重跑通过，日志已保留。

## 真实样张测试

- 真实 RAW 9579：3 项通过，44350 ms；估算耗时 9 ms；warmth 0.0190582，tint 0.0342949。
- 真实 JPEG 0705：3 项通过，17995 ms；估算耗时 4 ms；warmth -0.310905，tint -0.168131。
- 真实测试用例参数：crop .1 .1 .8 .8、angle 2、horizontalflip、exposure .312345、vignette -.4；其余配方设置保留；Undo/Redo 成功。
- 12 张原片 SHA256 与测试清单一致：0 项不匹配。

- 最终默认 CTest 3/3 通过：S2 148.64 秒、S1 0.27 秒、S0 3.52 秒，总计 152.45 秒。

## 未完成与限制

- 本批次未做新的完整源渲染 probe；复用既有 source WB 路径。
- 无 physicalKelvin、色卡、校准、商业质量认证，亦无 RAW 重复性保证。
- DSH UI 会话 0dcc30b0-eb73-4126-9905-d7ba57f64068 实际只读写 window.h/cpp；缺失的头文件声明由 GPT 修复，draft 测试中臆造的 API 已被更正。
- 审计：5 个 provider 步骤，input 11392、output 2516、cacheRead 39296、total 53204；5 次工具调用（grep 2 / pwsh 3），未涉及私密照片或密钥。
- 30/60 FPS 仅为额外成就，非硬性通过门槛。
- S2 剩余项仍包括精确分色取色/范围、颗粒、预设强度、光学校正等；S3 仅做准备工作。

文档执行会话 ee7ac1b5-2a3d-4e54-b7a5-2db653d7f7c2：实际一次 pwsh 只写/读本报告，日志核验未涉及图片或凭据；2 个 provider 步骤，input 2809/output 1018/cacheRead 8320/total 12147。GPT 修正文稿误解的 SHA 零差异、clipping 含义及测试状态后集成；token 统计不代表余额。
