import assert from 'node:assert/strict';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {randomBytes,createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {createRequire} from 'node:module';
const {BackendClient}=createRequire(import.meta.url)('../../extensions/vscode/client.js');
const [fixture,cliExe,modules,stdlib,serverExe,xmindExe]=process.argv.slice(2);
assert.ok(xmindExe,'Pass the unified xmind executable for console reconciliation');
const folder=await mkdtemp(join(tmpdir(),'xmind-approved-http-')),workspace=join(folder,'workspace');
const recoveryWorkspace=join(folder,'recovery-workspace');
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};
let child,port,errors='',output='';
async function request(path,body,headers={}) {
  const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:`Bearer ${token}`,...(body===undefined?{}:{'Content-Type':'application/json'}),...headers},body:body===undefined?undefined:typeof body==='string'?body:JSON.stringify(body),signal:AbortSignal.timeout(5000)});
  return {status:response.status,data:await response.json()};
}
function cli(...args) {const result=spawnSync(cliExe,[String(port),...args],{env,windowsHide:true,encoding:'utf8',timeout:5000});assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);}
async function state(id,expected) {
  const deadline=Date.now()+5000;
  while(Date.now()<deadline) {const result=await request(`/v1/operations/${id}`);if(result.status===200 && result.data.state===expected) return result.data;await delay(10);}
  throw new Error(`Timed out awaiting ${id} ${expected}: ${errors}`);
}
try {
  await mkdir(workspace);
  await mkdir(recoveryWorkspace);await writeFile(join(recoveryWorkspace,'recovery.txt'),'original\n');
  await Promise.all(['allowed.txt','denied.txt','stale.txt'].map(path=>writeFile(join(workspace,path),'original\n')));
  child=spawn(fixture,[folder,workspace,modules,stdlib],{env,windowsHide:true});
  child.stderr.on('data',data=>{errors+=data;});
  port=await new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error(`Fixture readiness timeout: ${errors}`)),10000);
    child.on('error',error=>{clearTimeout(timer);reject(error);});
    child.once('exit',code=>{clearTimeout(timer);reject(new Error(`Fixture exited ${code}: ${errors}`));});
    child.stdout.on('data',data=>{output+=data;const match=/fixture-port (\d+)/.exec(output);if(match){clearTimeout(timer);resolve(Number(match[1]));}});
  });
  await Promise.all(['allow-edit','deny-edit','stale-edit'].map(id=>state(id,'awaiting_approval')));
  const hostClient=new BackendClient(`http://127.0.0.1:${port}`,async()=>token);
  const original=cli('operation','allow-edit');
  assert.equal((await hostClient.operation('allow-edit')).arguments_json,original.arguments_json);
  assert.equal((await hostClient.operations('run-allow'))[0].id,'allow-edit');
  assert.equal(cli('operations','run-allow')[0].arguments_json,original.arguments_json);
  assert.equal(JSON.parse(original.arguments_json).after_content,'approved native\n');
  assert.equal(await readFile(join(workspace,'allowed.txt'),'utf8'),'original\n');
  assert.equal((await request('/v1/operations/allow-edit',undefined,{Authorization:'Bearer wrong'})).status,401);
  assert.equal((await request('/v1/operations/allow-edit/decision',{decision:'allow'},{Authorization:'Bearer wrong'})).status,401);
  assert.equal((await request('/v1/operations/allow-edit/decision',{decision:'allow',actor:'spoofed'})).status,400);
  assert.equal((await request('/v1/operations/allow-edit/decision',{decision:'allow',arguments_json:'{}'})).status,400);
  assert.equal((await request('/v1/operations/allow-edit/decision','{"decision":"deny","decision":"allow"}')).status,400);
  assert.equal((await request('/v1/operations/allow-edit/decision',{decision:'maybe'})).status,400);
  assert.equal((await request('/v1/operations/missing')).status,404);
  assert.equal((await request('/v1/operations',{id:'fake'})).status,404,'Views cannot create effect proposals');
  assert.equal((await request('/v1/operations/allow-edit/claim',{})).status,404,'Views cannot claim effects');
  assert.equal((await request('/v1/operations/allow-edit/finish',{state:'succeeded'})).status,404,'Views cannot fabricate outcomes');
  cli('decide','deny-edit','deny');
  assert.equal((await state('deny-edit','denied')).decision_actor,'local-owner');
  assert.equal(await readFile(join(workspace,'denied.txt'),'utf8'),'original\n');
  await hostClient.decide('allow-edit','allow');const actual=await state('allow-edit','succeeded');
  assert.equal(actual.decision_actor,'local-owner');assert.equal(actual.arguments_json,original.arguments_json);
  assert.equal(await readFile(join(workspace,'allowed.txt'),'utf8'),'approved native\n');
  assert.equal(JSON.parse(actual.result_json).after_sha256,createHash('sha256').update(await readFile(join(workspace,'allowed.txt'))).digest('hex'));
  assert.equal((await request('/v1/operations/allow-edit/decision',{decision:'allow'})).status,409);
  await writeFile(join(workspace,'stale.txt'),'external writer\n');
  assert.equal((await request('/v1/operations/stale-edit/decision',{decision:'allow'})).status,200);
  await state('stale-edit','failed');assert.equal(await readFile(join(workspace,'stale.txt'),'utf8'),'external writer\n');
  const operationBefore=cli('operation','uncertain-edit');
  const eventsBefore=cli('events','run-recovery');
  const recoveryPath='/v1/operations/uncertain-edit/inspection';
  assert.equal((await request(recoveryPath,undefined,{Authorization:'Bearer wrong'})).status,401);
  assert.equal((await request(recoveryPath+'?path=allowed.txt')).status,400);
  assert.equal((await request(recoveryPath,{})).status,404,'Inspection has no mutation route');
  assert.equal((await request('/v1/operations/missing/inspection')).status,404);
  assert.equal((await request('/v1/operations/allow-edit/inspection')).status,409);
  const reconcilePath='/v1/operations/uncertain-process/reconcile';
  assert.equal((await request(reconcilePath,{note:'I inspected the workspace and output.'},{Authorization:'Bearer wrong'})).status,401);
  assert.equal((await request(reconcilePath,{note:'I inspected the workspace and output.'})).status,400,'Possible side effects require explicit acknowledgment');
  assert.equal((await request(reconcilePath,{note:'short',acknowledge_possible_side_effects:true})).status,400,'Reconciliation requires an audit note');
  const auditNote='Inspected workspace and command output; accepting possible side effects.',acknowledgment='--acknowledge-possible-side-effects';
  const execute=(args,input)=>spawnSync(xmindExe,['--port',String(port),...args],{env,windowsHide:true,encoding:'utf8',timeout:5000,...(input===undefined?{}:{input})});
  const pendingProcess=cli('operation','uncertain-process'),pendingEvents=cli('events','run-process-recovery'),pendingOutput=cli('process-output','uncertain-process','stdout');
  for(const args of [
    ['reconcile-process','uncertain-process',auditNote],
    ['reconcile-process','uncertain-process','--not-an-acknowledgment',auditNote],
    ['reconcile-process','uncertain-process',acknowledgment,'short'],
    ['reconcile-process','uncertain-process',acknowledgment,'x'.repeat(1025)],
    ['reconcile-process','uncertain-process',acknowledgment,'A multiline audit\nnote is invalid.']
  ]){const rejected=execute(args);assert.ifError(rejected.error);assert.equal(rejected.status,1);assert.equal(rejected.stdout,'');assert.deepEqual(cli('operation','uncertain-process'),pendingProcess);assert.deepEqual(cli('events','run-process-recovery'),pendingEvents);}
  const accepted=execute(['reconcile-process','uncertain-process',acknowledgment,auditNote]);assert.ifError(accepted.error);assert.equal(accepted.status,0,accepted.stderr);
  const reconciled=JSON.parse(accepted.stdout);assert.equal(reconciled.id,'uncertain-process');assert.equal(reconciled.state,'failed');
  const reconciliation=JSON.parse(reconciled.result_json).reconciliation;assert.equal(reconciliation.actor,'local-owner');assert.equal(reconciliation.note,auditNote);assert.ok(Number.isSafeInteger(reconciliation.reconciled_unix_ms));assert.equal(reconciliation.acknowledges_possible_side_effects,true);
  assert.equal(cli('status','run-process-recovery').state,'failed','Acknowledgment must not turn the failed run into success');
  assert.deepEqual(cli('process-output','uncertain-process','stdout'),pendingOutput,'Saved command output remains unchanged after reconciliation');
  const replay=execute(['reconcile-process','uncertain-process',acknowledgment,auditNote]);assert.ifError(replay.error);assert.equal(replay.status,1,'Direct reconciliation must not replay an acknowledgment');
  const chatHistory=cli('history','process-recovery-chat'),chatRuns=cli('runs','process-recovery-chat');
  const chat=execute(['chat','process-recovery-chat'],`/reconcile-process uncertain-process-chat ${acknowledgment} ${auditNote}\n/exit\n`);assert.ifError(chat.error);assert.equal(chat.status,0,chat.stderr);
  const chatRecords=chat.stdout.trim().split(/\r?\n/).map(line=>JSON.parse(line)),chatAck=chatRecords.find(record=>record.type==='process_reconciliation');assert.ok(chatAck);assert.equal(chatAck.operation.id,'uncertain-process-chat');assert.equal(chatAck.operation.state,'failed');assert.equal(JSON.parse(chatAck.operation.result_json).reconciliation.note,auditNote);assert.ok(!chatRecords.some(record=>record.type==='run'));
  assert.deepEqual(cli('history','process-recovery-chat'),chatHistory);assert.deepEqual(cli('runs','process-recovery-chat'),chatRuns,'Chat recovery must not create or change any run');
  const chatEvents=cli('events','run-process-recovery-chat');assert.equal(chatEvents.filter(event=>event.kind==='operation.reconciled').length,1);
  const chatReplay=execute(['chat','process-recovery-chat'],`/reconcile-process uncertain-process-chat ${acknowledgment} ${auditNote}\n/exit\n`);assert.ifError(chatReplay.error);assert.equal(chatReplay.status,1);assert.deepEqual(cli('events','run-process-recovery-chat'),chatEvents);
  const help=spawnSync(xmindExe,['--help'],{env,windowsHide:true,encoding:'utf8',timeout:5000});assert.ifError(help.error);assert.equal(help.status,0);assert.match(help.stdout,/reconcile-process OPERATION_ID --acknowledge-possible-side-effects NOTE/);
  assert.equal((await request(reconcilePath,{note:'Inspected workspace and command output; accepting possible side effects.',acknowledge_possible_side_effects:true})).status,409,'A reconciliation cannot be replayed');
  assert.ok(cli('events','run-process-recovery').some(event=>event.kind==='operation.reconciled'&&event.data.actor==='local-owner'));
  assert.equal((await request('/v1/operations/uncertain-edit/reconcile',{note:'Inspected workspace and output; accepting possible effects.',acknowledge_possible_side_effects:true})).status,409,'File recovery cannot use process reconciliation');
  const beforeInspection=cli('inspect-edit','uncertain-edit');
  assert.deepEqual(await hostClient.inspectEdit('uncertain-edit').then(value=>({...value,observed_unix_ms:beforeInspection.observed_unix_ms})),beforeInspection,'Actual editor host client uses the compiled native inspection route');
  assert.equal(beforeInspection.match,'before');assert.equal(beforeInspection.same_file,true);
  assert.equal(beforeInspection.quarantine_released,false);
  assert.equal(beforeInspection.observed.size,9);
  assert.equal(beforeInspection.observed.content_sha256,createHash('sha256').update('original\n').digest('hex'));
  assert.ok(Number.isSafeInteger(beforeInspection.observed_unix_ms));
  assert.equal(await readFile(join(recoveryWorkspace,'recovery.txt'),'utf8'),'original\n');
  // Explicit external fixture writes to test observations. Inspection itself
  // cannot infer attribution or invent a successful effect from matching bytes.
  await writeFile(join(recoveryWorkspace,'recovery.txt'),'planned change\n');
  const afterInspection=await request(recoveryPath);assert.equal(afterInspection.status,200);
  assert.equal(afterInspection.data.match,'after');assert.equal(afterInspection.data.operation.state,'uncertain');
  const partial=Buffer.from([0,255,17]);await writeFile(join(recoveryWorkspace,'recovery.txt'),partial);
  const partialInspection=cli('inspect-edit','uncertain-edit');assert.equal(partialInspection.match,'different');
  assert.equal(partialInspection.observed.content_sha256,createHash('sha256').update(partial).digest('hex'));
  assert.deepEqual(await readFile(join(recoveryWorkspace,'recovery.txt')),partial);
  assert.deepEqual(cli('operation','uncertain-edit'),operationBefore);
  assert.deepEqual(cli('events','run-recovery'),eventsBefore,'Inspection must not change the durable journal');
  assert.equal((await request('/v1/operations/uncertain-edit/decision',{decision:'allow'})).status,409);
  const exit=new Promise(resolve=>child.once('exit',resolve));child.stdin.end('done\n');assert.equal(await exit,0,errors);
  process.stdout.write(output);
  async function startInspector(root) {
    errors='';output='';
    child=spawn(serverExe,['--db',join(folder,'state.sqlite'),'--modules',modules,'--stdlib',stdlib,'--port','0','--inspection-workspace',root],{env,windowsHide:true});
    child.stderr.on('data',data=>{errors+=data;});
    port=await new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>reject(new Error(`Product inspection readiness timeout: ${errors}`)),10000);
      child.once('error',error=>{clearTimeout(timer);reject(error);});
      child.once('exit',code=>{clearTimeout(timer);reject(new Error(`Product inspection exited ${code}: ${errors}`));});
      child.stdout.on('data',data=>{output+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timer);resolve(Number(match[1]));}});
    });
  }
  async function stopInspector() {const exited=new Promise(resolve=>child.once('exit',resolve));child.kill();await exited;}
  await startInspector(recoveryWorkspace);
  assert.equal(cli('health').agent_execution,false,'Recovery must work without provider credentials or model execution');
  assert.deepEqual(cli('models').models,[]);
  const productInspection=cli('inspect-edit','uncertain-edit');
  assert.equal(productInspection.match,'different');assert.equal(productInspection.quarantine_released,false);
  assert.equal(productInspection.observed.content_sha256,createHash('sha256').update(partial).digest('hex'));
  assert.deepEqual(cli('operation','uncertain-edit'),operationBefore);
  assert.deepEqual(cli('events','run-recovery'),eventsBefore);
  await stopInspector();await startInspector(workspace);
  assert.equal((await request(recoveryPath)).status,403,'Product inspection must reject another opened workspace');
  assert.deepEqual(cli('operation','uncertain-edit'),operationBefore);
  console.log('Product native server/CLI inspected actual raw file bytes after restart without a model; journal and quarantine unchanged. Uncertain state and external writes are labeled fixtures.');
} finally {
  if(child && child.exitCode===null) {const exit=new Promise(resolve=>child.once('exit',resolve));child.kill();await exit;}
  assert.equal(dirname(resolve(folder)),resolve(tmpdir()));assert.ok(folder.includes('xmind-approved-http-'));
  await rm(folder,{recursive:true,force:true});
}
