# LuminaS DeepSeek worker

GPT supervises; DeepSeek Flash performs bounded simple/bulk tasks. Core algorithms and model development stay with GPT.

This is a draft-producing worker, not an autonomous coding agent. It does not execute commands, modify project files or run tests. GPT performs those steps after reviewing its output.

## Usage

Create a UTF-8 JSON request file:

```json
{
  "category": "documentation",
  "task": "Summarize the supplied document in Chinese. Preserve limitations and distinguish implemented behavior from planned work.",
  "files": ["README.md"],
  "max_tokens": 1024
}
```

From the project root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/deepseek/launch.ps1 .tools/deepseek/request.json
```

Categories: `summary`, `documentation`, `translation`, `bulk_transform`, `test_draft`, `simple_code`, `review`. Maximum 30 explicitly selected text files, 200 KB per file, 400 KB combined request, 8192 output tokens. No implicit repository upload. Paths must resolve inside this workspace, including through symlinks. Known key patterns are redacted; this is not a general secret scanner, so review selected input for other credentials.

## Saving tokens and reusing prefixes

The worker puts stable source context before the changing task and canonically sorts selections by project-relative path/range. Unchanged necessary context can therefore form a common prefix across related tasks. Different files, changed file contents or changed ranges still create different prefixes. DeepSeek cache is enabled automatically; its current prefix persistence rules can require two completed requests sharing a prefix before a third request hits it. Persistence takes seconds and hits are best effort. Do not make paid warm-up calls or add irrelevant text just to raise the hit rate. See [current cache rules](https://api-docs.deepseek.com/guides/kv_cache/).

Prefer exact inclusive line ranges over whole large files:

```json
{
  "category": "simple_code",
  "task": "Return only the minimal changed snippet for the specified UI wiring; preserve the core algorithms.",
  "files": [{"path": "src/app/window.cpp", "start_line": 100, "end_line": 180}],
  "max_tokens": 2048
}
```

Inspect request size without an API call (bytes are not a token estimate):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/deepseek/launch.ps1 .tools/deepseek/request.json -DryRun
```

Responses and usage logs now include `cache_hit_ratio`, `context_sha256`, `request_sha256`, layout version and input/context byte counts. Identical context hashes help identify reuse opportunities, but do not prove a provider cache hit. The API's hit-token counts are the evidence. Output generation is still charged separately. Avoid complete file rewrites, and combine related small operations where practical.

The project `.codex/config.toml` registers a stdio MCP tool `luminas_deepseek.delegate_task`. New/reloaded Codex sessions can discover it if the project is trusted and project configuration is loaded. Existing sessions can use the CLI immediately. No change to the main GPT model/provider is needed.

The PowerShell launcher loads the environment key without printing it, including the Windows system variable when the current desktop process has not inherited it. Uses Node on PATH or the current Codex bundled runtime fallback; update the fallback if the runtime moves.

Usage metadata is appended to `.tools/deepseek/usage.jsonl`; it contains token counts, category and file paths, not task text, source contents, key or response. It is not a monetary balance ledger. Requests time out after 180 seconds and are never retried automatically. `finish_reason: length` indicates a truncated response and must be checked before integration.

API: `https://api.deepseek.com/chat/completions`, model `deepseek-flash`, thinking disabled for simple tasks. See [DeepSeek documentation](https://api-docs.deepseek.com/zh-cn/) and [Codex MCP configuration](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).
