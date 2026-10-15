# S3 A2 数据与历史接口决策

2026-10-15，GPT 架构决策。A2 尚未实施；本合同不代表笔刷或保存已经可用。实现入口为 `src/core/photo_document.h/.cpp`（计划新增），保留现有 Recipe 与预设语义。

## 记录类型

- 命名空间 `LocalEdit`；`DocumentSnapshot = std::shared_ptr<const PhotoDocument>`。持久记录禁止包含 QImage、GPU 句柄或整图 mask；Qt 值容器用隐式共享，后台只持 const 快照。
- `AssetIdentity`：稳定 `id`、`sourcePath`、`sourceHash`、方向归一 `normalizedSize`、`raw`、`sourceFacts`。facts 原样保留 RAW/ICC/归一方向、矩阵与 WB 等事实，不依据屏幕图重新猜测。
- `LocalParameters` 仅九个 double：exposure/contrast/highlights/shadows/whites/blacks/warmth/tint（默认 0），saturation（默认 1）。范围沿全局对应字段，不能携带局部曲线或 RAW WB。
- `MaskOperation` 为 Add/Subtract。`BrushGeometry`：源坐标 `QVector<QPointF> points`、源像素 radius、hardness。`LinearGeometry`：source start/end。`RadialGeometry`：source center、radiusX/radiusY、angleDegrees。
- `MaskComponent`：稳定 id、operation、strength（0–1）、上述三种 geometry 的 `std::variant`。每笔冻结这些字段；组成项顺序有意义。
- `Mask`：有序 components、feather（源像素，默认 0）、invert（默认 false）、version（默认 1）。
- `AdjustmentLayer`：稳定 id、name、enabled（默认 true）、opacity（默认 1）、parameters、mask、version（默认 1）。全图基础不属于该列表。
- `PhotoDocument`：asset、global Recipe、有序 layers（从下到上）、revision（默认 1）、localIdentity（默认 0）、algorithmVersion（当前 1）。后续蒙版/局部核心必须遵循 B 的数值合同；改变解释时必须明确升级算法版本。

## 验证与序列化

- `validDocument(document,error,requireSourceIdentity=false)`：编辑态允许尚未解析的空 hash/未知尺寸；非空 hash 必须为 64 位十六进制，尺寸只能为完整未知或正尺寸且不超过 45MP。持久化模式要求 hash/尺寸完整。
- 编辑态也要求非空 sourcePath 和稳定 asset id。图层/组成项 id 非空且在文档内不重复；复制必须由命令生成新 id，读回禁止重新生成。
- 所有数值必须有限。局部参数沿全局范围。strength/hardness/opacity 为 0–1；半径为正；线性端点不重合；羽化非负；坐标与半径绝对值上限 1e8、羽化上限 16384 仅作格式安全边界，不宣称这样的大值已通过性能验收。
- v1 最多 64 层、每层 4096 组成项、每笔 262144 点、每文档总计 1048576 点，sourceFacts 编码最多 256KiB。应用 UI 还需按实际资源限制，不以格式容量作为性能承诺。
- `documentJson` / `documentFromJson` 使用明确 format `LuminaS.photo-document`、formatVersion=1、algorithmVersion=1。revision/localIdentity/各 version 用十进制字符串编码，避免超过 JSON double 精确整数范围。
- 解析先构造临时对象，整份有效才替换输出。未知格式、算法、geometry kind、枚举或非白名单字段拒绝，不默默丢节点；sourceFacts 内部字段除外，需完整保留。读取失败不允许随后覆盖原项目。
- 全局字段调用现有 recipeJson/recipeFromJson/validRecipe；禁止自行重写曲线/HSL/几何解析。规范全局键来自现有 recipeJson；旧 Recipe 缺省字段继续可读，非法或未知全局字段拒绝。
- `documentFromLegacyRecipe(asset,recipeObject,error)` 显式把旧 Recipe-only 数据装入空局部文档；asset 由调用方提供，不凭空产生已解析源事实。旧调整预设仍使用原模块，不自动带入局部层。

## EditHistory 兼容扩展

- 保留 `commit(path,Recipe,name)`、value/undo/redo/nextUndo/nextRedo。value 返回当前快照的 global；没有状态仍返回默认 Recipe。
- 新增 `snapshot(path)`、`commitSnapshot(path,DocumentSnapshot,name)` 和原子 `resetDocuments(...)`。reset 校验全部文档后才清旧历史，用于项目重开；失败不改变原状态。
- 当前值以文档快照为唯一真相，不能并列维护互相独立的 Recipe 与 locals 两套状态。首次旧 commit 可创建源身份尚未完整的编辑态文档；asset id 在同一路径后续历史中保持稳定。
- EditCommand 可保留 before/after Recipe 字段供原调用兼容，并追加 beforeDocument/afterDocument 小快照引用。跨图仍只有一条有序时间线；新提交丢弃 redo 分支，非法/无内容变化不提交。
- 每个路径的 revision 单调递增，Undo/Redo 不恢复旧 revision。locals 改变时 localIdentity 同样前进；恢复旧几何/组成项的稳定 id 与顺序，不重新生成。
- 全局旧 API 仅修改 global，必须保留已有 locals、asset 与算法身份。同一张照片的局部命令不得污染另一张照片。
- 层/蒙版 version 由相应局部命令维护，用于 C5 的独立 mask/参数缓存；总 document revision 不能代替所有细分缓存身份。

## A2 出门检查

覆盖旧 Recipe commit/Undo/Redo、跨图顺序、无局部基线、局部与全局交替、历史快照不可变、复制记录独立、revision 单调、稳定 id、序列化精确往返、旧 Recipe 缺省、非法/未知/截断输入失败不改输出。这里不实现蒙版栅格化、局部算子、存储/UI、导出设置或模型推理。
