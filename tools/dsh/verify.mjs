// End-to-end MCP client check: keeps stdio open until the bounded task settles.
import { spawn } from 'node:child_process';
import readline from 'node:readline';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const directory = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(directory, '../..');
const child = spawn('powershell.exe', ['-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.join(directory, 'launch.ps1')], { cwd: root, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
const pending = new Map(); let seq = 0;
const lines = readline.createInterface({ input: child.stdout, crlfDelay: Infinity });
lines.on('line', line => {
  const message = JSON.parse(line); const item = pending.get(message.id);
  if (!item) return;
  pending.delete(message.id); clearTimeout(item.timer);
  if (message.error) item.reject(new Error(JSON.stringify(message.error))); else item.resolve(message.result);
});
child.stderr.on('data', data => process.stderr.write(data));
child.on('exit', code => { for (const item of pending.values()) { clearTimeout(item.timer); item.reject(new Error(`Bridge exited: ${code}`)); } pending.clear(); });
function rpc(method, params) {
  const id = ++seq;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { pending.delete(id); reject(new Error(`${method} timed out`)); }, 180000);
    pending.set(id, { resolve, reject, timer });
    child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
  });
}

try {
  const initialized = await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'luminas-verification', version: '1' } });
  assert.equal(initialized.serverInfo.name, 'luminas-dsh');
  child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');
  const catalog = await rpc('tools/list', {});
  assert.ok(catalog.tools.some(tool => tool.name === 'execute_task'));
  const rejected = await rpc('tools/call', { name: 'execute_task', arguments: { category: 'core_algorithm', task: 'Must fail before any model call' } });
  assert.equal(rejected.isError, true);
  const previous = JSON.parse(await fs.readFile(path.join(root, '.tools/dsh-smoke/response.json'), 'utf8'));
  const response = await rpc('tools/call', { name: 'execute_task', arguments: {
    category: 'test', session_id: previous.sessionId, timeout_seconds: 90, max_tool_calls: 4,
    task: '继续刚才的接入验证。只允许读取 .tools/dsh-smoke/result.txt，禁止写文件或读取其他文件。用 pwsh 实际运行：$v = (Get-Content -Raw -Encoding UTF8 .tools/dsh-smoke/result.txt).Trim(); if ($v -cne "LuminaS") { throw "Mismatch" }; "PASS"。最后只报告 PASS/FAIL 和命令，不重复旧任务报告。'
  } });
  assert.ok(!response.isError, response.content[0].text);
  const result = JSON.parse(response.content[0].text);
  assert.equal(result.sessionId, previous.sessionId);
  assert.equal(result.status, 'completed', JSON.stringify(result));
  assert.ok(result.tools.length > 0);
  assert.match(result.answer, /PASS/);
  const status = JSON.parse((await rpc('tools/call', { name: 'task_status', arguments: { session_id: result.sessionId } })).content[0].text);
  assert.equal(status.status, 'completed');
  const actual = (await fs.readFile(path.join(root, '.tools/dsh-smoke/result.txt'), 'utf8')).trim();
  assert.equal(actual, 'LuminaS');
  const report = { mcp_initialize: true, tool_catalog: catalog.tools.map(tool => tool.name), reserved_category_rejected: true,
    resumed_session: result.sessionId, live_task_completed: true, tools: result.tools, answer: result.answer,
    status_checked: true, output_file_independently_verified: true, timestamp: new Date().toISOString() };
  await fs.writeFile(path.join(root, '.tools/dsh-smoke/mcp-verification.json'), JSON.stringify(report, null, 2) + '\n');
  process.stdout.write(JSON.stringify(report, null, 2) + '\n');
} catch (error) { process.stderr.write(error.message + '\n'); process.exitCode = 1; }
finally {
  child.stdin.end();
  const timer = setTimeout(() => child.kill(), 15000);
  await new Promise(resolve => { if (child.exitCode !== null) resolve(); else child.once('exit', resolve); });
  clearTimeout(timer);
}
