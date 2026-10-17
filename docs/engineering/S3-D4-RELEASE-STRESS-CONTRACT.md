# D4 百轮释放用例与首轮验收

2026-10-17。百轮终态尚未取得；本页只确认已实现/独立验证的用例。S3仍未完成。

## 用例

`s3_release_stress_tests releaseStress`默认skip，显式`LUMINA_RELEASE_STRESS=1`启用。默认100轮，可用1..100做开发检查；**只有实际100轮结果才能关闭D4**。临时4500×3000 sRGB JPEG供合成轮使用，创建助手返回后释放原始QImage。提供`LUMINA_RELEASE_RAW_SAMPLE`时，每第10轮使用真实CR3，100轮应90合成/10RAW；未给RAW时必须报告rawCount0，不当实照通过。

每轮独立内存profile Window，导入1图、预处理完成、完整几何(texture/clarity/perspective)、非空局部笔刷/曝光、真实4K canonical绘制后销毁。所有强图像/快照引用只在该轮块内；`weak_ptr<const SourceFrame>`只观察fullGeometry，退出块后5秒内必须expired，另等1秒给采样器记录释放。内存观察不要求OS回零，也不把帧率设硬门槛。

逐文件独立Qt SHA在全程前后核对合成JPEG/可选RAW；原图只读。控制JSON与采样器共享Unix毫秒起点。报告先NewOnly创建，之后只原子更新本次自己创建的同一路径，QSaveFile明确`commit`且禁direct-write fallback。记录进度/RAW次数/有限阶段标量，不持有帧数组；不截屏或导出图片。合成源和私有工作缓存由正常生命周期回收。

## 已实际执行

- GPT实际构建exit0：`build/private-qa/s3-d4-release-build.txt`。
- 默认CTest1/1 exit0：`s3-d4-release-default-ctest.txt`；性能slot确为skip。
- 预存报告拒绝：故意运行返回exit1，Qt明确`Could not create a new release report path`；既有sentinel SHA与字节不变。父级检查exit0，`s3-d4-existing-report-protection.json`；Qt失败日志`s3-d4-existing-report-rejected.txt/.json`是期望拒绝，不冒称压力用例失败修复。小型sentinel保留为证据，不是成片。
- 单轮原生烟测：3 passed/12531ms，外层exit0/13.9714405s；实际4K/fullGeometry/非空笔刷/paint门禁和weakExpired均通过，最终报告completedCycles1、sourcesUnchanged=true，证明显式commit更新有效。该轮synthetic1/raw0，虽然提供了RAW路径，但尚未到第10轮，不称RAW已执行。日志`build/private-qa/s3-d4-release-smoke1.*`。

## 审查与未验

DSH `d2ccb643-f8d8-4a13-a7e3-e70b958c8d9b`只新增这一个测试，4次实际工具操作仅读3份允许文本/写新文件/静态检查，未执行程序或触碰照片/凭据。input8329/output4059/cacheRead40192/total52580，不当余额。

GPT在构建前修正：错误地依赖QSaveFile析构commit、首次未用NewOnly、weak变量作用域、RAW 0起轮次少一次、初次解码错误要求sourceRebuilt=false、头文件及规范化路径。工作者静态“括号平衡”不能证明构建/释放正确，实际检查均由GPT执行。

下一步在同一Qt进程实际100轮，提供R7 RAW，核对90/10、全部weak释放、逐源SHA及结束缓存清零，并按采样epoch核验释放后private/working-set趋势。先前三轮16.4→20.8→22.0MB的小增长尚不能宣布无泄漏。最新完整回归仍77/77（417.66秒），目前注册79项，不把两个新用例默认skip当百轮验收；最后完整回归在D5整合后进行。
