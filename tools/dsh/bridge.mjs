import { spawn } from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';

const directory = path.dirname(fileURLToPath(import.meta.url));
const root = await fs.realpath(path.resolve(directory, '../..'));
const home = path.join(root, '.tools/dsh-home');
const install = process.env.LUMINAS_DSH_INSTALL ?? path.join(process.env.LOCALAPPDATA, 'Programs/DeepSeek Harness');
const categories = ['documentation', 'translation', 'bulk_transform', 'test', 'simple_code', 'review'];
const records = new Map();
const pending = new Map();
let child, starting, seq = 0, closing = false, executionBusy = false;
const validSessionId = id => typeof id === 'string' && /^(?:session-)?[a-fA-F0-9-]{36,44}$/.test(id);
const scrub = value => {
  const text = typeof value === 'string' ? value : JSON.stringify(value);
  const key = process.env.DEEPSEEK_API_KEY;
  return (key ? text.split(key).join('[REDACTED]') : text).replace(/\bsk-[A-Za-z0-9_-]{16,}\b/g, '[REDACTED]');
};
const write = message => child.stdin.write(JSON.stringify(message) + '\n');
function cancelSession(sessionId) {
  if (child?.stdin?.writable && !child.stdin.destroyed) write({ jsonrpc: '2.0', method: 'session/cancel', params: { sessionId } });
}

function rpc(method, params, timeout = 30000) {
  const id = ++seq;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { pending.delete(id); reject(new Error(`ACP ${method} timed out`)); }, timeout);
    pending.set(id, { resolve, reject, timer });
    write({ jsonrpc: '2.0', id, method, params });
  });
}

function onMessage(message) {
  if (message.method === 'session/request_permission' && message.id !== undefined) {
    // Normal workspace writes use the native policy. Never approve an escalation automatically.
    const reject = message.params?.options?.find(option => option.kind === 'reject_once');
    write({ jsonrpc: '2.0', id: message.id, result: { outcome: reject ? { outcome: 'selected', optionId: reject.optionId } : { outcome: 'cancelled' } } });
    const record = records.get(message.params?.sessionId);
    if (record) record.permissions.push({ title: scrub(message.params?.toolCall?.title ?? 'Permission requested'), decision: 'rejected' });
    return;
  }
  if (message.method === 'session/update') {
    const record = records.get(message.params?.sessionId);
    if (!record) return;
    const update = message.params.update;
    if (update.sessionUpdate === 'agent_message_chunk' && update.content?.type === 'text') record.answer += scrub(update.content.text);
    if (update.sessionUpdate === 'tool_call') {
      record.tools.push({ id: update.toolCallId, title: scrub(update.title ?? ''), kind: update.kind, status: update.status });
      if (record.tools.length >= record.maxToolCalls && !record.cancelRequested) {
        record.cancelRequested = true;
        record.limit = 'tool-call limit reached';
        cancelSession(record.sessionId);
      }
    }
    if (update.sessionUpdate === 'tool_call_update') {
      const tool = record.tools.find(item => item.id === update.toolCallId);
      if (tool) { tool.status = update.status ?? tool.status; tool.locations = update.locations ?? tool.locations; }
    }
    if (/usage/.test(update.sessionUpdate)) record.contextUsage = update;
    return;
  }
  if (message.id !== undefined && pending.has(message.id)) {
    const item = pending.get(message.id); pending.delete(message.id); clearTimeout(item.timer);
    if (message.error) item.reject(new Error(scrub(message.error)));
    else item.resolve(message.result);
  } else if (message.method && message.id !== undefined) {
    write({ jsonrpc: '2.0', id: message.id, error: { code: -32601, message: 'Client method unsupported' } });
  }
}

async function boot() {
  if (starting) return starting;
  starting = (async () => {
    await fs.mkdir(home, { recursive: true });
    const exe = path.join(install, 'DeepSeek Harness.exe');
    await fs.access(exe);
    child = spawn(exe, ['--expose-internals', path.join(install, 'resources/app.asar/dsh/node_modules/@deepseek-ai/dsh-desktop-host/lib/cli.js'),
      '--profile', 'acp', '--patch', path.join(directory, 'overlay.yml')], {
      cwd: root, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'],
      env: { ...process.env, ELECTRON_RUN_AS_NODE: '1', DSH_HOME: home, DSH_PERMISSION_MODE: 'workspace-write' }
    });
    let stderr = '';
    child.stderr.on('data', chunk => { stderr = (stderr + scrub(chunk.toString())).slice(-12000); });
    const fail = error => {
      for (const item of pending.values()) { clearTimeout(item.timer); item.reject(error); }
      pending.clear(); starting = undefined;
    };
    child.on('error', error => fail(error));
    child.on('exit', code => fail(new Error(`DSH exited (${code}). ${stderr}`)));
    const lines = readline.createInterface({ input: child.stdout, crlfDelay: Infinity });
    lines.on('line', line => { try { onMessage(JSON.parse(line)); } catch { /* non-protocol startup output is never forwarded to MCP */ } });
    return await rpc('initialize', { protocolVersion: 1, clientCapabilities: {}, clientInfo: { name: 'luminas-codex-mcp', version: '1.0.0' } }, 60000);
  })();
  return starting;
}

async function save(record) {
  await fs.mkdir(path.join(root, '.tools/dsh-bridge'), { recursive: true });
  await fs.writeFile(path.join(root, '.tools/dsh-bridge', `${record.sessionId}.json`), scrub(record) + '\n');
}

export async function execute(input) {
  if (!categories.includes(input.category)) throw new Error('Unsupported category; core algorithms/model development remain with GPT');
  if (typeof input.task !== 'string' || !input.task.trim() || input.task.length > 30000) throw new Error('A bounded task up to 30000 characters is required');
  const timeout = input.timeout_seconds ?? 600;
  const maxToolCalls = input.max_tool_calls ?? 20;
  if (!Number.isInteger(timeout) || timeout < 10 || timeout > 1800 || !Number.isInteger(maxToolCalls) || maxToolCalls < 1 || maxToolCalls > 100) throw new Error('Invalid execution limits');
  await boot();
  // One writer at a time because sessions share the LuminaS working directory.
  if (executionBusy) throw new Error('Another DSH task is running in this workspace');
  executionBusy = true;
  try {
  let sessionId = input.session_id;
  if (sessionId) {
    if (!validSessionId(sessionId)) throw new Error('Invalid session_id');
    if (!records.has(sessionId)) {
      const persisted = JSON.parse(await fs.readFile(path.join(root, '.tools/dsh-bridge', `${sessionId}.json`), 'utf8'));
      if (persisted.workspace !== root) throw new Error('Session workspace mismatch');
      await rpc('session/resume', { sessionId, cwd: root, mcpServers: [] });
    }
  } else sessionId = (await rpc('session/new', { cwd: root, mcpServers: [] })).sessionId;
  const record = { sessionId, workspace: root, category: input.category, status: 'running', answer: '', tools: [], permissions: [], maxToolCalls, started_at: new Date().toISOString() };
  records.set(sessionId, record);
  const policy = 'Codex is the decision and review layer; you are the LuminaS execution layer. Execute only this bounded task in the current workspace. Core image algorithms, numerical/color science, model development and architecture remain with GPT. Do not delegate to other agents, call the DeepSeek worker, modify AGENTS.md or bridge configuration, install dependencies, recharge, publish, or read credentials/private photos. Read only needed files/ranges. Make minimal changes, run the requested relevant checks, and stop. For missing scope or permission, report the blocker. Report changes, actual verification commands/results, and leftovers concisely. Task-specific allowed paths and checks below govern this task; do not expand scope.';
  const timer = setTimeout(() => { record.cancelRequested = true; record.limit = 'time limit reached'; cancelSession(sessionId); }, timeout * 1000);
  try {
    const result = await rpc('session/prompt', { sessionId, prompt: [{ type: 'text', text: `${policy}\n\nTASK:\n${scrub(input.task)}` }] }, timeout * 1000 + 30000);
    record.stopReason = result.stopReason;
    record.status = record.cancelRequested || result.stopReason !== 'end_turn' ? 'stopped' : 'completed';
  } catch (error) {
    record.status = 'failed'; record.error = scrub(error.message);
    cancelSession(sessionId);
  } finally { clearTimeout(timer); record.finished_at = new Date().toISOString(); await save(record); }
  return record;
  } finally { executionBusy = false; }
}

const toolSchema = { type: 'object', properties: {
  category: { type: 'string', enum: categories }, task: { type: 'string', description: 'Bounded task with allowed paths, acceptance criteria and exact checks. Core algorithms/model development stay with GPT.' },
  session_id: { type: 'string', description: 'Resume/continue the returned session for related follow-up work and append-only context reuse.' },
  timeout_seconds: { type: 'integer', minimum: 10, maximum: 1800 }, max_tool_calls: { type: 'integer', minimum: 1, maximum: 100 }
}, required: ['category', 'task'], additionalProperties: false };
const idSchema = { type: 'object', properties: { session_id: { type: 'string' } }, required: ['session_id'], additionalProperties: false };
const tools = [
  { name: 'execute_task', description: 'Run/continue a bounded LuminaS task in DeepSeek Harness (Flash). Can read/write project files and run checks; GPT reviews results. Serial workspace execution, default 10-minute/20-tool-call bounds.', inputSchema: toolSchema },
  { name: 'task_status', description: 'Inspect current or persisted task result. No model call.', inputSchema: idSchema },
  { name: 'cancel_task', description: 'Cancel a running DSH session.', inputSchema: idSchema }
];

async function status(id) {
  if (!validSessionId(id)) throw new Error('Invalid session_id');
  return records.get(id) ?? JSON.parse(await fs.readFile(path.join(root, '.tools/dsh-bridge', `${id}.json`), 'utf8'));
}
async function call(name, input) {
  if (name === 'execute_task') return execute(input);
  if (name === 'task_status') return status(input.session_id);
  if (name === 'cancel_task') {
    const record = await status(input.session_id);
    if (record.status !== 'running') return { sessionId: record.sessionId, status: record.status };
    record.cancelRequested = true;
    cancelSession(input.session_id); return { sessionId: input.session_id, cancellation_requested: true };
  }
  throw new Error('Unknown tool');
}
async function mcp(request) {
  if (request.id === undefined) return;
  let result;
  if (request.method === 'initialize') result = { protocolVersion: request.params.protocolVersion, capabilities: { tools: {} }, serverInfo: { name: 'luminas-dsh', version: '1.0.0' } };
  else if (request.method === 'ping') result = {};
  else if (request.method === 'tools/list') result = { tools };
  else if (request.method === 'tools/call') {
    try { result = { content: [{ type: 'text', text: scrub(await call(request.params.name, request.params.arguments ?? {})) }] }; }
    catch (error) { result = { isError: true, content: [{ type: 'text', text: scrub(error.message) }] }; }
  } else return { jsonrpc: '2.0', id: request.id, error: { code: -32601, message: 'Method not found' } };
  return { jsonrpc: '2.0', id: request.id, result };
}
async function shutdown() {
  if (closing) return; closing = true;
  for (const record of records.values()) {
    if (record.status === 'running') cancelSession(record.sessionId);
    await rpc('session/close', { sessionId: record.sessionId }, 10000).catch(() => {});
  }
  if (child && child.exitCode === null) {
    child.stdin.end();
    const timer = setTimeout(() => child.kill(), 5000);
    await new Promise(resolve => { if (child.exitCode !== null) resolve(); else child.once('exit', resolve); });
    clearTimeout(timer);
  }
}
process.on('SIGINT', () => { void shutdown().then(() => process.exit(0)); });
process.on('SIGTERM', () => { void shutdown().then(() => process.exit(0)); });

if (process.argv[2] === '--mcp') {
  const lines = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  const active = new Set();
  for await (const line of lines) {
    const work = (async () => {
      try { const reply = await mcp(JSON.parse(line)); if (reply) process.stdout.write(JSON.stringify(reply) + '\n'); }
      catch (error) { process.stdout.write(JSON.stringify({ jsonrpc: '2.0', id: null, error: { code: -32700, message: scrub(error.message) } }) + '\n'); }
    })(); active.add(work); void work.finally(() => active.delete(work));
  }
  // EOF means the host has disconnected; cancel work before draining responses.
  await shutdown(); await Promise.allSettled(active);
} else {
  try {
    if (process.argv[2] === 'doctor') {
      const initialized = await boot();
      const session = await rpc('session/new', { cwd: root, mcpServers: [] });
      await rpc('session/close', { sessionId: session.sessionId });
      process.stdout.write(scrub({ initialized, session, workspace: root, home, model: 'deepseek-flash', paid_prompts: 0 }) + '\n');
    } else {
      const input = JSON.parse((await fs.readFile(process.argv[2], 'utf8')).replace(/^\uFEFF/, ''));
      process.stdout.write(scrub(await call(input.tool ?? 'execute_task', input.arguments ?? input)) + '\n');
    }
  } catch (error) { process.stderr.write(scrub(error.message) + '\n'); process.exitCode = 1; }
  finally { await shutdown(); }
}
