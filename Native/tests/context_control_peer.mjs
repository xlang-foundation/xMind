// Independent synthetic provider for actual native HTTP/AgentService controls.
// Real transport, authentication, SQLite, clocks and lifecycle belong to C++.
// No user provider key, live YAML, current preview or product runtime is read.
import assert from 'node:assert/strict';
import {createServer,request as controllerRequest} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp, mkdir, writeFile, readFile, rm} from 'node:fs/promises';
import {appendFileSync, existsSync} from 'node:fs';
import {join, dirname, resolve, basename} from 'node:path';
import {tmpdir} from 'node:os';
const [binary, modules, stdlib] = process.argv.slice(2);
assert.ok(binary && modules && stdlib, 'Expected compiled native fixture and embedded import roots');
const fixture = await mkdtemp(join(tmpdir(), 'xmind-context-control-'));
const modes = ['idle', 'queued-cancel', 'active-finish', 'model-race', 'shutdown', 'uncertain'];
const modelA = 'synthetic-control-a', modelB = 'synthetic-control-b';
const key = 'synthetic-context-control-provider-key-not-live';
const controllerToken = 'synthetic-context-control-access-token-32-bytes';
const execute = promisify(execFile), state = new Map();
let failure, nativeFailure, successful = false;
const users = body => body.input.filter(item => item.type === 'message' && item.role === 'user');
const text = item => item.content.map(part => part.text).join('');
for (const mode of modes) state.set(mode, {counts: new Map(), originals: new Map(), windows: new Map(), users: new Map()});
function audit(mode, scope, model, kind) {
  const current = state.get(mode), identity = `${scope}:${model}:${kind}`;
  const ordinal = (current.counts.get(identity) ?? 0) + 1;
  current.counts.set(identity, ordinal);
  // Fixed scope/counter metadata only; no request/receipt, opaque hashes or key.
  appendFileSync(join(fixture, mode, 'peer-audit.jsonl'), JSON.stringify({kind, scope, model, ordinal}) + '\n');
  return ordinal;
}
function send(response, mode, scope, model, ordinal) {
  const id = `${mode}_${scope}_${model}_${ordinal}`;
  const reasoning = {id: 'rs_' + id, type: 'reasoning', status: 'completed', summary: [], content: [], encrypted_content: 'synthetic-private-opaque-' + id};
  const message = {id: 'msg_' + id, type: 'message', role: 'assistant', status: 'completed', phase: null, content: [{type: 'output_text', text: 'Synthetic supplied answer for actual native context-control seed or active owner.', annotations: []}]};
  state.get(mode).originals.set(reasoning.id, reasoning);state.get(mode).originals.set(message.id, message);
  response.writeHead(200, {'Content-Type': 'text/event-stream'});let sequence = 0;
  const emit = (type, fields) => response.write(`event: ${type}\ndata: ${JSON.stringify({type, sequence_number: sequence++, ...fields})}\n\n`);
  emit('response.created', {response: {id: 'resp_' + id, model, status: 'in_progress'}});
  emit('response.output_item.added', {output_index: 0, item: {id: reasoning.id, type: 'reasoning', summary: []}});
  emit('response.output_item.done', {output_index: 0, item: reasoning});
  emit('response.output_item.added', {output_index: 1, item: {id: message.id, type: 'message', role: 'assistant', content: []}});
  const position = {output_index: 1, item_id: message.id, content_index: 0};
  emit('response.content_part.added', {...position, part: {type: 'output_text', text: ''}});
  emit('response.output_text.delta', {...position, delta: message.content[0].text});
  emit('response.output_text.done', {...position, text: message.content[0].text});
  emit('response.content_part.done', {...position, part: message.content[0]});
  emit('response.output_item.done', {output_index: 1, item: message});
  emit('response.completed', {response: {id: 'resp_' + id, model, status: 'completed', output: [reasoning, message], usage: {input_tokens: 37, output_tokens: 5, total_tokens: 42, output_tokens_details: {reasoning_tokens: 1}}}});
  response.end();
}
function untilFlag(response, mode, name, action) {
  const interval = setInterval(() => {
    if (existsSync(join(fixture, mode, name))) {clearInterval(interval);if (!response.destroyed) action();}
  }, 5);
  response.once('close', () => clearInterval(interval));
}
async function mark(mode, name) {await writeFile(join(fixture, mode, name), 'Actual native provider request reached the independent synthetic peer.\n', {flag: 'wx'});}
function rawController(port, method, path, expected) {
  return new Promise((resolveReply,rejectReply) => {
    const headers={Host:'127.0.0.1:'+port,Authorization:'Bearer '+controllerToken};
    const body=method==='POST'?JSON.stringify({id:'raw-query-rejected',expected_head_revision:0}):'';
    if(body){headers['Content-Type']='application/json';headers['Content-Length']=Buffer.byteLength(body);}
    // Node's explicit path is sent unchanged. In particular, '?' and malformed
    // percent escapes cannot be erased by cpp-httplib's client normalizer.
    const request=controllerRequest({hostname:'127.0.0.1',port,method,path,headers,agent:false},response=>{
      const chunks=[];let bytes=0;
      response.on('data',chunk=>{bytes+=chunk.length;if(bytes>65536){request.destroy(new Error('Synthetic controller response exceeded bound'));return;}chunks.push(chunk);});
      response.on('error',rejectReply);
      response.on('end',()=>{try{
        if(response.statusCode!==expected)throw new Error('Raw context query rejected unexpected status method='+method+' path='+path+' expected='+expected+' actual='+response.statusCode);
        const value=JSON.parse(Buffer.concat(chunks).toString('utf8'));
        if(expected===200){assert.equal(value.session_id,'owned');assert.equal(value.model_id,modelA);assert.equal(value.head_revision,0);assert.equal(value.checkpoint,null);}
        resolveReply();
      }catch(error){rejectReply(error);}});
    });
    request.setTimeout(2000,()=>request.destroy(new Error('Synthetic raw context query timed out')));
    request.on('error',rejectReply);request.end(body);
  });
}
function startRawQueryChecks() {
  let active=false,completed=false,finish;
  const done=new Promise(resolveDone=>{finish=resolveDone;});
  const timer=setInterval(async()=>{
    if(active||!existsSync(join(fixture,'idle','raw-query-checks-ready')))return;
    active=true;clearInterval(timer);let result='failed';
    try {
      const encoded=(await readFile(join(fixture,'idle','raw-controller-port.txt'),'utf8'));
      assert.match(encoded,/^[0-9]{1,5}$/);const port=Number(encoded);assert.ok(port>0&&port<=65535);
      const bad=[
        '?model_id='+modelA+'&model_id='+modelA,
        '?mod%65l_id='+modelA+'&model_id='+modelA,
        '?model_id='+modelA+'&model%5Fid='+modelA,
        '?model_id='+modelA+'&model_id='+modelB,
        '?','?model_id','?model_id=','?=value','?unknown=value',
        '?&model_id='+modelA,'?model_id='+modelA+'&','?model_id='+modelA+'&&',
        '?model_id=%','?model_id=%A','?model_id=%G1','?mod%GGel_id='+modelA,
        '?model_id='+modelA+'%00','?model_id='+modelA+'%0A','?model_id='+modelA+'%7F','?model_id=%FF',
        '?model_id=a+b','?model_id=a%20b','?model_id=a=b',
        '?model_id='+'a'.repeat(257),'?model_id='+'a'.repeat(1025),
      ];
      for(const path of ['/v1/sessions/owned/context','/v1/sessions/owned/context/requests/raw-query-absent'])
        for(const query of bad)await rawController(port,'GET',path+query,400);
      await rawController(port,'GET','/v1/sessions/owned/context?%6Dodel%5Fid=%73ynthetic-control-a',200);
      await rawController(port,'GET','/v1/sessions/owned/context/requests/raw-query-absent?%6Dodel%5Fid=%73ynthetic-control-a',404);
      for(const query of ['?','?=value','?&','?unknown=value','?model_id='+modelA])
        await rawController(port,'POST','/v1/sessions/owned/context/compact'+query,400);
      result='passed';completed=true;
    }catch(error){failure??=error;}
    finally {
      try{await writeFile(join(fixture,'idle','raw-query-result.txt'),result,{flag:'wx'});await mark('idle','raw-query-checks-completed');}
      catch(error){failure??=error;completed=false;}
      finally{finish(completed);}
    }
  },5);
  return {done,stop(){clearInterval(timer);if(!active)finish(false);}};
}
function compact(mode, scope, model, body) {
  const current = state.get(mode), retained = users(body).map(item => {
    if (item.id) return item;
    const value = text(item), identity = scope + ':' + value;
    if (!current.users.has(identity)) current.users.set(identity, 'user_' + mode + '_' + scope + '_' + current.users.size);
    return {...item, id: current.users.get(identity), status: 'completed'};
  });
  assert.equal(retained.length, mode === 'active-finish' ? 4 : 3, 'Every genuine source objective in the selected complete prefix is preserved, including the newly finished root');
  const opaque = String.raw`{"id":"cmp_${mode}_${scope}","type":"compaction","encrypted_content":"synthetic-private-opaque-\u0061","metadata":{"decimal":1.00000000000000000001,"k\u0065y":"retained"}}`;
  const window = '[' + retained.map(item => JSON.stringify(item)).join(',') + ',' + opaque + ']';
  assert.ok(!current.windows.has(scope), 'One controller request cannot receive duplicate maintenance');
  current.windows.set(scope, window);
  return `{"id":"compact_${mode}_${scope}","object":"response.compaction","created_at":1764967971,"output":${window},"usage":{"input_tokens":31,"output_tokens":7,"total_tokens":38}}`;
}
const server = createServer((request, response) => {
  request.on('error', () => {});response.on('error', () => {});const chunks = [];
  request.on('data', chunk => chunks.push(chunk));request.on('end', async () => {
    try {
      assert.equal(request.method, 'POST');assert.equal(request.headers.authorization, 'Bearer ' + key);
      const match = /^\/(idle|queued-cancel|active-finish|model-race|shutdown|uncertain)\/responses(\/input_tokens|\/compact)?$/.exec(request.url);
      assert.ok(match, 'Only independently authored fixed synthetic provider routes are accepted');
      const mode = match[1], kind = match[2] === '/input_tokens' ? 'count' : match[2] === '/compact' ? 'compact' : 'inference';
      const raw = Buffer.concat(chunks).toString('utf8'), body = JSON.parse(raw), model = body.model;
      assert.ok([modelA, modelB].includes(model));assert.ok(Array.isArray(body.input));assert.equal(body.tools, undefined);
      const originalUsers = users(body);assert.ok(originalUsers.length >= 1);
      const objective = text(originalUsers[0]), scopeMatch = new RegExp(`^fixture:${mode}:(owned|other|blocker):`).exec(objective);
      assert.ok(scopeMatch, 'The actual original user scope determines the provider observation');const scope = scopeMatch[1];
      assert.ok(body.input.some(item => item.role === 'system' && text(item).includes('Explicit synthetic native context-control fixture')));
      for (const item of body.input) if (state.get(mode).originals.has(item.id)) assert.deepEqual(item, state.get(mode).originals.get(item.id), 'Actual completed ordinary receipts must be replayed whole');
      if (body.input.some(item => item.type === 'compaction')) assert.ok(raw.includes(state.get(mode).windows.get(scope).slice(1, -1)), 'Prospective counting must retain every exact canonical byte including numeric lexemes');
      const ordinal = audit(mode, scope, model, kind);
      if (kind === 'count') {
        assert.equal(request.headers.accept, 'application/json');assert.deepEqual(Object.keys(body).sort(), ['input', 'model']);assert.equal(model, modelA);
        const projected = body.input.some(item => item.type === 'compaction');assert.equal(projected, ordinal === 2);
        const reply = () => {response.writeHead(200, {'Content-Type': 'application/json'});response.end(JSON.stringify({object: 'response.input_tokens', input_tokens: projected ? 80 : 400}));};
        if (ordinal === 1 && ((mode === 'idle' && scope === 'owned') || (['model-race', 'shutdown'].includes(mode) && scope === 'other'))) {
          await mark(mode, scope + '-count-started');
          // Shutdown intentionally never releases the provider; actual native
          // cancellation closes that socket and retires its real lease.
          if (mode !== 'shutdown') untilFlag(response, mode, 'release-' + scope + '-count', reply);
        } else reply();
        return;
      }
      if (kind === 'compact') {
        assert.equal(request.headers.accept, 'application/json');assert.deepEqual(Object.keys(body).sort(), ['input', 'model']);assert.equal(model, modelA);assert.equal(ordinal, 1);
        const actual = compact(mode, scope, model, body);
        response.writeHead(200, {'Content-Type': 'application/json'});response.end(actual);
        if (mode === 'uncertain') await mark(mode, scope + '-compact-acknowledged');
        return;
      }
      assert.equal(request.headers.accept, 'text/event-stream');assert.equal(body.stream, true);assert.equal(body.store, false);assert.deepEqual(body.include, ['reasoning.encrypted_content']);assert.equal(body.max_output_tokens, 64);
      const last = text(originalUsers.at(-1));
      if (last.endsWith(':hold') || last.endsWith(':model-b-hold')) {
        assert.ok((mode === 'queued-cancel' && scope === 'blocker') || (mode === 'active-finish' && scope === 'owned') || (mode === 'model-race' && scope === 'owned' && model === modelB));
        await mark(mode, scope + '-inference-started');
        if (mode === 'active-finish') untilFlag(response, mode, 'release-owned-inference', () => send(response, mode, scope, model, ordinal));
        return;
      }
      assert.equal(model, modelA);assert.ok(/:seed:[012]$/.test(last));send(response, mode, scope, model, ordinal);
    } catch (error) {
      failure ??= error;if (!response.headersSent) response.writeHead(500);response.end('Synthetic context-control provider rejected a contract request');
    }
  });
});
try {
  for (const mode of modes) await mkdir(join(fixture, mode));
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));const base = 'http://127.0.0.1:' + server.address().port;
  for (const mode of modes) {
    const rawChecks=mode==='idle'?startRawQueryChecks():undefined;
    try {const result = await execute(binary, [join(fixture, mode), modules, stdlib, base, mode], {windowsHide: true, timeout: 30000, maxBuffer: 1024 * 1024});if(rawChecks)assert.equal(await rawChecks.done,true,'Actual raw-target controller checks must complete');if (failure) throw failure;process.stdout.write(result.stdout);}
    catch (error) {nativeFailure = error;throw error;}
    finally {rawChecks?.stop();}
  }
  const expected = {
    idle: {'owned:synthetic-control-a:inference': 3, 'owned:synthetic-control-a:count': 2, 'owned:synthetic-control-a:compact': 1},
    'queued-cancel': {'owned:synthetic-control-a:inference': 3, 'owned:synthetic-control-a:count': 2, 'owned:synthetic-control-a:compact': 1, 'blocker:synthetic-control-a:inference': 1},
    'active-finish': {'owned:synthetic-control-a:inference': 4, 'owned:synthetic-control-a:count': 2, 'owned:synthetic-control-a:compact': 1},
    'model-race': {'other:synthetic-control-a:inference': 3, 'other:synthetic-control-a:count': 2, 'other:synthetic-control-a:compact': 1, 'owned:synthetic-control-b:inference': 1},
    shutdown: {'owned:synthetic-control-a:inference': 3, 'other:synthetic-control-a:inference': 3, 'other:synthetic-control-a:count': 1},
    uncertain: {'owned:synthetic-control-a:inference': 3, 'owned:synthetic-control-a:count': 1, 'owned:synthetic-control-a:compact': 1},
  };
  for (const mode of modes) assert.deepEqual(Object.fromEntries([...state.get(mode).counts.entries()].sort()), Object.fromEntries(Object.entries(expected[mode]).sort()), 'Exact actual socket observations prohibit model/count/maintenance replays');
  assert.equal([...state.values()].flatMap(item => [...item.counts.values()]).reduce((a, b) => a + b, 0), 39, '24 actual inference +10 count +5 maintenance requests; synthetic protocol data only');
  successful = true;
} catch (error) {
  await writeFile(join(fixture, 'private-peer-failure.log'), String(failure?.stack ?? error.stack ?? error), {flag: 'wx', mode: 0o600});
  if (nativeFailure) await writeFile(join(fixture, 'private-native-failure.json'), JSON.stringify({stdout: nativeFailure.stdout ?? '', stderr: nativeFailure.stderr ?? '', code: nativeFailure.code ?? null, signal: nativeFailure.signal ?? null}), {flag: 'wx', mode: 0o600});
  process.stderr.write('Synthetic native context-control contract failed; owned diagnostics retained at ' + fixture + '\n');process.exitCode = 1;
} finally {
  server.closeAllConnections();await new Promise(resolve => server.close(resolve));
  if (successful) {assert.equal(dirname(resolve(fixture)), resolve(tmpdir()));assert.ok(basename(fixture).startsWith('xmind-context-control-'));await rm(fixture, {recursive: true, force: true});}
}
