# Codex → MCP → DeepSeek Harness → ACP

Codex owns task selection, core image algorithms/model development, acceptance criteria and final review. DeepSeek Harness executes bounded simple/bulk tasks using its filesystem and PowerShell tools. This bridge uses the CLI bundled with the installed DeepSeek Harness Desktop, not a third-party harness wrapper or another npm installation.

Verified installation: `C:/Users/CascadeAI/AppData/Local/Programs/DeepSeek Harness`, DSH `0.2.0-rc.2`. Set `LUMINAS_DSH_INSTALL` to override the installation directory. The launcher uses a fixed bundled Node path; update it if the Codex runtime moves.

The bridge speaks stdio MCP to Codex and ACP v1 to DSH. It uses a separate project-owned `DSH_HOME` at `.tools/dsh-home`, leaving the Desktop profile and chats alone. These automation sessions do not appear in the current Desktop profile. Sessions and results persist locally; `.tools` is ignored by Git.

## Tools

- `execute_task`: `category`, `task`, optional `session_id`, `timeout_seconds` (default 600; 10–1800), `max_tool_calls` (default 20; 1–100).
- `task_status`: inspect a returned `session_id`, no model call.
- `cancel_task`: cancel a running returned `session_id`.

Categories: documentation, translation, bulk_transform, test, simple_code, review. Core algorithms/model development are excluded. Include explicit allowed files, behavior to preserve and actual verification commands in the task. Related follow-ups can reuse the same session, even after bridge restart, to preserve context and reduce redundant exploration. Do not append unrelated work to an ever-growing session.

Tasks execute serially inside each bridge process because they share the project directory. Do not launch multiple bridges or other writers against overlapping files. DSH native `workspace-write` policy applies; requested permission escalation is rejected by the bridge and reported. Per-file task scope and core-algorithm exclusion are instructions, not an OS-enforced file allowlist; GPT must inspect actual changes. The bridge does not provide Git isolation or automatic rollback (this workspace currently has no Git repository).

## Current-session CLI fallback

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/dsh/launch.ps1 doctor
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/dsh/launch.ps1 .tools/dsh-task.json
```

The doctor starts ACP, creates and closes an agent session, but submits no paid model prompt. A task JSON can be:

```json
{
  "category": "test",
  "task": "Allowed files: tests/example.cpp only. Add the specified non-algorithm regression case and run the exact check supplied by Codex. Return changed paths, commands/results and remaining issues.",
  "timeout_seconds": 600,
  "max_tool_calls": 20
}
```

For status: `{"tool":"task_status","arguments":{"session_id":"<returned UUID>"}}`.

`.codex/config.toml` registers `luminas_dsh`. A Codex host must actually load/refresh its tool catalog before native MCP calls are available; creating this file or independently testing a server does not refresh an already-running chat. CLI fallback works immediately.

## Cost and execution bounds

The automation overlay selects `deepseek-flash`, thinking off, at most 8192 output tokens per model step (raised per user preference on 2026-10-14), zero provider retries, no web tool or nested subagent/workflow/goal tools, and no session telemetry exporter. It leaves file/search/shell tools enabled. Time/tool-count limits request ACP cancellation; they are not a strict monetary cap and a currently running call can still incur charges. Actual provider usage is in DSH's persisted session logs, not the old direct-API usage log. Inspect these when comparing costs.

DSH still uses your DeepSeek API balance. MCP does not lower the per-token price. Its benefit is an execution loop, focused tool reads and persistent sessions; it can cost more for tasks that a single direct request can complete. Retain `luminas_deepseek` for short draft/summary/translation requests.

References: [official CLI](https://github.com/deepseek-ai/deepseek-harness/blob/master/apps/cli/README.md), [official ACP](https://github.com/deepseek-ai/deepseek-harness/blob/master/packages/acp/acp/README.md), [Codex MCP](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).

## 2026-10-13 execution audit

ACP `session/cancel` is a notification without `id`, not a JSON-RPC request. The bridge now uses notifications for explicit cancellation and execution bounds; `node tools/dsh/test-cancel.mjs` checks the packet. A new CLI continuation actually returned `stopReason: cancelled` at its tool bound. An already-loaded MCP bridge retains the previous JavaScript until reloaded; use the corrected CLI fallback meanwhile. Reference: [ACP v1 cancellation](https://github.com/agentclientprotocol/agent-client-protocol/blob/main/docs/protocol/v1/prompt-turn.mdx).

The first preset-library session ignored the private-photo restriction and read three historical UI screenshots, two containing R7 photos, into image tool results. Do not resume that session (`639b7ee6-4948-41bf-8d25-e520d321ecb4`). Do not claim its input was limited to source text. Its helper was stopped; source changes were subsequently reviewed and locally tested by GPT. Prior provider submission cannot be undone by changing the local profile.

The project automation overlay now disables `tool-fs`, which includes `read_image`. Fresh text-only smoke session `f884ff29-a50a-4777-98b1-d3a098a5ae71` completed, and its persisted `request/header.tools` advertised only `glob`, `grep`, `pwsh`, and `todo_write`. Native read/write/edit tools are therefore also unavailable in that profile; bounded text changes can use PowerShell. This reduces the observed image-tool route; shell access remains and this is not an OS-enforced per-path allowlist. Use fresh, tightly scoped text sessions and inspect actual tool arguments/results, not only the final answer. Keep image QA with GPT locally.

Provider usage is persisted in concatenated zstd frames in `session.v4.jsonl.zstd`. Do not sum both `assistant/message.data.usage` and its mirrored stream chunk usage; neither the context-usage gauge nor token totals state the remaining RMB balance.
