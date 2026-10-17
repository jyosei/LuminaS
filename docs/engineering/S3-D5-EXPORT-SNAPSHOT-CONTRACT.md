# D5 局部编辑全源导出桥接合同

2026-10-17，GPT架构决定；模型/队列/渲染/预览接线现已写入，专项和实照验收仍进行，不据此关闭D5。复用现有渲染算法与独立导出模块，不新增项目格式、ICC规则或GPU生产分支。

## 冻结与持久化

在`Export::Asset`和`Job`末尾新增可空`LocalEdit::DocumentSnapshot document`，保留现有aggregate初始化前缀。null保留Recipe-only行为；窗口对有局部层（包括禁用层）的照片提供完整只读文档，没局部层仍可走旧路径。`endControlEdit()`已调用`finishLocalEdits()`，导出打开和开始前沿用该提交边界；当前照片用currentPhotoDocument，其他用history.snapshot，每个文档用自己的revision。

新增`bool validateJobDocument(const Job&,QString& error)`：null返回true；非null必须`validDocument(...,true)`、sourcePath绝对且clean后与job.source等价（Windows忽略大小写，不读磁盘）、sourceHash等价、global==recipe、revision==job.revision、raw标志与CR3后缀一致。normalizedSize/算法/图层ID/点数沿用核心校验，不重写。

freeze在原有canonical源/输出命名检查后，验证并复制文档对象为新的const PhotoDocument；QVector/QImage外的配方/几何值按现有COW语义隔离，不能只拷贝原shared_ptr。Pending和Skipped都保留文档。同一路径大小写清理可规范化到job源路径，但不偷偷将不匹配的源哈希/配方/版本改成“匹配”。

导出Job格式升为2，Settings/预设格式仍1，项目PhotoDocument格式仍1，队列外层仍1。Job v2必须有document:null或合法文档对象；缺失/非法/未知算法拒绝。v1全局旧任务继续支持；v1若携带非null document必须拒绝，不能默默丢局部层。jobJson编码文档失败时不能写null来伪装成功（空对象等非法值必须使解码/入队失败）。jobFromJson失败不修改输出。

Queue.append目前验证JSON往返后仍存入原jobs；改为存入已往返验证的checked副本，保证外部仍持有可变对象时入队也隔离。保留16MiB/2000条边界及原子保存/回滚/显式重试；超限清晰失败，不降级或丢图层。任务恢复包含完整文档，重启不重建半张。

## 渲染与预览

execute先校验冻结文档，仍从原始JPEG/CR3读取完整浮点源。非null文档调用`LocalEdit::renderDocumentFullLinear`，null沿用renderLinear。沿用源SHA/RAW WB/尺寸/scale==1门禁；后续缩放、目标ICC/CMYK、量化、metadata筛选、编码、实文件校验、取消、无覆盖提交保持。绝不用4K或设备RGB导出。

导出转换预览对非null文档用`renderDocumentPreviewLinear(fullSource,document,document.global,3840,...)`，仍是代理计算，不先量化sRGB；target转换沿用独立模块。缓存等价性必须包含局部文档内容/版本，不能只看global Recipe；旧在途结果仍受sourceGeneration/path/index/recipe等门禁。后续修图只能影响新任务/新预览，不影响已入队快照。

## 责任与验收

- DeepSeek可实现模型/编解码/队列隔离与独立测试，限定文件；GPT接真实全源/预览/窗口接口并终审。
- 类型层：v1旧任务/v2/null/局部文档往返、未知算法/畸形/错hash/path/recipe/revision/raw拒绝、失败保留输出、冻结后修改原可变文档不影响job、append后修改原job文档不影响队列、保存恢复仍冻结。
- 数值/文件：非空局部层的JPEG/CR3全源导出与已有核心完整渲染+转换参考比较；禁用/顺序/opacity/feather/invert/梯度/brush至少代表组合，原图SHA不变，单张/批量/入队后编辑及重启一致。保留真实45MP RAW缺样本限制，合成边界不伪装实照。
- 窗口：导出预览局部变化确实失效，目标ICC转换不混显示器ICC；成片按真实输出检查，非叠加回原编辑链。
- 最后完整回归/原生验收按新增后实际数量，记录实际退出码、失败与未验，之后才S3冻结/上传/授权迁移包。
