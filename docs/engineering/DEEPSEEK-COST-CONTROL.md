# DeepSeek cost control verification — 2026-10-13

Before this change, the local usage log contained seven calls (including the initial smoke test): 37,672 input tokens, 8,797 output tokens, zero cache-hit tokens. One call included 20,720 input tokens. The old request layout placed the changing task before file contents, preventing those files from forming a reusable input prefix across different tasks. File sets and file contents also varied, so layout alone cannot explain all misses; provider caching is best effort.

The worker now puts context first, sorts explicit file selections deterministically, and appends the changing task. It supports inclusive line excerpts and a no-API `-DryRun` size check. Usage records contain context/request hashes, input byte counts and the provider's cache-hit ratio. No additional repository context is uploaded automatically.

Live verification used unchanged README content and three distinct short documentation questions (current name, supported formats/executable path, outstanding work). Answers were checked against README. Records are in `.tools/deepseek/cache-check-*-response.json` and `usage.jsonl`.

| Request | Input tokens | Cache-hit tokens | Hit rate | Output tokens |
| --- | ---: | ---: | ---: | ---: |
| 1 | 1,527 | 0 | 0% | 28 |
| 2 | 1,522 | 1,280 | 84.10% | 22 |
| 3 | 1,522 | 1,280 | 84.10% | 19 |

All three responses finished normally. The three offline tests passed: stable prefix across different tasks/reversed file arguments, correct minimal inclusive excerpts, and rejection of invalid ranges/outside files/reserved algorithm categories. No paid automatic warm-up or retry mechanism was added.

Cache-hit rate applies to input tokens, not total cost or output generation. Different file sets, edits to early context, provider eviction or persistence delays can reduce hits. The current [DeepSeek cache rules](https://api-docs.deepseek.com/guides/kv_cache/) describe automatic prefix persistence and best-effort matching. These measured results do not guarantee future hit rates.

Prefer minimal relevant excerpts, related operations in one bounded task, identical necessary context when reused, and compact patches rather than complete file rewrites. The project AGENTS.md records these rules. CLI calls use the updated code immediately; a running MCP server must reload before it picks up the changed code/schema.
