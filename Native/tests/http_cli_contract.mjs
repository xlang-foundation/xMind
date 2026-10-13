import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import {mkdtemp, rm, stat} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
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
async function idleConnectionFairness() {
  const sockets=[];
  try {
    await Promise.all(Array.from({length:4},()=>new Promise((resolve,reject)=>{
      const socket=createConnection({host:'127.0.0.1',port});sockets.push(socket);let response='',ready=false;
      socket.setTimeout(5000,()=>{if(!ready)reject(new Error('Idle fairness peer did not receive health response'));});
      socket.on('error',reject);
      socket.on('connect',()=>socket.write(`GET /v1/health HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nAuthorization: Bearer ${token}\r\nConnection: keep-alive\r\n\r\n`));
      socket.on('data',chunk=>{
        response+=chunk;const boundary=response.indexOf('\r\n\r\n');if(boundary<0)return;
        const length=/content-length:\s*(\d+)/i.exec(response.slice(0,boundary));
        if(length && Buffer.byteLength(response.slice(boundary+4))>=Number(length[1])){assert.match(response,/^HTTP\/1\.1 200/);ready=true;resolve();}
      });
      socket.on('end',()=>{if(!ready)reject(new Error('Idle fairness peer closed before its response'));});
    })));
    // Keep all four worker connections idle while another authenticated client
    // requests service. No fixture retries, forced Connection:close or timeout
    // widening is applied to the production clients under test.
    const started=performance.now();
    assert.equal(await raw(`Host: 127.0.0.1:${port}\r\nAuthorization: Bearer ${token}`),200);
    assert.ok(performance.now()-started<3000,'Idle peers must release the bounded worker pool before the next client deadline');
  } finally {for(const socket of sockets)socket.destroy();}
}
try {
  await start();
  await idleConnectionFairness();
  assert.equal((await request('/v1/health')).data.agent_execution, false);
  const processCatalog=cli('process-profiles');assert.equal(processCatalog.runtime_state,'per_operation');assert.ok(['powershell','cmd'].includes(processCatalog.profiles.find(profile=>['powershell','cmd'].includes(profile.id))?.id),'Windows local server provides a trusted built-in shell profile');assert.equal(processCatalog.profiles.find(profile=>['powershell','cmd'].includes(profile.id))?.max_timeout_ms,600000,'The built-in shell is bounded');
  const bundledXlang=await stat(resolve(dirname(modules),'xlang3.exe')).then(()=>true,()=>false);assert.equal(processCatalog.profiles.some(profile=>profile.id==='xlang3'),bundledXlang,'The trusted xlang3 process profile follows the backend-paired runtime executable');if(bundledXlang){const profile=processCatalog.profiles.find(value=>value.id==='xlang3');assert.deepEqual(profile,{id:'xlang3',revision:1,max_timeout_ms:600000});}
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
  if(process.platform==='win32'){
    assert.deepEqual(cli('a2a-agents'),{revision:0,agents:[]},'A2A registry starts empty and is available through the native CLI');
    const secret='synthetic-cli-a2a-bearer-'+randomBytes(8).toString('hex'),registered=spawnSync(cliExe,[String(port),'a2a-register','research.cli','https://peer.invalid/agent-card.json','true','0','XMIND_A2A_TEST_TOKEN'],{env:{...env,XMIND_A2A_TEST_TOKEN:secret},encoding:'utf8',timeout:15000,windowsHide:true});
    assert.equal(registered.status,0,registered.stderr);assert.ok(!registered.stdout.includes(secret)&&!registered.stderr.includes(secret),'A2A bearer must not appear in CLI output');const added=JSON.parse(registered.stdout);assert.equal(added.agent.id,'research.cli');assert.equal(added.agent.has_bearer,true);assert.deepEqual(cli('a2a-agents').agents,[added.agent]);
    const unavailable=spawnSync(cliExe,[String(port),'a2a-inspect','missing-a2a-operation'],{env,encoding:'utf8',timeout:15000,windowsHide:true});assert.notEqual(unavailable.status,0);assert.match(unavailable.stderr,/HTTP 404/,'A2A task inspection must route through the shared native server endpoint');
    const unavailableCancel=spawnSync(cliExe,[String(port),'a2a-cancel','missing-a2a-operation'],{env,encoding:'utf8',timeout:15000,windowsHide:true});assert.notEqual(unavailableCancel.status,0);assert.match(unavailableCancel.stderr,/HTTP 404/,'A2A cancellation must route through the shared native server endpoint');
    const unavailableFollowUp=spawnSync(cliExe,[String(port),'a2a-follow-up','missing-a2a-operation','1','synthetic follow-up'],{env,encoding:'utf8',timeout:15000,windowsHide:true});assert.notEqual(unavailableFollowUp.status,0);assert.match(unavailableFollowUp.stderr,/HTTP 404/,'A2A follow-up must route through the shared native server endpoint');
    assert.deepEqual(cli('a2a-enable','research.cli','false','1'),{agent:{...added.agent,revision:2,enabled:false}});
    assert.deepEqual(cli('a2a-remove','research.cli','2'),{removed:true,id:'research.cli'});assert.deepEqual(cli('a2a-agents'),{revision:3,agents:[]});
  }
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
  assert.deepEqual(await request('/v1/sessions/shared'),{status:200,data:{id:'shared',title:'HTTP and CLI'}},'Single-session lookup returns the durable session record');
  assert.equal((await request('/v1/sessions/missing-session')).status,404,'Single-session lookup reports absent records');
  assert.equal((await request('/v1/sessions/shared?unexpected=true')).status,400,'Single-session lookup rejects unsupported query parameters');
  assert.equal((await request('/v1/sessions',{id:'shared',title:'duplicate'})).status,409);
  const second = cli('create-session', 'CLI-created');
  assert.equal((await request('/v1/sessions')).data.length, 2);
  assert.ok(second.id);
  if(process.platform==='win32') {
    const launcher=fileURLToPath(new URL('../../Tools/agentflow.ps1',import.meta.url));
    const launched=spawnSync('pwsh',['-NoProfile','-File',launcher,'-Action','Client','-BinaryDirectory',dirname(serverExe),'-Port',String(port),'sessions'],{env,encoding:'utf8',timeout:15000,windowsHide:true});
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
  const sourceHistory=cli('history','shared');
  assert.equal(sourceHistory[0].id,`msg_${sourceHistory[0].seq}`,'Public message IDs bind stably to persisted sequence values');
  const branched=await request('/v1/sessions/shared/fork',{before:`msg_${sourceHistory[5].seq}`});
  assert.equal(branched.status,200);assert.equal(branched.data.parent_id,'shared');assert.equal(branched.data.fork.message_id,`msg_${sourceHistory[5].seq}`);assert.equal(branched.data.fork.boundary_type,'before');
  const branchHistory=cli('history',branched.data.id);assert.deepEqual(branchHistory.map(({role,data})=>({role,data})),sourceHistory.slice(0,5).map(({role,data})=>({role,data})),'Fork boundary excludes the selected source message');
  assert.equal((await request('/v1/sessions/shared/fork',{before:'msg_999999999'})).status,404,'Unknown source message boundary is not accepted');
  assert.equal((await request('/v1/sessions/shared/fork',{before:'bad'})).status,400,'Malformed fork message IDs are rejected');
  const fullBranch=cli('fork-session','shared');
  assert.equal(fullBranch.parent_id,'shared');assert.equal(fullBranch.fork.boundary_type,'through');
  assert.deepEqual(cli('history',fullBranch.id).map(({role,data})=>({role,data})),sourceHistory.map(({role,data})=>({role,data})),'Full fork copies the complete settled source transcript');
  const foreign=await request('/v1/sessions', {id:'other',title:'Other session'});assert.equal(foreign.status,201);
  const foreignMessage=await request('/v1/sessions/other/messages',{role:'user',data:{content:'foreign'}});assert.equal(foreignMessage.status,201);
  const foreignHistory=cli('history','other');assert.equal((await request('/v1/sessions/shared/fork',{before:`msg_${foreignHistory[0].seq}`})).status,404,'A message from another session cannot be used as a boundary');
  assert.deepEqual(cli('runs','shared'),[]);
  const wrong = spawnSync(cliExe, [String(port), 'sessions'], {env: {...env, XMIND_AUTH_TOKEN: 'wrong'}, encoding: 'utf8', windowsHide: true});
  const savedSessions=cli('sessions'),savedHistory=cli('history','shared');
  const readOnlyChat=spawnSync(cliExe,[String(port),'chat'],{env,input:'/sessions\n/session shared\n/runs\n/history\nDo not admit this unconfigured request\n/exit\n',encoding:'utf8',timeout:15000,windowsHide:true});
  assert.equal(readOnlyChat.status,0,readOnlyChat.stderr);assert.match(readOnlyChat.stderr,/Configure a backend model before submitting/);
  const inspected=readOnlyChat.stdout.trim().split(/\r?\n/).map(line=>JSON.parse(line));assert.ok(inspected.some(record=>record.type==='history'&&record.session_id==='shared'&&JSON.stringify(record.history)===JSON.stringify(savedHistory)));assert.ok(!inspected.some(record=>record.type==='run'));assert.deepEqual(cli('sessions'),savedSessions);assert.deepEqual(cli('history','shared'),savedHistory);assert.deepEqual(cli('runs','shared'),[]);
  const emptyUnconfiguredChat=spawnSync(cliExe,[String(port),'chat'],{env,input:'No configured model\n/exit\n',encoding:'utf8',timeout:15000,windowsHide:true});assert.equal(emptyUnconfiguredChat.status,0,emptyUnconfiguredChat.stderr);assert.equal(emptyUnconfiguredChat.stdout,'');assert.deepEqual(cli('sessions'),savedSessions,'Unavailable inference cannot create a placeholder conversation');
  assert.deepEqual(inspected.find(record=>record.type==='runs'),{type:'runs',session_id:'shared',runs:[]},'Saved run inspection is available without a provider');
  assert.equal(wrong.status, 1);
  console.log('Native HTTP/CLI contracts passed: authentication, shared clients, concurrent messages, restart persistence; unconfigured execution rejects admission without creating a run');
} finally {
  await stop();assert.equal(dirname(resolve(folder)),resolve(tmpdir()));assert.ok(basename(folder).startsWith('xmind-http-'));await rm(folder, {recursive: true, force: true});
}
