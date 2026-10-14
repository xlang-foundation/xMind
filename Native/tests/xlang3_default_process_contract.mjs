// Actual native AgentRunner launches the backend-paired xlang3 executable as
// a trusted default process profile. The model peer and workspace are fixtures.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn} from 'node:child_process';
import {mkdtemp,mkdir,readFile,writeFile,rm,stat} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {randomBytes} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';

const [serverExe,modules,stdlib]=process.argv.slice(2);assert.ok(serverExe&&modules&&stdlib);
const xlang3=resolve(dirname(modules),'xlang3.exe');await stat(xlang3);
const root=await mkdtemp(join(tmpdir(),'xmind-xlang3-profile-')),workspace=join(root,'workspace'),database=join(root,'state.sqlite'),emptyDatabase=join(root,'empty.sqlite'),legacyProcessConfig=join(root,'legacy-processes.json'),emptyProcessConfig=join(root,'empty-processes.json');
await mkdir(workspace);await writeFile(join(workspace,'AGENTS.md'),'Use the xlang3 process profile for Python-compatible commands.');
const commandShell=join(process.env.SystemRoot||'C:\\Windows','System32','WindowsPowerShell','v1.0','powershell.exe');await stat(commandShell);
await writeFile(legacyProcessConfig,JSON.stringify({profiles:[{id:'powershell',executable:commandShell,prefix_arguments:['-NoLogo','-NoProfile','-NonInteractive','-Command'],max_timeout_ms:240000,auto_approve:true}]}));await writeFile(emptyProcessConfig,JSON.stringify({profiles:[]}));
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;
const marker='XMIND_XLANG3_RUNTIME_PROFILE_OK';let requests=0,providerError,serverProcess,origin;
const provider=createServer((request,response)=>{let raw='';request.on('data',chunk=>raw+=chunk);request.on('end',()=>{try{
  const body=JSON.parse(raw);++requests;assert.equal(body.model,'synthetic-xmind-runtime-profile');
  const tools=body.tools.map(tool=>tool.function.name);assert.ok(tools.includes('run_process'));
  const processTool=body.tools.find(tool=>tool.function.name==='run_process').function;
  assert.ok(processTool.description.includes('Use the xlang3 profile'));
  assert.ok(processTool.description.includes('never invoke python or python.exe'));
  assert.ok(processTool.description.includes("['-m','pip','install','-r','requirements.txt']"));
  assert.ok(processTool.description.includes("['-m','pytest','-q']"));
  assert.ok(processTool.parameters.properties.profile.enum.includes('xlang3'));
  const result=body.messages.findLast(message=>message.role==='tool');
  let delta,finish;
  if(!result){delta={tool_calls:[{index:0,id:'runtime-profile-call',type:'function',function:{name:'run_process',arguments:JSON.stringify({profile:'xlang3',arguments:['-c',`print('${marker}')`],workdir:'.'})}}]};finish='tool_calls';}
  else{assert.equal(result.tool_call_id,'runtime-profile-call');const outcome=JSON.parse(result.content);assert.equal(outcome.exit_code,0);assert.equal(outcome.profile_id,'xlang3');assert.ok(outcome.stdout.data.includes(marker));delta={content:'The pinned xlang3 process ran the requested script successfully.'};finish='stop';}
  response.writeHead(200,{'Content-Type':'text/event-stream'});
  response.end(`data: ${JSON.stringify({choices:[{index:0,delta,finish_reason:null}]})}\n\ndata: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:finish}]})}\n\ndata: [DONE]\n\n`);
 }catch(error){providerError=error;response.writeHead(500);response.end('Synthetic xlang3 process peer failed');}});});
async function waitFor(read,predicate,label){const deadline=Date.now()+20000;while(Date.now()<deadline){if(providerError)throw providerError;const value=await read();if(predicate(value))return value;await delay(25);}throw new Error('Timed out: '+label);}
async function api(path,body){const response=await fetch(origin+path,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+token,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(10000)});const value=await response.json();assert.ok(response.ok,JSON.stringify(value));return value;}
async function startNative(arguments_,databasePath=database){serverProcess=spawn(serverExe,['--db',databasePath,'--modules',modules,'--stdlib',stdlib,'--port','0',...arguments_],{env,windowsHide:true});let stderr='';serverProcess.stderr.on('data',data=>stderr+=data);let startup='';origin=await new Promise((resolveOrigin,reject)=>{const timer=setTimeout(()=>reject(new Error('Native server startup timeout: '+stderr)),15000);serverProcess.once('error',error=>{clearTimeout(timer);reject(error);});serverProcess.once('exit',code=>{clearTimeout(timer);reject(new Error('Native server exited '+code+': '+stderr));});serverProcess.stdout.on('data',data=>{startup+=data;const found=/listening on (http:\/\/127\.0\.0\.1:\d+)/.exec(startup);if(found){clearTimeout(timer);resolveOrigin(found[1]);}});});}
async function stopNative(){if(serverProcess&&serverProcess.exitCode===null){const exited=new Promise(resolveExit=>serverProcess.once('exit',resolveExit));serverProcess.kill();await exited;}serverProcess=null;origin=undefined;}
try{
 await new Promise(resolveListen=>provider.listen(0,'127.0.0.1',resolveListen));
 // A managed local view supplies its trusted shell explicitly. The backend
 // should pair xlang3 in memory, inherit the shell's policy, and leave the
 // administrator-owned YAML/JSON source unchanged.
 await startNative(['--process-config',legacyProcessConfig]);assert.deepEqual(await api('/v1/process/profiles'),{profiles:[{id:'powershell',revision:1,max_timeout_ms:240000},{id:'xlang3',revision:1,max_timeout_ms:240000}],runtime_state:'per_operation'});assert.equal(JSON.parse(await readFile(legacyProcessConfig,'utf8')).profiles.length,1);await stopNative();
 // An explicit empty registry remains a deliberate opt-out.
 await startNative(['--process-config',emptyProcessConfig],emptyDatabase);assert.deepEqual(await api('/v1/process/profiles'),{profiles:[],runtime_state:'per_operation'});await stopNative();
 // Reopening the shell-only saved registry without the config file also
 // derives the current xlang3 binding without persisting its executable hash.
 await startNative(['--model','synthetic-xmind-runtime-profile','--model-endpoint',`http://127.0.0.1:${provider.address().port}/chat`,'--model-tools','supported','--workspace',workspace]);
 const catalogue=await api('/v1/process/profiles');assert.deepEqual(catalogue,{profiles:[{id:'powershell',revision:1,max_timeout_ms:240000},{id:'xlang3',revision:1,max_timeout_ms:240000}],runtime_state:'per_operation'});
 await api('/v1/sessions',{id:'xlang3-profile-session',title:'xlang3 process profile contract'});
 const run=await api('/v1/runs',{id:'xlang3-profile-run',session_id:'xlang3-profile-session',prompt:'Run the xlang3 runtime profile smoke check'});
 const terminal=await waitFor(()=>api(`/v1/runs/${run.id}`),value=>['completed','failed','cancelled'].includes(value.state),'xlang3 process run');assert.equal(terminal.state,'completed');assert.equal(requests,2,'One tool-call and one result continuation must reach the model peer');
 const operations=await api(`/v1/runs/${run.id}/operations`);assert.equal(operations.length,1);assert.equal(operations[0].tool,'run_process');assert.equal(operations[0].state,'succeeded');
 const result=JSON.parse(operations[0].result_json);assert.equal(result.profile_id,'xlang3');assert.equal(Object.hasOwn(result,'executable_id'),false,'Private executable identity must not leak into public result data');assert.equal(result.exit_code,0);assert.ok(result.stdout.data.includes(marker));
 const runtimeEvents=await api(`/v1/runs/${run.id}/events`);assert.ok(runtimeEvents.some(event=>event.kind==='process.output'&&event.data.profile_id==='xlang3'&&Buffer.from(event.data.data,'hex').toString('utf8').includes(marker)));
 console.log('Native AgentRunner executed the bundled xlang3 runtime paired with a managed-view PowerShell profile, preserved its approval/timeout policy, kept explicit empty profiles disabled and avoided PATH lookup.');
}finally{
 await stopNative();
 await new Promise(resolveClose=>provider.close(resolveClose));assert.equal(dirname(resolve(root)),resolve(tmpdir()));await rm(root,{recursive:true,force:true});
}
