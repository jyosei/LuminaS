# LuminaS V19 暗角(Vignette)结果报告

日期: 2026-10-13 · 范围: S2 裁剪后暗角 · 性质: 风格处理,非镜头/相机校正

## 1. 参数与默认值

- 作用对象: 最终裁剪(final crop)之后的风格化暗角,**不是**镜头/相机校正。
- `amount`: EV 范围 [-2, 2],默认 0。
- `midpoint`: [0, 1],默认 0.5。
- `feather`: [0, 1],默认 0.5。
- Recipe JSON 严格校验: 可选字段 + 默认值;非数字/越界/非有限值一律拒绝。
- 支持预设(presets)与单项重置(individual reset)/历史(history)。
- 自有卡片(own card)采用 Light glyph。

## 2. 算法公式

对输出像素 (x, y),W/H 为输出宽高:

```
u = (x + 0.5) / W
v = (y + 0.5) / H
radius = sqrt( ((2u - 1)^2 + (2v - 1)^2) / 2 )
start  = 0.05 + 0.9 * midpoint
end    = start + (1 - start) * (0.1 + 0.9 * feather)
t      = clamp( (radius - start) / (end - start), 0, 1 )
RGB gain = 2 ^ ( amount * t * t * (3 - 2t) )
```

- 施加时机: ToneOperator（含RGB/HSL/颜色分级）**之后**。
- alpha 通道保持不变。

## 3. 实现细节

- 共享 16385 项 LUT,基于 radius²;另加一维(1D)轴向平方表。
- 临时辅助内存约64KiB + 4×(W+H)字节，不含既有图像/编码表。
- 未启用时(参数为默认 0/inactive)不构建 LUT。
- 融合路径(fused path)中不生成完整 mask,也不额外申请整幅 float 缓冲。

## 4. 参考与边界

- 参考[Adobe官方裁剪后暗角](https://helpx.adobe.com/au/lightroom-classic/desktop/process-and-develop-photos/retouch-photos.html)。
- 自有实现,不声明与 Adobe 等价(own implementation,not Adobe equivalence)。
- 尚未实现 roundness / highlight priority。

## 5. 验证结果

### 5.1 原生测试 final: 7 passed / 0 failed / 1837 ms

覆盖:
- 零参数恒等(zero identity)
- 带符号 HDR(signed HDR)
- alpha 保持
- 3x 归一化预览 与 full 对比
- 裁剪/旋转/翻转中心点(crop-angle-flip center)
- LCMS <= 1 code
- 纯翻转 4 组合(pure flip 4 combos)
- 隐藏用例: 有限 RGB 3e38、alpha 0、NaN 拒绝
- JSON / 预设 / 历史

### 5.2 修复记录

- 初始UI测试在Redo之后预览尚未就绪时点击复位失败；测试改为等待精确就绪，未据此修改产品复位逻辑。
- 初始真实 crop-angle-Hflip 反馈失败(frames >= 4),定位为串行 full-flip 重采样问题。
- 修复: pure flip 现为 CPU float 与 fused 四线程编码器中的精确索引置换(exact index permutation),不再使用 bilinear。
- 隐藏用例 alpha 0: 在矩阵增长前已清除(hidden alpha 0 cleared before matrix growth)。

### 5.3 真实 RAW/JPEG 用例

实际 RAW 9579 / JPEG 0705,裁剪 (.1, .1, .8, .8),校直2°、水平翻转，三个控件每项约2秒:

| 源 | previews | paints |
|---|---|---|
| RAW | 13 / 13 / 12 | 14 / 14 / 12 |
| JPEG | 13 / 12 / 12 | 14 / 12 / 13 |

- 每个 3 passed,耗时 29404 ms / 26322 ms。
- 精确释放(precise release)与一次 Undo/Redo 正常。
- 初始失败日志保留(failed logs retained)。

### 5.4 工程探针(engineering probe,非导出 UI)

recipe: EV .15, texture .1, sharpen .3, radius 1.5, mask .7, vignette -1.2, mid .35, feather .8, Hflip,无裁剪:

- RAW 6960x4640;JPEG 4640x6960;合成 45MP 9000x5000;
- preview 3840x2560 / 2560x3840 / 3840x2133;
- 峰值私有提交 1794.85 / 1795.73 / 2498.74 MiB;
- 3 次 exit 0,sourceUnchanged true。

### 5.5 构建与回归

- Final build 0 错误。
- CTest 3/3,147.66 秒(S2 143.96 / S1 0.28 / S0 3.40)。
- 12 个原始文件 SHA 未变更(12 original SHA unchanged)。
- 原生 `LuminaS --smoke-test` 返回 0。
- 最终 JPEG 由原生 UI 截图,并由 GPT 本地查看。

## 6. 会话与协作审计

- DSH 全新会话 dca82543...: 仅 5 次 pwsh 调用,仅 4 个允许的源文件;范围审计未发现私有 QA/照片/密钥引用。
- 提供方用量: 5 步,input 17719,output 3346,cacheRead 50688,total 71753;非 RMB 账本(不代表人民币余额)。
- GPT 修正草稿中 bool 无效 Recipe 返回/类型问题;审查者修正 direct test 的 fake instance / Img / setRecipe / flipH / slider 前缀 / waits 后才应用。
- 核心数学/alpha/置换及测试由 GPT 完成。
- 本批次所有照片 QA 均留在 GPT 本地,未恢复被隔离(quarantined)会话。
- S3 准备: 已更新全局输出坐标 + tile 绝对偏移规则;未做 tile/局部生产。

## 7. 已知限制与未完成项

- 30/60 fps 仅为额外成果(extra achievement),p95 未认证(not certified)。
- 仍待完成: 镜头畸变(lens distortion)/CA/透视(perspective)、精确 PointColor、自动白平衡/物理色温(auto WB/physical Kelvin)、颗粒(grain)/预设强度(preset strength)、画质认证/真实 45MP RAW/8GB 证明。
- S2 并未全部完成(not all S2 complete)。
- 不虚构分数/测试/付费余额,不声明与商业产品等价。