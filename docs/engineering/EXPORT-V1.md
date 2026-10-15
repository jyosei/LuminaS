# 图片导出 v1 — 实施与验收记录

2026-10-15，软件范围已完成并验收，最终结论与限制见EXPORT-V1-ACCEPTANCE.md。以下保留实施过程，不以历史待验状态覆盖最终记录。不主动打包、不创建GitHub仓库、不推进S3。完成后由用户准备仓库并以v0.1.0发布，再按新计划进行S3，目标一个月内v1.0.0。

## 依赖

新增LibTIFF4.7.1及zlib1.3.1；来源、SHA256见.tools/export-deps/sources.json。使用官方源码，本机MSVC2022构建静态库，不在线调用图像服务。cmake/ExportDependencies.cmake明确本地SDK；不在应用构建中隐式下载。zlib需安装至.tools/export-deps/zlib-install；TIFF源码位于.tools/export-deps/tiff-4.7.1，可用CMake变量覆盖。

## 原实施验收清单（最终结果见验收记录）

- 原源全尺寸渲染→浮点缩放→目标ICC→8/16位→JPEG/PNG/TIFF，不以代理放大成片。
- RGB四种内置ICC及CMYK导入/系统配置，转换预览与读取实际输出。
- 尺寸、方向、元数据白名单、GPS默认关闭、ICC嵌入与物理100%。
- 冻结配方/哈希/ICC/路径、串行执行、原图保护、失败继续、取消与重试。
- 持久记录原子保存、版本校验、重启后不自动运行。
- 小窗口/键盘可达、设置预设、实际R7 JPEG/CR3和45MP边界。

## 委派记录

初次DSH5e3d122b-085e-48c0-978a-9e558cfb6029因Messages transport失败终止，交付部分类型/队列/检查器草稿，未接受为完成；队列缺失方法、尺寸适配不正确、检查器Canvas调用类型错误，由GPT修正。未自动重试该会话，界面改为单独窄任务。直接API元数据草稿达到8192输出上限而截断，不采纳；GPT实现有界白名单读写。实际usage在.tools/deepseek/usage.jsonl；不以token推算余额。


## 当前已取得的实测证据

- 首轮引擎/队列专项2/2通过（2.85秒），不代替后续最终回归。
- 最新独立引擎8 passed、1 opt-in跳过；UI6 passed；队列5 passed，详见export_engine-third.txt、export_ui-third.txt及export_queue-second.txt。
- 全源R7 JPEG/CR3均输出4640×6960的ProPhoto RGB16 TIFF，分别32395/33287ms；合成45MP输出9000×5000，35314ms，超限JPEG拒绝。全源专项3 passed/0 skipped，共103157ms（export-v1-real-full.txt）。该轮早于TIFF设备回调及元数据指针补充，最终版本复查仍待完成。
- 12张原片与最初manifest的SHA256全部一致（export-v1-source-check.json）。
- 修复过的实际问题：Windows临时文件保留句柄造成提交失败；宽高框/打印尺寸失去比例；IFD类型13导致TIFF EXIF/GPS未被检查器解析；预设名称与实际设置不同步；高级选项隐藏祖先被测试误计为可见。失败日志保留，未伪称一次全绿。
- 磁盘满验证使用返回ENOSPC风格错误的输出设备，验证写入失败不提交；没有故意耗尽物理磁盘。不可写路径另以父路径为普通文件验证。

第二个窄UI任务edcd5f85-1ec3-42a2-9970-d15f7410d4fc未交付代码，越过指定只读范围探索工具链，并启动了明确未允许的baseline ninja构建，已取消，确认该ninja进程停止。未自动重试。GPT完成界面与整合；截至审计未发现私人照片/预览/截图或凭据读取，实际工具参数与去重usage在.tools/export-ui-live-audit.json；首任务审计在.tools/export-v1-dsh-audit.json。

直接API初步评审（review，7257 tokens）由GPT逐条复核：未采纳其临时文件泄漏推断（Qt对象析构自动清理，已有取消后无.tmp实测）；其源别名目标场景仍被存在性/不覆盖提交保护拦截；哈希读取为流式而非全文件分配；load错误保护正是设计。未以该草稿作为验收结论，亦未因无证据建议改动核心或反复重测。


最终补充：33/33完整回归363.33秒后，相关收口6/6通过19.24秒；DPR1/1.5/2各4 passed；最新全源R7 JPEG/CR3及精确45MP3 passed，真实CR3导出与100%原生检查3 passed，最终main进程真实JPEG绘制exit0。详见EXPORT-V1-ACCEPTANCE.md。

终审发现DSH取消并不保证其后台shell子进程同步终止，第二会话有超范围Add-Content空白触碰export_types.cpp及两个baseline Ninja进程；未接受逻辑改动，后续GPT修改/构建覆盖。已验证进程归属并停止29372及24416，最终无ninja/cl/link后台写入者。完整审计export-ui-final-audit.json：65 provider steps，input47371/output9623/cacheRead2261760；首任务64 steps，input67128/output28985/cacheRead3746432。无私人照片/预览/截图或凭据读取记录。后续执行层应禁止自行工具链探索/构建，并由GPT核查真实子进程。
