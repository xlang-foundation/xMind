import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import {mkdtemp, readFile, rm, stat} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
import {randomBytes} from 'node:crypto';
import {createConnection} from 'node:net';
import {fileURLToPath} from 'node:url';
import {verifyCliUsagePresentation} from './cli_usage_presentation.mjs';

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
async function requestDelete(path) {
  const response = await fetch(`http://127.0.0.1:${port}${path}`, {
    method: 'DELETE', headers: {Authorization: `Bearer ${token}`},
    signal: AbortSignal.timeout(10000), redirect: 'error'
  });
  assert.equal(response.status,204,'Successful session deletion follows the pinned no-content response contract');
  assert.equal(await response.text(),'','A 204 deletion response has no body');
  return {status: response.status};
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
  await verifyCliUsagePresentation(cliExe);
  await start();
  await idleConnectionFairness();
  assert.equal((await request('/v1/health')).data.agent_execution, false);
  const serverInfo=await request('/v1/info');
  assert.equal(serverInfo.status,200);
  assert.deepEqual(Object.keys(serverInfo.data).sort(),['capabilities','paths','pid','urls','version']);
  assert.equal(serverInfo.data.version,'0.1.0');
  assert.equal(serverInfo.data.pid,processHandle.pid);
  assert.deepEqual(serverInfo.data.urls,[`http://127.0.0.1:${port}`]);
  assert.equal(resolve(serverInfo.data.paths.tmp),resolve(tmpdir()));
  assert.equal(typeof serverInfo.data.capabilities.persistentPty,'boolean');
  assert.deepEqual(cli('info'),serverInfo.data,'CLI server info must observe the same live backend contract');
  assert.equal((await request('/v1/info',undefined,{Authorization:''})).status,401);
  assert.equal((await request('/v1/info?unexpected=true')).status,400);
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
    const unavailableResubscribe=spawnSync(cliExe,[String(port),'a2a-resubscribe','missing-a2a-operation'],{env,encoding:'utf8',timeout:15000,windowsHide:true});assert.notEqual(unavailableResubscribe.status,0);assert.match(unavailableResubscribe.stderr,/HTTP 404/,'A2A resubscription must route through the shared native server endpoint');
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
  const observedMessages=(await request('/v1/sessions/shared/history')).data;
  const selectedMessage=observedMessages.at(-1);
  const newestPage=await request('/v1/sessions/shared/messages?limit=5');
  assert.equal(newestPage.status,200);assert.deepEqual(newestPage.data.messages.map(item=>item.seq),[8,9,10,11,12]);assert.equal(newestPage.data.has_more,true);assert.equal(newestPage.data.next_before,'msg_8');
  const middlePage=await request('/v1/sessions/shared/messages?limit=5&before=msg_8');
  assert.deepEqual(middlePage.data.messages.map(item=>item.seq),[3,4,5,6,7]);assert.equal(middlePage.data.has_more,true);assert.equal(middlePage.data.next_before,'msg_3');
  const oldestPage=await request('/v1/sessions/shared/messages?limit=5&before=msg_3');
  assert.deepEqual(oldestPage.data.messages.map(item=>item.seq),[1,2]);assert.equal(oldestPage.data.has_more,false);assert.equal(oldestPage.data.next_before,null);
  assert.deepEqual(cli('history-page','shared','5'),newestPage.data,'Console CLI returns the same bounded page as HTTP');
  assert.deepEqual(cli('history-page','shared','5','msg_8'),middlePage.data,'Console CLI follows the stable message boundary');
  assert.equal((await request('/v1/sessions/shared/messages?limit=0')).status,400);
  assert.equal((await request('/v1/sessions/shared/messages?limit=1001')).status,400);
  assert.equal((await request('/v1/sessions/shared/messages?before=msg_bad')).status,400);
  assert.equal((await request('/v1/sessions/shared/messages?before=msg_999999')).status,404);
  assert.equal((await request('/v1/sessions/shared/messages?unexpected=true')).status,400);
  assert.equal((await request('/v1/sessions/shared/messages?limit=5&limit=6')).status,400);
  assert.deepEqual(await request(`/v1/sessions/shared/messages/${selectedMessage.id}`),{status:200,data:selectedMessage},'Single-message lookup returns the durable typed history entry');
  assert.deepEqual(cli('message','shared',selectedMessage.id),selectedMessage,'Console CLI exposes the same single-message lookup');
  const malformedMessage=spawnSync(cliExe,[String(port),'message','shared','msg_bad'],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.notEqual(malformedMessage.status,0,'Console CLI rejects malformed message IDs before making a request');
  assert.equal((await request('/v1/sessions/shared/messages/msg_999999')).status,404,'Single-message lookup reports an absent sequence');
  assert.equal((await request('/v1/sessions/shared/messages/msg_bad')).status,400,'Single-message lookup validates the public message ID');
  assert.equal((await request(`/v1/sessions/shared/messages/${selectedMessage.id}?unexpected=true`)).status,400,'Single-message lookup rejects unsupported query parameters');
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
  const archiveFile=join(folder,'shared-session.xmind.json');const exportResult=spawnSync(cliExe,[String(port),'export-session','shared',archiveFile],{env,encoding:'utf8',timeout:15000,windowsHide:true});
  assert.equal(exportResult.status,0,exportResult.stderr);assert.deepEqual(JSON.parse(exportResult.stdout),{exported:true,file:archiveFile});
  const archive=JSON.parse(await readFile(archiveFile,'utf8'));assert.equal(archive.format,'xmind.session');assert.equal(archive.version,2);assert.equal(archive.session.title,'HTTP and CLI');assert.equal(archive.session.agent,null);assert.equal(archive.messages.length,13);
  assert.equal((await request('/v1/sessions/shared/export')).data.messages.length,13,'HTTP export includes the complete settled transcript');
  assert.equal((await request('/v1/sessions/import',{archive:{...archive,version:3}})).status,400,'Unknown archive versions are rejected');
  const importedResponse=await request('/v1/sessions/import',{archive});assert.equal(importedResponse.status,201);assert.notEqual(importedResponse.data.id,'shared');assert.equal(importedResponse.data.title,'HTTP and CLI');
  assert.deepEqual(cli('history',importedResponse.data.id).map(({role,data})=>({role,data})),archive.messages,'HTTP import preserves transcript order and content');
  const importedCli=spawnSync(cliExe,[String(port),'import-session',archiveFile],{env,encoding:'utf8',timeout:15000,windowsHide:true});assert.equal(importedCli.status,0,importedCli.stderr);const importedByCli=JSON.parse(importedCli.stdout);
  assert.equal(importedByCli.title,'HTTP and CLI');assert.deepEqual(cli('history',importedByCli.id).map(({role,data})=>({role,data})),archive.messages,'Console import preserves transcript order and content');
  const legacyArchive={...archive,version:1,session:{title:archive.session.title}};assert.equal((await request('/v1/sessions/import',{archive:legacyArchive})).status,201,'Version 1 archives remain importable');
  const selectedAgent={id:'portable-agent',name:'portable-agent',description:'',revision:2,model_id:'openai/gpt-5.6-sol',instructions:'Keep changes focused.',mode:'all',hidden:false,default:false,tools:['read_file','apply_patch']};
  const agentArchive={...archive,session:{...archive.session,agent:selectedAgent}};const importedAgent=await request('/v1/sessions/import',{archive:agentArchive});assert.equal(importedAgent.status,201);
  const agentExport=await request(`/v1/sessions/${importedAgent.data.id}/export`);assert.equal(agentExport.status,200);assert.deepEqual(agentExport.data.session.agent,selectedAgent,'Agent model, instructions and tool policy survive a transfer round trip');
  const malformedRole=await request('/v1/sessions/import',{archive:{...archive,messages:[{role:'system',data:{content:'must reject'}}]}});assert.equal(malformedRole.status,400,'Unsupported message roles are rejected');
  assert.equal((await request('/v1/sessions/shared/export?unexpected=true')).status,400,'Session export rejects unsupported query parameters');
  assert.equal((await request('/v1/runs/run/events?after=-1')).status, 400);
  assert.equal((await request('/v1/runs/run/events?after=0&after=1')).status, 400);
  await stop();await start();
  assert.equal(cli('history', 'shared').length, 13);
  assert.deepEqual(cli('history',importedResponse.data.id).map(({role,data})=>({role,data})),archive.messages,'Imported HTTP transcript persists across native restart');
  assert.deepEqual(cli('history',importedByCli.id).map(({role,data})=>({role,data})),archive.messages,'Imported CLI transcript persists across native restart');
  assert.deepEqual((await request(`/v1/sessions/${importedAgent.data.id}/export`)).data.session.agent,selectedAgent,'Imported agent snapshot persists across native restart');
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
  const deleteSource=await request('/v1/sessions',{id:'delete-source',title:'Deletion source'});assert.equal(deleteSource.status,201);
  assert.equal((await request('/v1/sessions/delete-source/messages',{role:'user',data:{content:'Fork retention fixture'}})).status,201);
  const deleteChild=await request('/v1/sessions/delete-source/fork',{});assert.equal(deleteChild.status,200);const childId=deleteChild.data.id;
  assert.deepEqual(await requestDelete('/v1/sessions/delete-source'),{status:204});
  assert.equal((await request('/v1/sessions/delete-source')).status,404,'Deleted session is absent from the native API');
  assert.equal((await request('/v1/sessions/delete-source/messages')).status,404,'Deleted session history is absent from the native API');
  assert.ok(!cli('sessions').some(item=>item.id===childId),'Deleting a parent also removes its direct child fork');
  assert.equal((await request(`/v1/sessions/${childId}/messages`)).status,404,'A deleted child fork is absent from the native history API');
  const deleteCli=await request('/v1/sessions',{id:'delete-cli',title:'CLI deletion'});assert.equal(deleteCli.status,201);
  assert.deepEqual(cli('delete-session','delete-cli'),{deleted:true,session_id:'delete-cli'},'CLI deletion reports the removed session without decoding an empty HTTP body');assert.ok(!cli('sessions').some(item=>item.id==='delete-cli'),'CLI deletion removes the selected session');
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
