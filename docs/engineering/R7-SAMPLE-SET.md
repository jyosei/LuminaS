# R7 扩展素材验证

日期：2026-10-12（Asia/Shanghai）。本地现有6组CR3+同名JPEG，共12文件。用户确认均由R7拍摄，并最终确认设计上限保持45MP，不扩到6680万像素；不要求为当前R7工作强行补齐45MP真实样本。

## 实际检查

- 6张CR3分别运行官方参考路径与4线程OpenMP/直接缓冲候选，各3次，共36次RAW处理；6张JPEG各3次。全部成功，原路径/候选4K像素hash逐文件一致，RAW处理前后源hash一致。
- 额外逐张检查Qt解码的JPEG原生方向归一尺寸，均与对应RAW的厂商有效画面一致。不是仅比较缩小后长宽比。
- 全集原始文件SHA-256核对不变，manifest记录尺寸/哈希与仅本地测试授权。照片/截图/详细报告在被忽略的build/private-qa/r7-expanded，不上传。
- 原生候选窗口逐张打开12张、全部保留标记/筛选、30次快切后只提交最终图，全部通过；3 passed包含一个功能case与init/cleanup，21.712秒为测试总时间，非逐图打开时延。
- 截图已视觉检查，照片、4K尺寸、12张胶片条和保留状态可见。胶片条横向滚动属正常行为，不靠截图认证RAW画质。

## 实测性能

32.2944MP的有效画面均为6960×4640；其中9579为横图（flip0），其余5张竖图（flip5）。预览分别3840×2560和2560×3840，小图规则未改。

| 文件前缀 | 参考RAW中位ms | 4线程候选中位ms | 耗时减少 | JPEG中位ms |
|---|---:|---:|---:|---:|
| 4X1A0705 | 4938 | 2517 | 49.0% | 232 |
| 4X1A9413 | 4829 | 2591 | 46.3% | 225 |
| 4X1A9579 | 4629 | 2244 | 51.5% | 215 |
| 4X1A9817 | 5019 | 2676 | 46.7% | 230 |
| 4X1A9827 | 5063 | 2645 | 47.8% | 203 |
| 4X1A9954 | 4994 | 2561 | 48.7% | 252 |

参考进程峰值私有提交748.93–749.02MiB；候选563.35–563.54MiB。每文件/模式是独立进程，内存为该进程3次运行的生命周期高水位，不含其他进程/GPU，不冒称整机峰值。OS缓存/CPU温度/后台负载未控制，样本数量不足以认证p95/功耗，仍是本开发机数据。

结果支持继续以4线程+直接缓冲作为R7候选。全分辨率去马赛克、WB、矩阵、gamma、裁边、源完整性检查不变，没有用内嵌JPEG或低分辨率显影替代RAW；不能由此声明其他机型/模式或正式颜色质量通过。

## 运行证据

候选speed_probe重建成功；实际批量命令：

```text
python -u tools/speed-probe/check_sample_set.py pictures4test build/private-qa/r7-expanded --repetitions 3
```

退出0、ALL SAMPLES PASS。首次批次报告包含JPEG缩小尺寸；随后新增normalizedSourceSize计时工具字段，并用下列独立补充检查验证完整JPEG几何、源hash：

```text
python tools/speed-probe/verify_sample_geometry.py build/private-qa/r7-expanded
```

退出0；补充几何检查的单次时长未混入上表3次性能结果。以后check_sample_set会直接断言完整JPEG尺寸，不需要补充工具。输出目录必须新建，避免覆盖旧测量。

原生窗口测试：设置QT_ROOT/PATH/插件、QT_QPA_PLATFORM=windows、OMP_NUM_THREADS=4、LUMINA_REAL_SAMPLES后，运行 `build/openmp/RelWithDebInfo/s1_tests.exe expandedLocalSetWindow -o docs/engineering/evidence/r7-expanded-window.txt,txt`，退出0。截图仅保留私有目录。默认工程构建及CTest2/2通过，真实素材case默认跳过，已在上述原生检查单独执行。

共享数字记录：evidence/r7-expanded-summary.json；构建/原生/CTest日志：evidence/r7-*；完整输入manifest更新为tests/fixtures/manifest.json。

## 当前阶段与S2交接

S1的**R7工程基线实验已通过**：解码、方向/尺寸、4K、原/优化一致性、源安全及本机速度/内存有真实多张证据。正式RAW颜色/细节/高光可调性将在S2及后续P0质量验证中继续，不把与机内JPEG的灰暗差异视作失败。

45MP仍是输入设计上限，已有JPEG合成边界/安全整数检查；真实近45MP CR3未测，明确记录为覆盖缺口，不作为当前R7/S2推进阻碍。没有宣称45MP实机性能通过，也没有提高像素上限。

下一步可进入S2。具体实施顺序/接口/验收见S2-IMPLEMENTATION.md；本轮只准备方案，没有创建S2空模块或宣称调参已实现。
