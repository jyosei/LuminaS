# C5首项：复用蒙版显示索引图

2026-10-16。C5仍在进行；本批不代表整个缓存/性能切片完成。

将现有float覆盖通道量化后的Indexed8图像作为独立、无调色板的不可变payload，纳入同一个256MiB LocalPreviewCache。key为既有蒙版语义key加`mask-indices-v1`命名空间；参数/名称/opacity/显隐不改变覆盖，实际组成项/几何/feather/invert和mask版本变化会失效。命中时跳过PreparedBrushMask、几何alpha准备、覆盖查询与重新量化。显示色和黑白模式仍由Canvas提供，不写入缓存图像或RGB输出。

输出尺寸使用与CPU渲染相同的renderedSize/previewSize；冷构建再校验实际alpha尺寸，避免仅用debug断言掩盖不一致。缓存预算、ready/inflight/pinned、join、取消/clear与逐层RGB算法保持原合同。

## 实际验证

- 构建：`s3-c5-index-cache-build.txt`生产及相关目标exit0；补充测试后`index-reviewed-build.txt`与`index-native-build.txt`均exit0。
- 最终 `s3-c5-index-final-check.txt` **3/3，2.81秒**：renderer10、cache24、mask window5 passed（含init/cleanup）。
- renderer补测显示索引图共享同一个QImage key；局部参数/名称/opacity/显隐复用，strength/operation/brush geometry/feather/invert实际值变化即使未推进版本也会失效，mask版本推进及clear也重建；逐项与当前无缓存RGB及索引图精确对照。缓存图不带UI调色板。
- 原生补测同一次参数拖动：第一帧准备后改变曝光，索引图key相同，实际照片hash改变并完成规范帧绘制，未增加同一覆盖的maskPaint通知。事务边界/显隐变更仍可重新呈现已有覆盖，不能把复用或控制点绘制计作新的蒙版计算帧。

## 前后测量

工具 `local_mask_probe --overlay` 使用3840×2560合成float工作帧、6000×4000源坐标、2笔共1000点，不读照片。新增maskElapsedMs与索引SHA记录；两次运行OMP=4/PASSIVE、无同时构建/测试。每次1轮冷计算、3轮暖参数变化，样本数量不足以作p95或普遍速度承诺。

| 暖缓存阶段 | 修改前ms（3次） | 修改后ms（3次） |
|---|---|---|
| 蒙版显示准备 | 72.877 / 71.402 / 70.837 | 1.412 / 1.484 / 1.411 |
| 整体计算（含局部RGB、sRGB与蒙版显示准备） | 230.747 / 235.889 / 238.036 | 178.626 / 172.666 / 178.986 |

计时不含原生绘制、源读取或解码，不能用作导入速度、实际拖动端到端时延或FPS。前后规范照片SHA和索引SHA一致；200点标量查询无不一致。缓存最终3次构建/10次命中，retained payload为206,438,400字节（196.875MiB），其中新增索引约9.375MiB；该预算不含源、输出及系统分配开销，不是进程RAM结果。

原始结果：`s3-c5-before-index-cache.json`、`s3-c5-after-index-cache.json`、`s3-c5-index-cache-comparison.json`，均在忽略的build/private-qa。没有写入图像输出。

## 协作与后续

DeepSeek提供maskView草稿（输入1885/输出1228 tokens），GPT修正共享preview_size头文件、增加release尺寸错误处理并避免重复序列化蒙版key。测试草稿（1714/1277）捏造ViewState、cache getter和可移动cache，没有直接采用；GPT用实际RenderView、QImage key和统计字段补测。worker没有执行或接触照片。

C5还需处理羽化的稀疏支持域、进一步的精确依赖/失效矩阵与真实R7原生端到端测量，再进入C6组合和D。33/100ms目标仍未通过；60项全量未重验，atomic_file偶发拒绝访问未解决，真实45MP RAW/8GB设备与D资源/导出桥仍待验。没有截图、包、ZIP、快捷方式或推送。
