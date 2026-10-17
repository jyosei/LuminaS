# D3 多轮源/局部编辑资源观察

2026-10-17。资源采样/流式哈希切片继续通过；**D4百轮释放、D5全源导出桥接及S3尚未完成**。不推送、不打包。生产缓存预算与算法未改。

## 用例与实际结果

新增独立`s3_resource_cycles_tests`，默认未给显式cycles及私有样片时skip、不生成文件。一个Qt进程中每轮新建/销毁内存profile Window，预加载6对R7，cached JPEG切图，返回RAW首图，texture=.4/clarity=.3/perspective(.03,-.02)，再创建非空笔刷并设局部曝光.3。断言最新document.layers相等、4K、真实fullGeometry支持和目标canonical paint；每轮重引用只在本轮块内。循环前后每个源独立Qt SHA核对，外层只保留摘要与有限JSON标量。直接Canvas信号测试控制器/资源，**不冒称鼠标命中测试**。

GPT修订基础草稿（错误头/字段/shared_ptr用法、拼接hash弱化逐源验证、paint等待时点及快照持有等），实际构建exit0：`s3-d3-cycles-build.txt`。默认CTest1/1、exit0：`s3-d3-cycles-default-ctest.txt`；默认性能slot明确skip，不当作实照执行。

首轮显式R7：3 passed/70645ms，外层exit0/71.3132309s；预处理结束56056ms、完整几何65160ms、局部真实绘制67224ms、释放69469ms。观察139轮，tree private peak1,832,329,216B、working set1,712,795,648B、disk peak2,404,160,955B；最后disk0/remaining空，缺测0。日志`build/private-qa/s3-d3-r7-cycle1.*`。这不是纯导入/解码耗时。

为准确连接phase和资源序列，采样器及测试增加Unix毫秒起点；单独重建exit0（`s3-d3-cycles-epoch-build.txt`），不改图像或生命周期。随后同进程**3轮/36次文件预处理**：Qt3 passed/226957ms，外层exit0/228.4661679s，12源前后SHA均一致，三轮均通过局部/完整几何/4K/绘制门禁。

| 轮（0起） | opening→released | tree private观察峰值B | 释放观察private B | 释放disk B |
|---|---:|---:|---:|---:|
| 0 | 68.912s | 1,871,679,488 | 16,379,904 | 0 |
| 1 | 77.192s | 1,874,014,208 | 20,762,624 | 0 |
| 2 | 77.095s | 1,876,766,720 | 21,962,752 | 0 |

释放区间为Window/本轮所有强引用销毁后150ms至下一阶段，分别有1/2/2个采样点；对应working set约24.65/23.28/25.31MB。释放private仍有约5.58MB增加，**三轮不能宣布无增长或排除泄漏**，由D4百轮继续判定。窗口活动峰值包含12个显示缓存、完整RAW几何和局部缓存，不能将不同时间各进程峰值直接相加。

共442次500ms采样、9轮内存查询缺测显式标记；磁盘峰值2,404,160,956B（多于此前无全源几何用例，包含RAW full交接临时帧），最终两根disk0、remainingKnownAlive空。采样不是连续精确峰值，不证明间隙内没有短命子进程。8GB机器和真实45MP RAW仍未验。

## 证据与清理

`build/private-qa/s3-d3-r7-cycles3.txt/.json/.resources.json/.phases.json/.control.json/.summary.json`保留结果、实际序列和按同一epoch连接的阶段统计。不生成截图/比较成片；每轮约1.9GB工作缓存及交接帧正常结束后自动回收，GPT逐一核实本次private-qa中独立临时根为空后只删除空根，未递归删除或触碰旧40MB拒绝删除目录。

最新完整回归仍流式哈希接线的77/77（417.66秒）；加入本可选用例后注册78项，尚未重新完整执行，不把新slot默认skip冒充百轮/实照通过。接下来D4有界百轮、D5冻结局部图层全源接口与最终全套。
