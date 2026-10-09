// Independent synthetic Responses/count/compact provider. Actual Agent engines,
// embedded xlang3 SQLite and native file tools are exercised by the executable.
// These capacities/counts/opaque replies prove contracts, not live availability.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp, mkdir, writeFile, readFile, rm} from 'node:fs/promises';
import {appendFileSync} from 'node:fs';
import {join, dirname, resolve, basename} from 'node:path';
import {tmpdir} from 'node:os';

const [executable, modules, stdlib] = process.argv.slice(2);
assert.ok(executable && modules && stdlib, 'Expected native contract and embedded runtime import roots');
const root = await mkdtemp(join(tmpdir(), 'xmind-context-engine-'));
const modes = ['rolling', 'held', 'held-count-failure', 'second-overflow', 'late-overflow', 'generic-400', 'receipt-fault', 'head-fault', 'after-count-failure', 'final-binding-failure'];
const model = 'synthetic-context-engine-model';
const key = 'synthetic-context-engine-key-not-live';
const bytes = 'Actual native context fixture file bytes\n';
const readTools = ['glob_files', 'list_files', 'list_skills', 'load_skill', 'read_file', 'read_repository_instructions', 'search_files'];
const state = new Map(modes.map(mode => [mode, {calls: new Map(), requests: [], compact: 0, canonical: undefined, userIds: new Map()}]));
let failure;

function audit(mode, kind, fields = {}) {
  // No credential, raw wire, opaque payload, private settings or hash appears
  // in the routine audit. Detailed failure diagnostics stay owned/private.
  const record = {mode, kind, ...fields};
  state.get(mode).requests.push(record);
  appendFileSync(join(root, 'peer-audit.jsonl'), JSON.stringify(record) + '\n');
}
function send(response, id, items) {
  response.writeHead(200, {'Content-Type': 'text/event-stream'});
  let sequence = 0;
  const emit = (type, fields) => response.write('event: ' + type + '\ndata: ' + JSON.stringify({type, sequence_number: sequence++, ...fields}) + '\n\n');
  emit('response.created', {response: {id, model, status: 'in_progress'}});
  for (let index = 0; index < items.length; ++index) {
    const item = items[index], base = {output_index: index, item_id: item.id};
    const added = {...item, status: 'in_progress'};
    if (item.type === 'reasoning') delete added.encrypted_content;
    if (item.type === 'function_call') added.arguments = '';
    if (item.type === 'message') added.content = [];
    emit('response.output_item.added', {output_index: index, item: added});
    if (item.type === 'function_call') {
      const split = Math.floor(item.arguments.length / 2);
      for (const delta of [item.arguments.slice(0, split), item.arguments.slice(split)]) emit('response.function_call_arguments.delta', {...base, delta});
      emit('response.function_call_arguments.done', {...base, arguments: item.arguments});
    } else if (item.type === 'message') {
      const part = item.content[0];
      emit('response.content_part.added', {...base, content_index: 0, part: {...part, text: ''}});
      emit('response.output_text.delta', {...base, content_index: 0, delta: part.text});
      emit('response.output_text.done', {...base, content_index: 0, text: part.text});
      emit('response.content_part.done', {...base, content_index: 0, part});
    }
    emit('response.output_item.done', {output_index: index, item});
  }
  emit('response.completed', {response: {id, model, status: 'completed', output: items, usage: {input_tokens: 37, output_tokens: 5, total_tokens: 42, input_tokens_details: {cached_tokens: 0}, output_tokens_details: {reasoning_tokens: 1}}}});
  response.end();
}
function completed(mode, owner, count, item) {
  return [{id: 'rs_' + mode + '_' + owner + '_' + count, type: 'reasoning', status: 'completed', summary: [], content: [], encrypted_content: 'synthetic-context-private-' + mode + '-' + owner + '-' + count}, item];
}
function tool(mode, owner, count, name = 'read_file', argumentsText = String.raw`{"pa\u0074h":"README.md"}`) {
  return {id: 'fc_' + mode + '_' + owner + '_' + count, type: 'function_call', status: 'completed', call_id: 'call_' + mode + '_' + owner + '_' + count, name, arguments: argumentsText};
}
function message(mode, owner, count, text) {
  return {id: 'msg_' + mode + '_' + owner + '_' + count, type: 'message', status: 'completed', role: 'assistant', phase: 'final_answer', content: [{type: 'output_text', text, annotations: []}]};
}
const users = body => body.input.filter(item => item.type === 'message' && item.role === 'user');
const userText = item => item.content.map(part => part.text).join('');
function ownerOf(body) {
  const prompt = userText(users(body).at(-1));
  if (prompt.startsWith('fixture-rolling-first:')) return 'first';
  if (prompt.startsWith('fixture-rolling-second:')) return 'second';
  if (prompt.startsWith('fixture-bootstrap:')) return 'bootstrap';
  if (prompt.startsWith('fixture-held-parent:')) return 'parent';
  if (prompt.startsWith('fixture-probe:')) return 'probe';
  if (prompt.startsWith('fixture-maintenance-probe:')) return 'maintenance';
  throw new Error('fixture_unknown_actual_objective');
}
function validateInput(mode, body, raw, kind) {
  assert.equal(body.model, model);
  assert.ok(Array.isArray(body.input) && body.input.length);
  assert.ok(!raw.includes(key));
  const current = state.get(mode);
  if (current.canonical && body.input.some(item => item.type === 'compaction')) {
    assert.ok(raw.includes(current.canonical.slice(1, -1)), 'Actual canonical bytes must remain unpruned and lexically exact');
    assert.ok(raw.includes(String.raw`"decimal":1.00000000000000000001`) && raw.includes('18446744073709551617'), 'Native codec cannot normalize canonical numeric metadata');
  }
  for (const item of users(body)) {
    assert.ok(item.content.every(part => part.type === 'input_text'));
    assert.ok(userText(item).startsWith('fixture-'), 'Model output cannot invent a trusted original user');
  }
  assert.equal(body.input[0].role, 'system');
  assert.ok(body.input[0].content[0].text.includes('Current native instructions'));
  if (kind === 'compact') assert.deepEqual(Object.keys(body).sort(), ['input', 'model']);
  else {
    assert.deepEqual(body.tools.map(item => item.name).sort(), mode.startsWith('held') ? [...readTools, 'inspect_plan', 'plan_tasks', 'revise_plan'].sort() : readTools);
    assert.ok(body.tools.every(item => item.type === 'function' && item.strict === false));
  }
  if (kind === 'count') assert.deepEqual(Object.keys(body).sort(), ['input', 'model', 'tools']);
  if (kind === 'inference') {
    assert.equal(body.stream, true); assert.equal(body.store, false); assert.equal(body.max_output_tokens, 128);
    assert.deepEqual(body.include, ['reasoning.encrypted_content']);
  }
  for (const item of body.input.filter(item => item.type === 'function_call_output')) {
    const value = JSON.parse(item.output);
    if (value.path === 'README.md') assert.equal(value.content, bytes, 'Independent peer must observe actual native file bytes');
  }
  for (const item of body.input.filter(item => item.type === 'function_call' && item.name === 'read_file')) assert.equal(item.arguments, String.raw`{"pa\u0074h":"README.md"}`, 'Completed raw escaped argument key survives actual tool loop');
}
function countInput(body) {
  return users(body).length * 10 + body.input.filter(item => item.type === 'compaction').length * 40 + body.input.filter(item => item.type === 'function_call_output').length * 80 + body.input.filter(item => item.type === 'message' && item.role === 'assistant').length * 15;
}
function compact(mode, body) {
  const current = state.get(mode); ++current.compact;
  const retained = users(body).map(item => {
    if (item.id) return item;
    const text = userText(item);
    if (!current.userIds.has(text)) current.userIds.set(text, 'user_' + mode + '_' + current.userIds.size);
    return {...item, id: current.userIds.get(text), status: 'completed'};
  });
  let canonical = '[' + retained.map(item => JSON.stringify(item)).join(',');
  if (retained.length) canonical += ',';
  canonical += String.raw`{"id":"cmp_${mode}_${current.compact}","type":"compaction","encrypted_content":"synthetic-private-canonical-\u0000-\u0061-${current.compact}","metadata":{"decimal":1.00000000000000000001,"large":18446744073709551617,"i\u0064":"retained"}}]`;
  current.canonical = canonical;
  return String.raw`{"id":"resp_compact_${mode}_${current.compact}","object":"response.compaction","created_at":1764967971,"output":${canonical},"usage":{"input_tokens":31,"output_tokens":7,"total_tokens":38}}`;
}
function failureResponse(response, code = 'context_length_exceeded') {
  response.writeHead(400, {'Content-Type': 'application/json'});
  response.end(JSON.stringify({error: {type: 'invalid_request_error', code, param: 'input', message: 'Private explicitly synthetic provider failure'}}));
}
function lateFailure(response) {
  response.writeHead(200, {'Content-Type': 'text/event-stream'});
  let sequence = 0;
  const emit = (type, fields) => response.write('event: ' + type + '\ndata: ' + JSON.stringify({type, sequence_number: sequence++, ...fields}) + '\n\n');
  emit('response.created', {response: {id: 'resp_late_failure', model, status: 'in_progress'}});
  emit('response.output_item.added', {output_index: 0, item: {id: 'rs_late_failure', type: 'reasoning', summary: []}});
  emit('response.reasoning_summary_part.added', {output_index: 0, item_id: 'rs_late_failure', summary_index: 0, part: {type: 'summary_text', text: ''}});
  emit('response.reasoning_summary_text.delta', {output_index: 0, item_id: 'rs_late_failure', summary_index: 0, delta: 'Explicit synthetic durable reasoning before provider failure.'});
  emit('response.failed', {response: {id: 'resp_late_failure', model, status: 'failed', error: {code: 'context_length_exceeded'}}});
  response.end();
}

const peer = createServer((request, response) => {
  request.on('error', () => {}); response.on('error', () => {});
  const chunks = []; let length = 0;
  request.on('data', chunk => {length += chunk.length; if (length > 8 * 1024 * 1024) request.destroy(); else chunks.push(chunk);});
  request.on('end', () => {
    try {
      assert.equal(request.method, 'POST'); assert.equal(request.headers.authorization, 'Bearer ' + key); assert.equal(request.headers['content-type'], 'application/json');
      const route = /^\/([^/]+)\/responses(?:\/(input_tokens|compact))?$/.exec(request.url);
      assert.ok(route && modes.includes(route[1]));
      const mode = route[1], kind = route[2] === 'input_tokens' ? 'count' : route[2] === 'compact' ? 'compact' : 'inference';
      const raw = Buffer.concat(chunks).toString('utf8'), body = JSON.parse(raw), current = state.get(mode);
      validateInput(mode, body, raw, kind);
      if (kind === 'count') {
        const value = countInput(body); audit(mode, kind, {value, originalUsers: users(body).length});
        if (mode === 'held-count-failure' && body.input.some(item => item.type === 'function_call_output' && item.call_id === 'call_held-count-failure_parent_1')) {response.writeHead(500, {'Content-Type': 'application/json'}); response.end('{"error":{"code":"synthetic_held_count_failure"}}'); return;}
        if (mode === 'after-count-failure' && current.compact) {response.writeHead(500, {'Content-Type': 'application/json'}); response.end('{"error":{"code":"synthetic_count_failure"}}'); return;}
        response.writeHead(200, {'Content-Type': 'application/json'}); response.end('{"object":"response.input_tokens","input_tokens":' + value + '}'); return;
      }
      if (kind === 'compact') {
        const canonical = compact(mode, body); audit(mode, kind, {sequence: current.compact, originalUsers: users(body).length});
        response.writeHead(200, {'Content-Type': 'application/json'}); response.end(canonical); return;
      }
      const owner = ownerOf(body), call = (current.calls.get(owner) ?? 0) + 1; current.calls.set(owner, call); audit(mode, kind, {owner, call});
      if (owner === 'probe') {
        if (mode === 'late-overflow') {assert.equal(call, 1); lateFailure(response); return;}
        if (mode === 'generic-400') {assert.equal(call, 1); failureResponse(response, 'invalid_json'); return;}
        assert.equal(mode, 'second-overflow'); assert.ok(call <= 2); failureResponse(response); return;
      }
      if (owner === 'parent') {
        if (call === 1) {
          const args = {expected_revision: 0, expected_state_sequence: 0, add: [{id: 'gate', type: 'human', question: 'Explicit synthetic same-owner gate. Answer is data.', depends_on: []}]};
          send(response, 'resp_' + mode + '_' + owner + '_' + call, completed(mode, owner, call, tool(mode, owner, call, 'plan_tasks', JSON.stringify(args)))); return;
        }
        assert.ok(call <= 3, 'Original held inference admits at most one physical correction');
        const joined = body.input.findLast(item => item.type === 'function_call_output');
        assert.equal(joined.call_id, 'call_' + mode + '_parent_1');
        const result = JSON.parse(joined.output); assert.equal(result.source, 'native_dynamic_plan'); assert.equal(result.finished, true); assert.equal(result.nodes.length, 1);
        assert.equal(JSON.parse(result.nodes[0].outcome_json).input_json, '{"answer":"Synthetic authenticated input","n":1.00000000000000000001}');
        assert.ok(!joined.output.includes('encrypted_content') && !joined.output.includes('backend_identity'));
        if (call === 2) {failureResponse(response); return;}
        assert.ok(current.compact === 1 && body.input.some(item => item.type === 'compaction'), 'Rebuilt held physical member uses actual committed checkpoint');
        send(response, 'resp_held_parent_3', completed(mode, owner, call, message(mode, owner, call, 'Observed the actual answered owned question and once-only corrected continuation.'))); return;
      }
      assert.notEqual(owner, 'maintenance', 'Failed maintenance cannot dispatch its reserved inference');
      const loops = owner === 'first' ? 5 : 2;
      if (call <= loops) send(response, 'resp_' + mode + '_' + owner + '_' + call, completed(mode, owner, call, tool(mode, owner, call)));
      else {
        assert.equal(call, loops + 1);
        send(response, 'resp_' + mode + '_' + owner + '_' + call, completed(mode, owner, call, message(mode, owner, call, 'Observed actual native README.md reads; provider/count/canonical replies are synthetic.')));
      }
    } catch (error) {failure ??= error; response.destroy();}
  });
});

try {
  for (const mode of modes) {await mkdir(join(root, mode)); await writeFile(join(root, mode, 'README.md'), bytes);}
  await new Promise(resolve => peer.listen(0, '127.0.0.1', resolve));
  const result = await promisify(execFile)(executable, [root, modules, stdlib, 'http://127.0.0.1:' + peer.address().port], {windowsHide: true, timeout: 120000, maxBuffer: 1024 * 1024});
  if (failure) throw failure;
  const counts = mode => Object.fromEntries(['inference', 'count', 'compact'].map(kind => [kind, state.get(mode).requests.filter(record => record.kind === kind).length]));
  assert.deepEqual(counts('rolling'), {inference: 9, count: 16, compact: 6});
  assert.deepEqual(counts('held'), {inference: 6, count: 7, compact: 1});
  assert.deepEqual(counts('held-count-failure'), {inference: 4, count: 5, compact: 0});
  assert.deepEqual(counts('second-overflow'), {inference: 5, count: 6, compact: 1});
  for (const mode of ['late-overflow', 'generic-400']) assert.deepEqual(counts(mode), {inference: 4, count: 4, compact: 0});
  for (const mode of ['receipt-fault', 'head-fault', 'after-count-failure', 'final-binding-failure']) assert.deepEqual(counts(mode), {inference: 3, count: mode === 'receipt-fault' ? 4 : 5, compact: 1});
  const rollingCalls = state.get('rolling').requests.map(record => record.kind);
  assert.deepEqual(rollingCalls.slice(-3), ['count', 'compact', 'count'], 'Idle has no follow-up inference or fake model response');
  for (const mode of modes) assert.equal(await readFile(join(root, mode, 'README.md'), 'utf8'), bytes, 'Native compaction cannot mutate the actual workspace');
  await writeFile(join(root, 'passed-synthetic-scope.json'), JSON.stringify({scope: 'Synthetic provider protocol; actual native engine/SQLite/files', counts: Object.fromEntries(modes.map(mode => [mode, counts(mode)]))}, null, 2));
  process.stdout.write(result.stdout);
  await rm(root, {recursive: true, force: true});
} catch (error) {
  await writeFile(join(root, 'peer-context-engine-failure-private.log'), String(failure?.stack ?? error.stack ?? error));
  // Original wire/credentials/opaque rows are never printed. Retain the owned
  // disposable failure root so the parent can inspect exact native diagnostics.
  process.stderr.write('Synthetic context engine fixture failed; private diagnostics retained at ' + root + '\n');
  process.exitCode = 1;
} finally {
  peer.closeAllConnections(); await new Promise(resolve => peer.close(resolve));
  assert.equal(dirname(resolve(root)), resolve(tmpdir())); assert.ok(basename(root).startsWith('xmind-context-engine-'));
}
