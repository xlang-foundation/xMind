import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import {mkdtemp, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {randomBytes} from 'node:crypto';
import {createConnection} from 'node:net';
import {fileURLToPath} from 'node:url';

const [serverExe, cliExe, modules, stdlib] = process.argv.slice(2);
assert.ok(serverExe && cliExe && modules && stdlib, 'Pass server, CLI and runtime roots');
const folder = await mkdtemp(join(tmpdir(), 'xmind-http-'));
const token = randomBytes(32).toString('hex');
const env = {...process.env, XMIND_AUTH_TOKEN: token};
let processHandle, port;
async function start() {
  processHandle = spawn(serverExe, ['--db', join(folder, 'state.sqlite'), '--modules', modules, '--stdlib', stdlib, '--port', '0'], {env, windowsHide: true});
  let output = '', errors = '';
  processHandle.stderr.on('data', data => { errors += data; });
  port = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('Server readiness timed out')), 10000);
    processHandle.on('error', error => { clearTimeout(timer); reject(error); });
    processHandle.on('exit', code => { clearTimeout(timer); reject(new Error(`Server exited ${code}: ${errors}`)); });
    processHandle.stdout.on('data', data => {
      output += data;
      const match = /listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);
      if (match) { clearTimeout(timer); resolve(Number(match[1])); }
    });
  });
}
async function stop() {
  if (!processHandle || processHandle.exitCode !== null) return;
  const exited = new Promise(resolve => processHandle.once('exit', resolve));
  processHandle.kill(); await exited; processHandle = null;
}
async function request(path, body, headers = {}) {
  const response = await fetch(`http://127.0.0.1:${port}${path}`, {
    method: body === undefined ? 'GET' : 'POST',
    headers: {Authorization: `Bearer ${token}`, ...(body === undefined ? {} : {'Content-Type': 'application/json'}), ...headers},
    body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(10000), redirect: 'error'
  });
  assert.match(response.headers.get('content-type') || '', /application\/json/);
  return {status: response.status, data: await response.json()};
}
function cli(...args) {
  const result = spawnSync(cliExe, [String(port), ...args], {env, encoding: 'utf8', timeout: 15000, windowsHide: true});
  assert.equal(result.status, 0, result.stderr || 'CLI failed');return JSON.parse(result.stdout);
}
async function raw(headers) {
  return new Promise((resolve,reject) => {
    const socket=createConnection({host:'127.0.0.1',port});let output='';
    socket.setTimeout(5000,()=>{socket.destroy();reject(new Error('Raw HTTP timeout'));});
    socket.on('error',reject);
    socket.on('connect',()=>socket.write(`GET /v1/health HTTP/1.1\r\n${headers}\r\nConnection: close\r\n\r\n`));
    socket.on('data',data=>{output+=data;});
    socket.on('end',()=>resolve(Number(/^HTTP\/1\.1 (\d+)/.exec(output)?.[1])));
  });
}
try {
  await start();
  assert.equal((await request('/v1/health')).data.agent_execution, false);
  assert.deepEqual(cli('process-profiles'),{profiles:[],runtime_state:'per_operation'},'Unconfigured native server must not invent process profiles or running processes');
  assert.equal((await request('/v1/process/profiles',undefined,{Authorization:''})).status,401);
  assert.equal((await request('/v1/process/profiles?executable=spoof')).status,400,'Discovery cannot select executable paths');
  assert.equal((await request('/v1/agent/instructions',undefined,{Authorization:''})).status,401);
  assert.equal((await request('/v1/agent/instructions?instructions=spoof')).status,400,'Discovery cannot import instruction text');
  assert.deepEqual(cli('instructions'),{revision:0,byte_count:0,scope:'server',runtime_state:'startup_snapshot'});
  assert.equal((await request('/v1/sessions', undefined, {Authorization: ''})).status, 401);
  assert.equal((await request('/v1/sessions', {}, {Authorization: 'Bearer wrong'})).status, 401);
  assert.equal((await request('/v1/health', undefined, {Origin: 'https://untrusted.example'})).status, 403);
  // fetch controls Host itself; use a raw independent peer for these wire checks.
  assert.equal(await raw(`Host: untrusted.example\r\nAuthorization: Bearer ${token}`),400);
  assert.equal(await raw(`Host: 127.0.0.1:${port}\r\nAuthorization: Bearer ${token}\r\nAuthorization: Bearer ${token}`),401);
  assert.deepEqual(cli('sessions'), []);
  const deepRequest=await fetch(`http://127.0.0.1:${port}/v1/sessions`,{
    method:'POST',headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'},
    body:'{"title":'+'['.repeat(10000)+'"nested"'+']'.repeat(10000)+'}',signal:AbortSignal.timeout(5000)
  });
  assert.equal(deepRequest.status,400);
  assert.equal((await deepRequest.json()).detail,'Request JSON nesting exceeds limits','Reject excessive depth during parsing, before shape validation');
  assert.deepEqual(cli('sessions'),[],'Rejected nested requests must not persist data');
  assert.equal((await request('/v1/sessions', {title: 'unknown', unexpected: 1})).status, 400);
  assert.equal((await request('/v1/sessions', {title: 'media'}, {'Content-Type': 'text/plain'})).status, 400);
  const session = await request('/v1/sessions', {id: 'shared', title: 'HTTP and CLI'});
  assert.equal(session.status, 201);assert.equal(cli('sessions')[0].id, 'shared');
  assert.equal((await request('/v1/sessions',{id:'shared',title:'duplicate'})).status,409);
  const second = cli('create-session', 'CLI-created');
  assert.equal((await request('/v1/sessions')).data.length, 2);
  assert.ok(second.id);
  if(process.platform==='win32') {
    const launcher=fileURLToPath(new URL('../../Tools/agentflow.ps1',import.meta.url));
    const launched=spawnSync('pwsh',['-NoProfile','-File',launcher,'-Action','Client','-Port',String(port),'sessions'],{env,encoding:'utf8',timeout:15000,windowsHide:true});
    assert.equal(launched.status,0,launched.stderr);assert.equal(JSON.parse(launched.stdout).length,2);
  }
  const writes = await Promise.all(Array.from({length: 12}, (_, i) => request('/v1/sessions/shared/messages', {role: 'user', data: {content: `message ${i}`}})));
  assert.ok(writes.every(result => result.status === 201));
  assert.equal(cli('history', 'shared').length, 12);
  for(const data of [{},{unknown:'not conversation content'},{content:[]},{content:''},{content:'embedded\0NUL'},
    {content:'text',tool_calls:[]},{content:'text',refusal:'unsupported user field'}]) {
    assert.equal((await request('/v1/sessions/shared/messages',{role:'user',data})).status,400);
  }
  assert.equal(cli('history','shared').length,12,'Invalid conversation data must not contaminate stored history');
  // A model-free server now has an enrollment-capable executor, but it must
  // reject work as unavailable and create no run until a provider is enrolled.
  assert.equal((await request('/v1/runs', {id: 'not-executed', session_id: 'shared', prompt: 'Do coding'})).status, 503);
  assert.equal((await request('/v1/runs/not-executed')).status, 404);
  assert.deepEqual(cli('runs','shared'),[]);
  assert.equal((await request('/v1/runs/run/transition',{expected:'queued',next:'running'})).status,404);
  assert.equal((await request('/v1/sessions/shared/messages',{role:'assistant',data:{content:'fabricated'}})).status,400);
  cli('append-message','shared','User message from native CLI');
  assert.equal((await request('/v1/runs/run/events?after=-1')).status, 400);
  assert.equal((await request('/v1/runs/run/events?after=0&after=1')).status, 400);
  await stop();await start();
  assert.equal(cli('history', 'shared').length, 13);
  assert.deepEqual(cli('runs','shared'),[]);
  const wrong = spawnSync(cliExe, [String(port), 'sessions'], {env: {...env, XMIND_AUTH_TOKEN: 'wrong'}, encoding: 'utf8', windowsHide: true});
  assert.equal(wrong.status, 1);
  console.log('Native HTTP/CLI contracts passed: authentication, shared clients, concurrent messages, restart persistence; execution routes absent');
} finally {
  await stop();await rm(folder, {recursive: true, force: true});
}
