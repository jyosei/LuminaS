# Codex decision layer / DSH execution layer — verification

Verified 2026-10-13, Asia/Shanghai. Installed DeepSeek Harness Desktop includes CLI `0.2.0-rc.2`. The bridge uses its official ACP v1 profile through stdio and exposes stdio MCP tools to Codex. No third-party MCP harness wrapper was installed.

Configuration: project `.codex/config.toml` registers `luminas_dsh`; user Codex configuration now trusts this Lumina directory so project MCP configuration is loaded. A backup of the previous user configuration is in `.tools/deepseek/codex-config-before-dsh.toml`. `codex mcp get luminas_dsh` confirms enabled stdio, startup timeout 90 seconds and tool timeout 1860 seconds. The existing `luminas_deepseek` remains enabled for one-shot drafts.

Checks completed:

- Official ACP initialize, session/new and session/close, with no paid model prompt; returned model selector is `deepseek-flash` and reasoning is off.
- Real DSH execution: read README first line, write `.tools/dsh-smoke/result.txt`, run PowerShell assertion. DSH recorded three completed calls (read, write, pwsh) and returned PASS. GPT independently checked the result file contains `LuminaS`.
- Real MCP initialize and tool discovery; catalog contains `execute_task`, `task_status`, `cancel_task`.
- Reserved algorithm task category rejected before model execution.
- Same persisted ACP session resumed after the bridge/DSH process restarted; a follow-up PowerShell assertion completed with PASS and MCP task_status confirmed completion.
- Test clients closed cleanly; no automation CLI process remained. The user's Desktop app remained running.

Session: `af0644f3-ec9a-4ba4-846b-3fa999ccd310`. Evidence: `.tools/dsh-smoke/response.json`, `.tools/dsh-smoke/mcp-verification.json`, `.tools/dsh-bridge/<session>.json`, and native compressed DSH logs under `.tools/dsh-home/sessions`. Reproducible end-to-end verification is `node tools/dsh/verify.mjs`; it submits a paid follow-up prompt and is not run automatically.

The bridge profile is separate from the Desktop profile: automation chats are not displayed in the current GUI profile. DSH still consumes DeepSeek API balance; execution loops are not inherently cheaper than direct calls. Native workspace-write policy bounds workspace operations; allowed files in task text and the GPT/DSH algorithm division are instruction boundaries rather than per-file OS access controls. GPT must inspect actual changes before accepting them. Time/tool limits request cancellation and are not a hard financial cap.

The current chat's host-provided tool catalog has no live MCP refresh API. Registration and independent protocol verification do not update that catalog. Restart/reopen Codex to load native MCP tools; in this chat the CLI bridge works immediately. See [bridge usage](../../tools/dsh/README.md) for request format and limits.
