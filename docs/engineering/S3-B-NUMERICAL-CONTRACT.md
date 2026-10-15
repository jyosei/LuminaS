# S3 B 数值合同

2026-10-16，GPT 核心决策。B1 入口 `render/local_reference.h/.cpp`；这是参考计算，尚未接入画布，不宣称笔刷 UI 可用。

- 源坐标采用现有 outputToSource 的像素边界空间，原图像素 (i,j) 的中心是 (i+.5,j+.5)。半径单位为方向归一后的原图像素。
- BrushSampler 在按下时冻结 radius/hardness；沿输入折线按 radius/8 的弧长间距取样，起点保留一次，剩余距离跨事件延续。snapshot 另包含当前末端一次。静止事件不增加点；同一路径的不同事件密度允许源坐标绝对误差1e-8。无间距下限暗改；间距下溢、非有限输入或超过格式点数预算时明确失败，已有采样状态不变。
- 单笔覆盖取所有笔头的最大值，之后乘该笔 strength。距离 d>=radius 为0，d<=hardness*radius 为1；中间 t=(d-hardRadius)/(radius-hardRadius)，权重 (1-t)^2*(1+2t)。hardness=1 为半开硬圆盘 d<radius。重叠、重复点和停留不累积；不同笔仍按顺序累积。
- 初值 M=0；Add 使用 M+(1-M)*a，Subtract 使用 M*(1-a)，最终数值限制在[0,1]。此限制仅用于蒙版，不裁剪工作 RGB。
- 参考 rasterize 使用全局输出坐标的像素中心，经原始完整 recipe 的 outputToSource 回到绿色参考原图。即使颜色工作帧已准备并清除了几何，MaskMapping.geometry 仍必须持原始几何，sourceSize 仍为原图尺寸。不能把 tile 局部坐标当成全图坐标。
- 映射无效时覆盖为0。alpha 来自实际输出工作帧；alpha<=1e-8 的区域无覆盖，部分透明像素不再额外乘 alpha 以免重复衰减。NaN/Inf alpha 是错误，不提交部分结果。全局阶段已经定义的 alpha 行为不改。
- B1 仅接受笔刷、feather=0、invert=false；线性/径向、整体羽化/反相在 C 实施前明确报未支持，不能静默忽略已保存的节点。
- LocalPixelTransform 在 straight float32 linear Rec.2020 上工作。RGB 暖冷/色调增益分别为 2^warmth、2^tint、2^-warmth，包括 RAW；然后曝光 2^EV、对比以0.18为轴且倍率2^contrast，再调用现有 ToneOperator 的四区影调和饱和度。只构造九个局部字段，其余全局字段全部保持默认；不重新执行 RAW 源白平衡。
- 亮度沿既有 Rec.2020 权重0.2627/0.6780/0.0593。层复合 C=F+w*(A(F)-F)，w=coverage*opacity，alpha 原值不变。混合差值暂用 double 避免有限大值相减中间溢出，最终存 float32；不额外限制负值或超白。非法输入/非有限输出时像素原值不变并报失败。
- 默认参数、零覆盖、零强度及透明像素跳过变换；默认参数必须逐位不变。一般 RGB 对照容差 abs<=1e-5+1e-4*abs(reference)，alpha 严格不变。蒙版解析标量样例容差1e-6；相同坐标下参考分块与全幅必须逐位相同。

B2 固定使用256逻辑tile，并以512及全幅参考对照。B1包含逐点参考、矩形重放和局部像素算子；B2现已加入空间索引、有限线程预览与独立线性图层合成。持久缓存、实时画布及overlay/native计时尚未实现，不以参考循环冒称4K实时性能。局部九项算子核心已复用同一入口，C1仍需与实际图层/完整源组合验收。实际证据与限制见 S3-B-CORE-RESULTS.md。

## B2 草稿接口与验证口径

- `PreparedBrushMask` 为每个笔触建立平衡二维最近邻点索引；径向笔头覆盖单调，因此最大笔头权重等于最近点权重。层内组成项顺序及 float 合成公式不改，头部函数与参考共用一个实现。
- 单点查询及 tile 输出必须与 BrushMaskReference 同坐标逐位相同。hardness=0/中间值/1、重复点、Add/Subtract/零强度和 1x1 残块均覆盖；特别不能将“搜索初值=半径”误判为已找到 hardness=1 的内部点。
- `OutputSourceMapping` 缓存三角与镜头常量，outputToSource 也使用同一实现；保留原表达式乘法顺序，旧几何 RGB 管线的相对顺序不改。
- 每 tile 精确扫描实际映射点形成源包围范围，按此跳过不相交笔触；不使用仅四个角估计非线性回源范围。临时坐标、有效位和 mask 仅限该 tile，默认256，最大512；不分配完整源坐标图。
- 此阶段的覆盖与像素变换都是点运算，halo=0；不以此宣称 C4 整体羽化已完成。持久 LRU、ready/inflight/pinned 预算与精确失效属于后续 C5。
- 性能探针使用程序生成的4K float32基础帧，不读取照片，不编码图像。只报告 mask 重放、索引字节与标量对照，不把它称为导入、原生事件或 RGB 效果延迟。是否达到性能目标以实际数字记录，不设置30/60FPS硬断言。
