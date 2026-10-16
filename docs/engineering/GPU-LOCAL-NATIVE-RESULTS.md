# GPU局部常驻原生窗口验证

2026-10-16。单层G3常驻局部计算已接入隔离QRhiWidget原生窗口；仍为研究路径，不是生产GPU后端验收。后续多层、ICC与真实样本仍需验证。

## 实现与计时口径

- `LocalPipeline`拆分为可在既有command buffer记录的`record`与离屏`dispatch`；窗口内不逐帧finish或读回，直接将线性输出送入固定sRGB显示shader。
- 新增`gpu_probe --local-present`及脚本`-LocalPresent`；CPU对照调用实际`renderDocumentPreview`和256MiB局部缓存。局部混合包含九项参数和.65层强度，蒙版为径向源空间羽化覆盖。每次改变曝光、保留其余局部参数，输入/蒙版只上传一次。
- 请求递增epoch，在GPU实际记录render时捕获，frameSubmitted回调携带该记录epoch；CPU在paintEvent捕获并反馈。等待中的epoch必须相等才采样，旧反馈不归入新请求。每次匹配反馈后才请求下一次更新，共5次预热+100次测量。这不是高频排队/丢帧压力测试，也不是人工鼠标输入到光子测量。
- GPU终点为QRhiWidget.frameSubmitted，CPU终点为QWidget绘制完成；两者不同，不从这组数值宣称实际用户体感提速倍数、物理扫描输出延迟或30/60FPS认证。GPU帧提交也不等于面板显示完成。
- 固定4K颜色缓冲3840×2560，实际DPR1.25；窗口客户端1000×740。结束后用grabFramebuffer读回实际颜色缓冲，对照CPU渲染+sRGB转换+同一预乘格式，全通道允许≤1码值。没有保存截图/整幅图像文件。
- GPU窗口、CPU窗口依次运行，GPU对象与其资源先销毁，再开始CPU对照；不跨QRhi实例/线程共享资源。重新initialize时标记需要重新上传，不把历史上传计数当成新资源已就绪；设备丢失恢复尚未故障注入验证。

## 实际构建、运行及数值

首次`gpu-local-native-build.txt`构建exit0；直接原生100轮`gpu-local-native-first.json` exit0，GPU median22.5444ms/p95 25.9207ms，CPU211.7018/224.3136ms，颜色缓冲最大差1、超差0。随后通过公开脚本跑同一输入，`gpu-local-native-wrapper/report.json`、`process.json`均通过，确认新flag实际传递给进程。

最终补充暗部/中间调更密集的输入：保留原合成图每127列的HDR/负值，其余RGB乘.1，alpha不变，避免只对大量剪裁到白色的HDR样本取证。增加DPR、请求epoch、旧反馈计数、CPU缓存统计后重新构建，`gpu-local-native-observability-build.txt` exit0。

实际命令：

`./tools/run-gpu-experiment.ps1 -ReportDirectory build/private-qa/gpu-local-native-final -LocalPresent -Iterations 100 -Size 4k`

exit0、29.448秒，五类硬件/源manifest/报告/进程/输出日志保存在新目录，未覆盖旧报告。

| 最终暖路径 | n | median ms | p95 ms | max ms |
|---|---:|---:|---:|---:|
| GPU更新→frameSubmitted | 100 | 21.9812 | 25.6452 | 50.1832 |
| CPU更新→绘制完成 | 100 | 211.1792 | 231.3818 | 233.9809 |

两边最后请求/提交epoch均105，旧反馈拒绝计数0。GPU共渲染109帧（含初始化/验证等非测量帧），基础帧上传1、覆盖上传1。最终真实4K颜色缓冲最大差1码值、超过1码值通道数0。CPU缓存builds2/hits210/retained196,608,000字节，即基础帧与覆盖暖复用；并非每次重新准备基础帧，也不是复用最终成片而伪造反馈。

## 资源

GPU验证读回时私有提交986,394,624字节，进程峰值1,000,157,184字节，峰值working set580,939,776字节；DXGI local518,795,264字节。这些包含CPU输入、覆盖及最终读回/参考缓冲，不能叫无读回编辑稳态，也不能将UMA段统计直接当独立物理显存。

GPU widget销毁后同一进程私有提交430,227,456字节；CPU对照结束后167,391,232字节，working set202,354,688字节，此时外层157MiB合成源仍存活。证明本次生命周期能退出并下降，但不代替100轮创建/销毁、设备故障、真实源或生产进程树资源验收。

## 相关回归与审查

- 启动脚本PowerShell AST语法检查通过；互斥模式、原生small尺寸均在报告目录创建前拒绝，日志`gpu-wrapper-invalid-{modes,size}.txt`。未调整已有路径防护、超时及进程归属。
- `-Present`原有基础窗口复测100次exit0，`gpu-basic-native-regression`：GPU18.7325/22.8195ms median/p95，CPU79.5989/93.9707ms，颜色差≤1，源上传1。与局部模式使用不同输入/算法，不混合比较。
- `-Local -Size small`离屏回归100轮exit0，`gpu-local-record-regression`：五组float数值通过，证明拆分record后仍可在离屏实例使用。
- QA图片输出复查0；默认原生实验不再生成PNG，数值验证只用进程内图像。

DeepSeek启动脚本草稿因选择行范围无效被工具拒绝，未自动重试；GPT完成flag接线及实际检查。另有文字初步审查input6863/output1092 tokens；采纳增加CPU缓存统计，拒绝每轮清缓存的建议（会把暖路径改成冷路径）。epoch错配不会被记成成功，超时会失败；多次shutdown的空资源清理可重入。所有Qt/QRhi生命周期、数值与最终运行由GPT复核；worker未接触照片。

## 下一步

推进多层常驻与任意有效显示器ICC的独立正确性/原生验证，再决定生产接入边界与CPU回退。还需真实JPEG/CR3、源与蒙版变化上传、旧结果隔离和设备故障/多轮释放。不能把本轮“固定输入+既有覆盖”的25.6ms p95当成S3新画笔overlay的33ms目标通过。

C6实照/prepared组合、D恢复/RAW隔离/资源/局部导出桥仍待完成；上一轮完整61项仍60通过，色差拖动偶发与历史atomic_file问题未定位。生产后端不变，没有打包或推送。
