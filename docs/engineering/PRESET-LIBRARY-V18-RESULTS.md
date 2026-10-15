# LuminaS V18 持久化预设库结果报告

日期：2026-10-13。范围：LuminaS S2持久化预设库。本报告由新的文字任务按GPT核定事实撰写；本批代码执行会话的越界图像读取见第4节，不能混同为全程仅文字。

## 1. 已实现能力

- **托管副本位置**：`AppLocalDataLocation/presets`；最多 64 个文件、非递归、单文件上限 256 KiB、严格 JSON 解析。
- **命名与写入**：名称 trim 后参与文件名 SHA 命名；采用原子 `QSaveFile` 写入，同名替换。
- **启动加载**：内置 4 个 + 自定义预设，按去重 / 名称排序 / 数量 / 警告处理。
- **保存与导入契约**：预设文件排除白平衡和几何；应用时保留目标照片现有WB/裁剪/校直/翻转。正常就绪状态保存不触发渲染或新增历史；导入应用沿现有单条历史。
- **失败语义**：导入时托管副本写入失败，不改当前参数、历史或列表；启动时无效文件跳过并提示，其他有效项仍加载。
- **部分保存失败提示**：只有外部JSON成功而托管副本失败时，明确提示“外部 JSON 已保存，但预设库保存失败”；外部文件自身失败时保留实际错误。
- **测试隔离**：显式空库构造函数在测试中禁用持久化；测试使用工作区 `v18-temp` 目录。

## 2. 验证结果（实测）

- 原生库 / 存储 / 容量 / 目录目标 / 重启 / 外部 JSON 删除 / 失败 / history：**6 passed, 0 failed, 2335 ms**。
- 最终构建：**0 错误**。
- 完整 CTest：**3/3 通过，133.97 秒**。

## 3. 明确未完成 / 未改动

- JPEG/RAW 渲染算法未改动。
- 未实现照片导出（photo export）与工程保存（project save）。
- 删除 / 重命名 UI 与 preset strength（预设强度）仍缺失。
- 本报告不代表全部 S2 完成，也不代表 S3 已实现。
- 30/60 FPS 仅为额外成果，属用户规则范围。
- 原生启动：LuminaS --smoke-test退出0；12份原片与manifest的SHA核对无差异。

## 4. DSH 会话与工具面审计

- DSH 首个会话 `639b7ee6...` 出现 looped temp image baseline，并错误使用 RPC `session/cancel`；正确ACP要求notification且无id，见[ACP v1取消协议](https://github.com/agentclientprotocol/agent-client-protocol/blob/main/docs/protocol/v1/prompt-turn.mdx)。
- GPT 修正了 bridge；`test-cancel.mjs` 通过，doctor 显示 `paid_prompts=0`。
- 第一个项目 ACP helper 在完成 PID / parent / profile 校验后停止，Desktop 未被触碰。
- 第二次相关 CLI 运行发生实际 core / window 源码写入，状态在达到上限时以 `stopReason=cancelled` 停止；GPT 复核并修复了错误的 nullable 语义、applyPreset 调用与下拉提示归属，并运行了实际测试。
- DSH 日志审计发现三个 `read_image` 工具结果，其中两个包含历史 UI 截图中的私有 R7 照片；此处如实披露，不能仅以 source-only 委派方式表述，且无法撤销此前对提供方的提交。该会话不得恢复。
- 项目 automation overlay 现已禁用 tool-fs suite；fresh smoke `f884ff29...` 完成，结果为 `PROFILE_OK`；请求 / header.tools 中实际目录仅 `glob` / `grep` / `pwsh` / `todo_write`，不含 `read_image` / `read` / `write` / `edit`。
- Shell 仍具备文件访问能力：这**不是** OS 路径白名单，也不是完整隐私保证。
- 后续 DSH 仅限 fresh bounded text 任务，由 GPT 复核；不会提出新的照片相关请求。
- 提供方用量为本地审计，未提出人民币余额结论。

## 5. 报告约束声明

本报告不含 API key、完整日志或未经支持的结论。
GPT复核限制：目标符号链接拒绝代码已检查，但本批没有实际Windows符号链接用例；整体摄影质量、真实45MP RAW与8GB机仍未认证。首次MSVC D8050失败后改用工作区临时目录重建成功，不声称已完全定位原系统临时目录故障；旧失败日志保留。
