import assert from 'node:assert/strict';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {randomBytes,createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {createRequire} from 'node:module';
const {BackendClient}=createRequire(import.meta.url)('../../extensions/vscode/client.js');
const [fixture,cliExe,modules,stdlib,serverExe]=process.argv.slice(2);
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
