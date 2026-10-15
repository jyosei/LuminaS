import test from 'node:test';
import assert from 'node:assert/strict';
import { buildRequest } from './worker.mjs';

test('changing task and file argument order retain identical source prefix', async () => {
  const a = await buildRequest({ category: 'summary', task: 'Task one', files: ['README.md', 'src/core/presets.h'] });
  const b = await buildRequest({ category: 'review', task: 'Task two', files: ['src/core/presets.h', 'README.md'] });
  assert.equal(a.telemetry.context_sha256, b.telemetry.context_sha256);
  assert.notEqual(a.telemetry.request_sha256, b.telemetry.request_sha256);
  assert.equal(a.body.messages[0].content, b.body.messages[0].content);
  assert.equal(a.body.messages[1].content.slice(0, -'Task one'.length), b.body.messages[1].content.slice(0, -'Task two'.length));
  assert.ok(a.body.messages[1].content.indexOf('--- FILE:') < a.body.messages[1].content.indexOf('TASK:\n'));
});

test('inclusive line excerpts shrink context and do not include other lines', async () => {
  const whole = await buildRequest({ category: 'summary', task: 'Check', files: ['README.md'] });
  const excerpt = await buildRequest({ category: 'summary', task: 'Check', files: [{ path: 'README.md', start_line: 1, end_line: 1 }] });
  assert.ok(excerpt.telemetry.context_bytes < whole.telemetry.context_bytes);
  assert.ok(excerpt.body.messages[1].content.includes('--- FILE: README.md:1-1 ---\n# LuminaS\n\nTASK:'));
  assert.ok(!excerpt.body.messages[1].content.includes('历史设计草案'));
});

test('invalid ranges, external files and reserved work fail before network calls', async () => {
  await assert.rejects(buildRequest({ category: 'summary', task: 'Check', files: [{ path: 'README.md', start_line: 3, end_line: 1 }] }), /line range/);
  await assert.rejects(buildRequest({ category: 'summary', task: 'Check', files: ['../../.codex/config.toml'] }), /outside/);
  await assert.rejects(buildRequest({ category: 'core_algorithm', task: 'Check' }), /Unsupported/);
});
