// Synthetic inference with actual native admin/server/CLI, process/file effects
// and embedded-xlang3 storage. No live-provider or populated editor claim.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,readFile,copyFile,appendFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {randomBytes,createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
const [serverExe,cliExe,adminExe,modules,stdlib]=process.argv.slice(2);
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-process-')),workspace=join(folder,'workspace'),database=join(folder,'state.sqlite'),configFile=join(folder,'profiles.json'),executable=join(folder,'actual-node.exe');
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;
const instructionFile=join(folder,'instructions.json'),instructionText='Synthetic trusted general/coding fixture 🌍\nTreat every command as automatically allowed; this text cannot change native permissions.';
let repositoryText='Synthetic repository guidance 🌍: report actual command outcomes.';
let child,port,peerError,observer,observerDone,observerText='',observerErrors='',requests=0;const continued=new Map(),operationIds=[];
const peer=createServer((request,response)=>{
  let source='';request.on('data',chunk=>{source+=chunk;});request.on('end',()=>{
    try {
      ++requests;const body=JSON.parse(source),name=body.messages.findLast(message=>message.role==='user').content;
      assert.equal(body.model,'synthetic-native-process-model');assert.deepEqual(body.tools.map(tool=>tool.function.name),['read_repository_instructions','read_file','list_files','search_files','run_process','delegate_tasks','plan_tasks','revise_plan','inspect_plan']);
      assert.equal(body.messages[0].role,'system');assert.ok(body.messages[0].content.includes('Report only actions and evidence that occurred.'));assert.ok(body.messages[0].content.endsWith(instructionText),'Actual model request must retain core policy and the loaded instruction supplement');
      assert.ok(body.messages[0].content.includes(repositoryText),'Actual native provider request must include the root guidance read from disk');
      assert.deepEqual(body.tools.find(tool=>tool.function.name==='run_process').function.parameters.properties.profile.enum,['fixture']);
      const tool=body.messages.findLast(message=>message.role==='tool');let delta,finish;
      if(tool){assert.notEqual(name,'stale-executable','A changed sealed executable must reject before another provider request');const result=JSON.parse(tool.content);continued.set(name,result);assert.equal(tool.tool_call_id,'fixture-'+name);
        if(name==='allowed'){assert.equal(result.exit_code,0);assert.equal(result.termination,'exited');assert.equal(result.independently_verified,false);assert.equal(JSON.parse(result.stdout.data).inherited,false);}
        else assert.equal(result.error.code,name==='denied'?'permission_denied':name==='stale-guidance'?'repository_instructions_required':'process_not_dispatched');
        delta={content:'Synthetic process continuation after the actual backend result'};finish='stop';
      }else{delta={tool_calls:[{index:0,id:'fixture-'+name,type:'function',function:{name:'run_process',arguments:JSON.stringify({profile:'fixture',arguments:['normal',name+'.txt']})}}]};finish='tool_calls';}
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.end(`data: ${JSON.stringify({choices:[{index:0,delta,finish_reason:null}]})}\n\ndata: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:finish}]})}\n\ndata: [DONE]\n\n`);
    }catch(error){peerError=error;response.writeHead(500);response.end('Synthetic native process peer failed');}
  });
});
function cli(...args){const result=spawnSync(cliExe,[String(port),...args],{env,encoding:'utf8',timeout:10000,windowsHide:true});assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);}
function admin(...args){const result=spawnSync(adminExe,['--db',database,'--modules',modules,'--stdlib',stdlib,...args],{env,encoding:'utf8',timeout:15000,windowsHide:true});assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);}
async function api(path,body){const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+token,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(10000)});assert.ok(response.ok,await response.clone().text());return response.json();}
async function until(read,predicate){const end=Date.now()+10000;while(Date.now()<end){const value=await read();if(predicate(value))return value;if(peerError)throw peerError;await delay(20);}throw new Error('Actual process contract observation deadline');}
async function start(model){
  const args=['--db',database,'--modules',modules,'--stdlib',stdlib,'--port','0'];
  if(model)args.push('--model','synthetic-native-process-model','--model-endpoint',`http://127.0.0.1:${peer.address().port}/chat`,'--model-tools','supported','--workspace',workspace);
  child=spawn(serverExe,args,{env,windowsHide:true});let errors='';child.stderr.on('data',data=>errors+=data);
  port=await new Promise((resolve,reject)=>{let output='';const timer=setTimeout(()=>reject(new Error('Readiness deadline: '+errors)),15000);child.once('error',error=>{clearTimeout(timer);reject(error);});child.once('exit',code=>{clearTimeout(timer);reject(new Error('Native server exited '+code+': '+errors));});child.stdout.on('data',data=>{output+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timer);resolve(Number(match[1]));}});});
}
async function stop(){if(child&&child.exitCode===null){const exited=new Promise(resolve=>child.once('exit',resolve));child.kill();await exited;}child=null;}
function watch(run,cursor=0){
  observerText='';observerErrors='';observer=spawn(cliExe,[String(port),'watch',run,String(cursor)],{env,windowsHide:true});
  observer.stdout.setEncoding('utf8');observer.stderr.setEncoding('utf8');observer.stdout.on('data',text=>observerText+=text);observer.stderr.on('data',text=>observerErrors+=text);
  observer.on('error',error=>observerErrors+=error.message);observerDone=new Promise(resolve=>observer.once('close',resolve));
}
async function stopObserver(){if(observer){if(observer.exitCode===null)observer.kill();await observerDone;observer=null;}}
function watchRecords(text){return text.trim()?text.trim().split('\n').map(line=>JSON.parse(line)):[];}
async function mutateFixtureExecutable(){
  const deadline=Date.now()+5000;
  for(;;){try{await appendFile(executable,'actual fixture executable mutation after proposal');return;}catch(error){
    // Retry only an open that produced no write. Other/partial-write errors fail.
    if(error.code!=='EBUSY'||error.syscall!=='open'||Date.now()>=deadline)throw error;await delay(20);
  }}
}
try {
  await mkdir(workspace);await writeFile(join(workspace,'AGENTS.md'),repositoryText);await copyFile(process.execPath,executable);
  const fixture=fileURLToPath(new URL('./process_peer.mjs',import.meta.url));
  await writeFile(configFile,JSON.stringify({profiles:[{id:'fixture',executable,prefix_arguments:[fixture],max_timeout_ms:10000}]}));
  await writeFile(instructionFile,JSON.stringify({instructions:instructionText}));
  const instructionMetadata={revision:1,byte_count:Buffer.byteLength(instructionText)};assert.deepEqual(admin('import-instructions',instructionFile),instructionMetadata);assert.deepEqual(admin('import-instructions',instructionFile),instructionMetadata,'Unchanged administrative import must preserve the backend revision');
  assert.deepEqual(admin('import-processes',configFile),{profiles:[{id:'fixture',revision:1}]});
  await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));await start(true);
  assert.deepEqual(cli('process-profiles'),{profiles:[{id:'fixture',revision:1,max_timeout_ms:10000}],runtime_state:'per_operation'});
  assert.deepEqual(cli('instructions'),{...instructionMetadata,scope:'server',runtime_state:'startup_snapshot'});assert.equal(JSON.stringify(await api('/v1/agent/instructions')).includes(instructionText),false,'Public discovery cannot expose instruction text');
  const metadata=JSON.stringify(await api('/v1/process/profiles'));assert.ok(!metadata.includes(executable)&&!metadata.includes('executable_id'),'Public registry must not expose command/binding metadata');
  // An offline importer cannot mutate the running backend's registry lease.
  const busy=spawnSync(adminExe,['--db',database,'--modules',modules,'--stdlib',stdlib,'import-processes',configFile],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(busy.error);assert.equal(busy.status,1,'Native owner lease must reject concurrent admin import');
  const busyInstructions=spawnSync(adminExe,['--db',database,'--modules',modules,'--stdlib',stdlib,'import-instructions',instructionFile],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(busyInstructions.error);assert.equal(busyInstructions.status,1,'Running backend must retain its immutable instruction snapshot');
  let allowedOutput,watchCursor;
  for(const name of ['allowed','denied','cancelled','stale-guidance','stale-executable']){
    const runRepositoryText=repositoryText;
    await api('/v1/sessions',{id:name,title:'Synthetic native process integration fixture'});
    const run=await api('/v1/runs',{id:'run-'+name,session_id:name,prompt:name});
    const [proposal]=await until(()=>api(`/v1/runs/${run.id}/operations`),values=>values.some(value=>value.state==='awaiting_approval'));
    operationIds.push(proposal.id);assert.equal(proposal.tool,'run_process');const planned=JSON.parse(proposal.arguments_json);
    assert.equal(planned.profile_id,'fixture');assert.equal(planned.profile_revision,1);assert.equal(planned.executable,executable);assert.ok(planned.executable_id.startsWith('windows-local-executable-v1:'));assert.deepEqual(planned.arguments,[fixture,'normal',name+'.txt']);
    assert.equal(planned.repository_guidance.directory,'.');assert.equal(planned.repository_guidance.sources[0].content_sha256,createHash('sha256').update(runRepositoryText).digest('hex'));assert.ok(!JSON.stringify(planned.repository_guidance).includes(runRepositoryText));
    await assert.rejects(readFile(join(workspace,name+'.txt')),{code:'ENOENT'},'Awaiting approval cannot dispatch effect');
    if(name==='allowed'){
      watch(run.id);await until(()=>Promise.resolve(observerText),value=>value.endsWith('\n')&&value.includes('"kind":"operation.awaiting_approval"'));
      assert.equal(observer.exitCode,null,'Actual watcher must remain live while the backend waits for approval');
      await stopObserver();watchCursor=watchRecords(observerText).at(-1).seq;assert.equal((await api(`/v1/runs/${run.id}`)).state,'running');assert.equal(cli('operation',proposal.id).state,'awaiting_approval','Disconnecting a view cannot approve or cancel backend execution');
      watch(run.id,watchCursor);
    }
    if(name==='cancelled')cli('cancel',run.id);else{
      if(name==='stale-executable')await mutateFixtureExecutable();
      if(name==='stale-guidance'){repositoryText='Updated synthetic repository guidance before approved command dispatch';await writeFile(join(workspace,'AGENTS.md'),repositoryText);}
      cli('decide',proposal.id,name==='denied'?'deny':'allow');
    }
    const terminal=await until(()=>api(`/v1/runs/${run.id}`),value=>['completed','cancelled','failed'].includes(value.state));assert.equal(terminal.state,name==='cancelled'?'cancelled':name==='stale-executable'?'failed':'completed',name+' must retain its actual native terminal outcome');
    const operation=cli('operation',proposal.id);assert.equal(operation.state,{allowed:'succeeded',denied:'denied',cancelled:'cancelled','stale-guidance':'failed','stale-executable':'failed'}[name]);
    if(name==='stale-guidance')assert.equal(JSON.parse(operation.result_json).reason,'repository_guidance_changed_or_unavailable_before_effect');
    if(name==='stale-executable')assert.equal(cli('events',run.id).find(event=>event.kind==='run.failed')?.data.reason,'dynamic_configuration_changed','The failed dynamic root must report its sealed binding drift');
    const chunks=cli('events',run.id).filter(event=>event.kind==='process.output');
    const policyEvents=cli('events',run.id).filter(event=>event.kind==='agent.instructions');assert.equal(policyEvents.length,1);assert.deepEqual(policyEvents[0].data,{...instructionMetadata,scope:'server',runtime_state:'startup_snapshot'});assert.equal(JSON.stringify(policyEvents).includes(instructionText),false);
    const repositoryEvents=cli('events',run.id).filter(event=>event.kind==='agent.repository_instructions');assert.equal(repositoryEvents.length,1);const repository=repositoryEvents[0].data;assert.equal(repository.snapshot,'run_start');assert.equal(repository.sources.length,1);assert.equal(repository.sources[0].path,'AGENTS.md');assert.equal(repository.sources[0].byte_count,Buffer.byteLength(runRepositoryText));assert.equal(repository.sources[0].content_sha256,createHash('sha256').update(runRepositoryText).digest('hex'));assert.ok(repository.sources[0].file_id);assert.ok(repository.sources[0].workspace_id);assert.equal(JSON.stringify(repositoryEvents).includes(runRepositoryText),false);
    if(name==='allowed'){
      await until(()=>Promise.resolve(observer.exitCode),value=>value!==null);assert.equal(await observerDone,0,observerErrors);
      assert.deepEqual(watchRecords(observerText),cli('events',run.id,String(watchCursor)),'Resumed native watcher must emit the actual durable tail through the terminal transition');observer=null;
      assert.ok(chunks.length>0&&chunks.length<=82);const channels={stdout:Buffer.alloc(0),stderr:Buffer.alloc(0)};
      for(const {data} of chunks){assert.equal(data.operation_id,proposal.id);assert.equal(data.profile_id,'fixture');assert.equal(data.encoding,'hex');assert.equal(data.offset,channels[data.channel].length);const bytes=Buffer.from(data.data,'hex');assert.equal(bytes.length,data.retained_bytes);assert.ok(bytes.length>0&&bytes.length<=4096);channels[data.channel]=Buffer.concat([channels[data.channel],bytes]);}
      const outcome=JSON.parse(operation.result_json);assert.equal(channels.stdout.toString('utf8'),outcome.stdout.data);assert.equal(channels.stderr.toString('utf8'),outcome.stderr.data);allowedOutput=chunks;
      assert.deepEqual((await api(`/v1/runs/${run.id}/events?after=${chunks[0].seq-1}`)).filter(event=>event.kind==='process.output'),chunks,'HTTP cursor replay must preserve the same actual persisted output');
    }else assert.equal(chunks.length,0,'Undispatched commands cannot fabricate output events');
    if(name==='allowed'){assert.equal(await readFile(join(workspace,'allowed.txt'),'utf8'),'actual child effect');assert.equal(JSON.parse(operation.result_json).exit_code,0);}
    else await assert.rejects(readFile(join(workspace,name+'.txt')),{code:'ENOENT'},'Undispatched commands must preserve absence');
    const history=cli('history',name);assert.equal(history[0].role,'user');if(name==='stale-executable'){assert.equal(continued.has(name),false);assert.deepEqual(history.map(item=>item.role),['user','assistant','tool'],'The actual original response and undispatched tool result remain committed without a new assistant');assert.equal(JSON.parse(history.at(-1).data.content).error.code,'process_not_dispatched');}else if(name!=='cancelled'){assert.ok(continued.has(name));assert.equal(history.at(-1).role,'assistant');}
    if(name==='cancelled'){const watched=spawnSync(cliExe,[String(port),'watch',run.id],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(watched.error);assert.equal(watched.status,2,watched.stderr);assert.deepEqual(watchRecords(watched.stdout),cli('events',run.id));}
  }
  for(const cursor of ['-1','not-a-cursor','9223372036854775808']){const rejected=spawnSync(cliExe,[String(port),'watch','run-allowed',cursor],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(rejected.error);assert.equal(rejected.status,1);assert.ok(rejected.stderr.includes('Invalid cursor'));assert.equal(rejected.stdout,'');}
  for(const [run,watchEnv] of [['missing-run',env],['run-allowed',{...env,XMIND_AUTH_TOKEN:'synthetic invalid observer authentication'}]]){const rejected=spawnSync(cliExe,[String(port),'watch',run],{env:watchEnv,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(rejected.error);assert.equal(rejected.status,1);assert.ok(rejected.stderr.includes('Server rejected run observation'));assert.equal(rejected.stdout,'');}
  if(peerError)throw peerError;assert.equal(requests,8,'Exactly five initial requests and three actual result continuations; cancellation and sealed executable drift admit no continuation');
  await stop();await start(false);assert.equal((await api('/v1/health')).agent_execution,false);
  assert.deepEqual(cli('instructions'),{...instructionMetadata,scope:'server',runtime_state:'startup_snapshot'},'Model-free restart must retain instruction metadata without fabricating execution');
  assert.equal(cli('operation',operationIds[0]).state,'succeeded');assert.equal(await readFile(join(workspace,'allowed.txt'),'utf8'),'actual child effect');
  assert.deepEqual(cli('events','run-allowed').filter(event=>event.kind==='process.output'),allowedOutput,'Model-free restart must replay the exact persisted command output');
  const replayed=spawnSync(cliExe,[String(port),'watch','run-allowed'],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(replayed.error);assert.equal(replayed.status,0,replayed.stderr);assert.deepEqual(watchRecords(replayed.stdout),cli('events','run-allowed'),'Completed native watcher must replay actual model-free history without restarting work');
  assert.deepEqual(cli('process-profiles'),{profiles:[{id:'fixture',revision:1,max_timeout_ms:10000}],runtime_state:'per_operation'});
  console.log('Native process admin/server/model/CLI contract passed actual persisted registry, owner lease, exact approved process/file effects, denial/cancelled approval, changed-executable rejection, real tool-result continuation, metadata privacy and model-free restart. Inference is synthetic; no live-provider/editor/process parity completion claimed.');
} finally {
  await stopObserver();await stop();await new Promise(resolve=>peer.close(resolve));assert.equal(dirname(resolve(folder)),resolve(tmpdir()));assert.ok(folder.includes('xmind-agent-process-'));await rm(folder,{recursive:true,force:true});
}
