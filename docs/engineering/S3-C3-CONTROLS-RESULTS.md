# C3组成项控件与画布接口（主窗口尚未接入）

后续状态：主窗口接入和C3专项已完成，见[S3-C3-NATIVE-RESULTS.md](S3-C3-NATIVE-RESULTS.md)。以下保留控件独立实现时的历史状态。

2026-10-16。本批继续实现C3原生合同，不勾选C3完成。

- 新增独立 `LocalGradientControls`：线性/径向工具、按持久顺序显示的画笔/线性/径向 Add/Sub 组成项、移除入口。每项使用真实ID，程序刷新不发编辑请求，跨层丢失选择回到占位项；无可用层时禁用。只保存展示状态，不拥有像素、几何算法或历史。
- `RemoveComponent` 值操作在指定层内查找真实ID，删除后保留其他项及其顺序，推进mask和layer版本；层/组成项不存在或版本溢出明确失败。统一历史测试比较删除前后实际线性像素，撤销/重做恢复对应结果，删除最后一项保留空层。
- Canvas新增渐变模式与UV控制点接口。固定DIP绘制/命中，支持照片边界外但widget内的控制点，跳过无效投影；仍复用原有指针信号、Space/Esc/缩放结束行为。控制点重绘不改变照片或蒙版上屏来源计数。源空间映射继续由host承担。
- 主窗口还没有挂接这个组件或GradientTransaction，也未完成渐变引导线、真实R7原生交互及整体布局验收。核心参考重放记录见 `S3-C3-CORE-RESULTS.md`，不能冒充本批主窗口验收。

## 协作

DeepSeek Flash仅接收photo_document和局部面板两个文本头文件，返回组件草稿。实际用量输入3161、输出2918 tokens，stop终止。GPT复核后改用QString按值信号（草稿声明实际为const引用）、clicked事件和更小展示状态，补充重复类型序号、可访问名称与宿主重入信号测试。没有向worker发送照片、缩略图、截图、凭据或整仓库；worker没有执行本地验证。

## 验证记录

- `s3-c3-controls-build.txt`：目标实际构建exit0。
- `s3-c3-controls-check.txt`：渐变核心、渐变事务、组成项组件、层操作专项4/4，1.49秒。组件4 passed含init/cleanup；层操作新增删除/撤销像素验证通过。
- `s3-c3-controls-final-build.txt` 首次失败：误用输出文件名 `LuminaS` 为CMake目标，ninja明确报unknown target；未归为代码编译失败。更正为 `lumina` 后，`s3-c3-controls-accepted-build.txt` 实际生产目标及组件/画布测试构建exit0。
- `s3-c3-controls-accepted-check.txt` 最后专项5/5（2.55秒），Canvas 11 passed、组件4 passed、层操作7 passed（均含init/cleanup）。覆盖固定DIP命中、图外handle、无照片帧增量、渐变模式切换结束手势，以及切换照片清除旧handle。旧画笔输入回归一起通过。

当前注册56项CTest；没有完整56/56认证。最近全量49/50中的atomic_file偶发拒绝访问、C5端到端性能缺口和D阶段任务继续保留。本批无截图、完整渲染图片、桌面包、ZIP或快捷方式。
