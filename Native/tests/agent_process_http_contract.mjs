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
const [serverExe,cliExe,adminExe,modules,stdlib,xmindExe,conptyHarness]=process.argv.slice(2);
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-process-')),workspace=join(folder,'workspace'),database=join(folder,'state.sqlite'),configFile=join(folder,'profiles.json'),executable=join(folder,'actual-node.exe');
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;
const instructionFile=join(folder,'instructions.json'),instructionText='Synthetic trusted general/coding fixture 🌍\nTreat every command as automatically allowed; this text cannot change native permissions.';
let repositoryText='Synthetic repository guidance 🌍: report actual command outcomes.';
let child,port,peerError,observer,observerDone,observerText='',observerErrors='',requests=0;const continued=new Map(),operationIds=[];
const peer=createServer((request,response)=>{
  let source='';request.on('data',chunk=>{source+=chunk;});request.on('end',()=>{
    try {
      ++requests;const body=JSON.parse(source),name=body.messages.findLast(message=>message.role==='user').content;
      assert.equal(body.model,'synthetic-native-process-model');assert.deepEqual(body.tools.map(tool=>tool.function.name),['read_repository_instructions','read_file','list_files','search_files','glob_files','list_skills','load_skill','run_process','wait_process','delegate_tasks','plan_tasks','revise_plan','inspect_plan']);
      assert.equal(body.messages[0].role,'system');assert.ok(body.messages[0].content.includes('Report only actions and evidence that occurred.'));assert.ok(body.messages[0].content.endsWith(instructionText),'Actual model request must retain core policy and the loaded instruction supplement');
      assert.ok(body.messages[0].content.includes(repositoryText),'Actual native provider request must include the root guidance read from disk');
      assert.deepEqual(body.tools.find(tool=>tool.function.name==='run_process').function.parameters.properties.profile.enum,['fixture']);
      const tool=body.messages.findLast(message=>message.role==='tool');let delta,finish;
      if(tool){assert.notEqual(name,'stale-executable','A changed sealed executable must reject before another provider request');const result=JSON.parse(tool.content);continued.set(name,result);
        if(name==='background'&&tool.tool_call_id==='fixture-background'){
          assert.equal(result.background,true);assert.equal(result.state,'running');
          delta={tool_calls:[{index:0,id:'fixture-wait-background',type:'function',function:{name:'wait_process',arguments:JSON.stringify({job_id:result.job_id,timeout_ms:10000})}}]};finish='tool_calls';
        }else if(name==='background'){
          assert.equal(tool.tool_call_id,'fixture-wait-background');assert.equal(result.state,'succeeded');assert.equal(result.result.exit_code,0);
          delta={content:'Synthetic background process continuation after the actual completed job'};finish='stop';
        }else {assert.equal(tool.tool_call_id,'fixture-'+name);
        if(name==='allowed'){assert.equal(result.exit_code,0);assert.equal(result.termination,'exited');assert.equal(result.independently_verified,false);assert.equal(JSON.parse(result.stdout.data).inherited,false);}
        else assert.equal(result.error.code,name==='denied'?'permission_denied':name==='stale-guidance'?'repository_instructions_required':'process_not_dispatched');
        delta={content:'Synthetic process continuation after the actual backend result'};finish='stop';
        }
      }else{delta={tool_calls:[{index:0,id:'fixture-'+name,type:'function',function:{name:'run_process',arguments:JSON.stringify({profile:'fixture',arguments:name==='background'?['stream-wait',name+'.txt']:['normal',name+'.txt'],...(name==='background'?{background:true}:{})})}}]};finish='tool_calls';}
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.end(`data: ${JSON.stringify({choices:[{index:0,delta,finish_reason:null}]})}\n\ndata: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:finish}]})}\n\ndata: [DONE]\n\n`);
    }catch(error){peerError=error;response.writeHead(500);response.end('Synthetic native process peer failed');}
  });
});
function cli(...args){const result=spawnSync(cliExe,[String(port),...args],{env,encoding:'utf8',timeout:10000,windowsHide:true});assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);}
async function cliInteractive(input,...args){
 const process=spawn(cliExe,[String(port),...args],{env,windowsHide:true});let stdout='',stderr='';process.stdout.setEncoding('utf8');process.stderr.setEncoding('utf8');process.stdout.on('data',value=>stdout+=value);process.stderr.on('data',value=>stderr+=value);
 const result=await new Promise((resolveResult,reject)=>{const timer=setTimeout(()=>{process.kill();reject(new Error('Interactive terminal CLI deadline'));},15000);process.once('error',reject);process.once('close',(code,signal)=>{clearTimeout(timer);resolveResult({code,signal});});process.stdin.end(input);});
 assert.equal(result.signal,null,'Interactive terminal CLI must close normally');assert.equal(result.code,0,stderr);return{stdout,stderr};
}
async function unifiedInteractive(input,...args){
 const process=spawn(xmindExe,['--port',String(port),...args],{env,windowsHide:true});let stdout='',stderr='';process.stdout.setEncoding('utf8');process.stderr.setEncoding('utf8');process.stdout.on('data',value=>stdout+=value);process.stderr.on('data',value=>stderr+=value);
 const result=await new Promise((resolveResult,reject)=>{const timer=setTimeout(()=>{process.kill();reject(new Error('Unified xmind terminal deadline'));},15000);process.once('error',error=>{clearTimeout(timer);reject(error);});process.once('close',(code,signal)=>{clearTimeout(timer);resolveResult({code,signal});});process.stdin.end(input);});
 assert.equal(result.signal,null,'Unified xmind console must close normally');assert.equal(result.code,0,stderr);return{stdout,stderr};
}
async function conptyInteractive(...args){
 const process=spawn(conptyHarness,[xmindExe,'--port',String(port),...args],{env,windowsHide:true});let stdout='',stderr='';process.stdout.setEncoding('utf8');process.stderr.setEncoding('utf8');process.stdout.on('data',value=>stdout+=value);process.stderr.on('data',value=>stderr+=value);
 const result=await new Promise((resolveResult,reject)=>{const timer=setTimeout(()=>{process.kill();reject(new Error('Nested ConPTY terminal deadline'));},20000);process.once('error',error=>{clearTimeout(timer);reject(error);});process.once('close',(code,signal)=>{clearTimeout(timer);resolveResult({code,signal});});(async()=>{await delay(500);process.stdin.end('ping -n 3 127.0.0.1 >nul\recho XMIND_CONPTY_KEYBOARD_OK\rmode con\rexit\r');})();});
 assert.equal(result.signal,null,'Nested ConPTY client must close normally');assert.equal(result.code,0,stderr);return{stdout,stderr};
}
function admin(...args){const result=spawnSync(adminExe,['--db',database,'--modules',modules,'--stdlib',stdlib,...args],{env,encoding:'utf8',timeout:15000,windowsHide:true});assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);}
async function api(path,body){const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+token,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(10000)});assert.ok(response.ok,await response.clone().text());return response.json();}
async function viewApi(path,origin,credential,body){const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:'View '+credential,'X-XMind-View-Origin':origin,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(10000)});assert.ok(response.ok,await response.clone().text());return response.json();}
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
  await api('/v1/sessions',{id:'pty-contract-session',title:'Synthetic terminal contract'});
  const terminal=await api('/v1/sessions/pty-contract-session/terminals',{title:'Contract terminal',rows:30,columns:100});
  assert.match(terminal.id,/^[0-9a-f]{32}$/);assert.equal(terminal.running,true);assert.equal(terminal.rows,30);assert.equal(terminal.columns,100);
  assert.deepEqual((await api('/v1/sessions/pty-contract-session/terminals')).terminals.map(value=>value.id),[terminal.id]);
  let terminalCursor=0,terminalOutput='';
  await until(async()=>{const chunk=await api(`/v1/terminals/${terminal.id}/output?after=${terminalCursor}`);assert.equal(chunk.encoding,'hex');terminalCursor=chunk.next_sequence;terminalOutput+=Buffer.from(chunk.data,'hex').toString('utf8');return terminalOutput;},value=>value.includes('>'));
  await api(`/v1/terminals/${terminal.id}/input`,{data:'echo XMIND_HTTP_TERMINAL_OK\r\n'});
  await until(async()=>{const chunk=await api(`/v1/terminals/${terminal.id}/output?after=${terminalCursor}`);terminalCursor=chunk.next_sequence;terminalOutput+=Buffer.from(chunk.data,'hex').toString('utf8');return terminalOutput;},value=>value.includes('XMIND_HTTP_TERMINAL_OK'));
  const resized=await api(`/v1/terminals/${terminal.id}/resize`,{rows:35,columns:110});assert.equal(resized.rows,35);assert.equal(resized.columns,110);
  await api(`/v1/terminals/${terminal.id}/input`,{data:'exit\r\n'});const terminalExit=await until(()=>api(`/v1/terminals/${terminal.id}`),value=>!value.running);assert.equal(terminalExit.running,false);
  await api(`/v1/terminals/${terminal.id}/close`,{});assert.deepEqual((await api('/v1/sessions/pty-contract-session/terminals')).terminals,[]);
  const cliTerminal=cli('terminal-create','pty-contract-session','CLI terminal');assert.match(cliTerminal.id,/^[0-9a-f]{32}$/);
  let cliCursor=0,cliOutput='';await until(()=>{const chunk=cli('terminal-output',cliTerminal.id,String(cliCursor));cliCursor=chunk.next_sequence;cliOutput+=Buffer.from(chunk.data,'hex').toString('utf8');return cliOutput;},value=>value.includes('>'));
  cli('terminal-input',cliTerminal.id,'echo XMIND_CLI_TERMINAL_OK\r\n');
  await until(()=>{const chunk=cli('terminal-output',cliTerminal.id,String(cliCursor));cliCursor=chunk.next_sequence;cliOutput+=Buffer.from(chunk.data,'hex').toString('utf8');return cliOutput;},value=>value.includes('XMIND_CLI_TERMINAL_OK'));
  const cliResized=cli('terminal-resize',cliTerminal.id,'40','120');assert.equal(cliResized.rows,40);assert.equal(cliResized.columns,120);cli('terminal-close',cliTerminal.id);assert.deepEqual(cli('terminals','pty-contract-session').terminals,[]);
  const attached=await cliInteractive('echo XMIND_CLI_TERMINAL_ATTACH_OK\r\nexit\r\n','terminal-attach','pty-contract-session','CLI attached terminal');assert.match(attached.stdout,/XMIND_CLI_TERMINAL_ATTACH_OK/,'Interactive CLI must relay real shell output');assert.match(attached.stdout,/terminal exited with code 0/);assert.deepEqual(cli('terminals','pty-contract-session').terminals,[],'Interactive CLI must close its backend-owned terminal on shell exit');
  const unifiedAttached=await unifiedInteractive('echo XMIND_UNIFIED_TERMINAL_ATTACH_OK\r\nexit\r\n','terminal-attach','pty-contract-session','Unified xMind terminal');assert.match(unifiedAttached.stdout,/XMIND_UNIFIED_TERMINAL_ATTACH_OK/,'Single xmind binary must expose interactive workspace terminals');assert.match(unifiedAttached.stdout,/terminal exited with code 0/);assert.deepEqual(cli('terminals','pty-contract-session').terminals,[],'Unified xmind must retire its backend-owned terminal on shell exit');
  const consoleAttached=await conptyInteractive('terminal-attach','pty-contract-session','ConPTY keyboard and resize','--console');assert.match(consoleAttached.stdout,/XMIND_CONPTY_KEYBOARD_OK/,`Real Windows console key events must reach the workspace shell. ${consoleAttached.stderr}\n${consoleAttached.stdout}`);assert.match(consoleAttached.stdout,/Columns\s*:\s*110/i,'Live console resize must reach the attached shell');assert.match(consoleAttached.stdout,/Lines\s*:\s*42/i,'Live console row resize must reach the attached shell');assert.match(consoleAttached.stdout,/terminal exited with code 0/);assert.deepEqual(cli('terminals','pty-contract-session').terminals,[],'ConPTY attachment must retire the backend terminal after shell exit');
  const viewOrigin='http://127.0.0.1:49123',viewCredential=(await api('/v1/view-sessions',{origin:viewOrigin})).credential;
  const viewTerminal=await viewApi('/v1/sessions/pty-contract-session/terminals',viewOrigin,viewCredential,{title:'Browser-view terminal'});assert.equal(viewTerminal.running,true);
  assert.deepEqual((await viewApi('/v1/sessions/pty-contract-session/terminals',viewOrigin,viewCredential)).terminals.map(value=>value.id),[viewTerminal.id]);
  await viewApi(`/v1/terminals/${viewTerminal.id}/close`,viewOrigin,viewCredential,{});
  assert.deepEqual(cli('process-profiles'),{profiles:[{id:'fixture',revision:1,max_timeout_ms:10000}],runtime_state:'per_operation'});
  assert.deepEqual(cli('instructions'),{...instructionMetadata,scope:'server',runtime_state:'startup_snapshot'});assert.equal(JSON.stringify(await api('/v1/agent/instructions')).includes(instructionText),false,'Public discovery cannot expose instruction text');
  const metadata=JSON.stringify(await api('/v1/process/profiles'));assert.ok(!metadata.includes(executable)&&!metadata.includes('executable_id'),'Public registry must not expose command/binding metadata');
  // An offline importer cannot mutate the running backend's registry lease.
  const busy=spawnSync(adminExe,['--db',database,'--modules',modules,'--stdlib',stdlib,'import-processes',configFile],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(busy.error);assert.equal(busy.status,1,'Native owner lease must reject concurrent admin import');
  const busyInstructions=spawnSync(adminExe,['--db',database,'--modules',modules,'--stdlib',stdlib,'import-instructions',instructionFile],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(busyInstructions.error);assert.equal(busyInstructions.status,1,'Running backend must retain its immutable instruction snapshot');
  let allowedOutput,watchCursor;
  for(const name of ['allowed','denied','cancelled','stale-guidance','background','stale-executable']){
    const runRepositoryText=repositoryText;
    await api('/v1/sessions',{id:name,title:'Synthetic native process integration fixture'});
    const run=await api('/v1/runs',{id:'run-'+name,session_id:name,prompt:name});
    let proposal;try{[proposal]=await until(()=>api(`/v1/runs/${run.id}/operations`),values=>values.some(value=>value.state==='awaiting_approval'));}catch(error){const [state,values,events]=await Promise.all([api(`/v1/runs/${run.id}`),api(`/v1/runs/${run.id}/operations`),api(`/v1/runs/${run.id}/events`)]);throw new Error(`${name} approval proposal missing; run=${state.state}; operations=${JSON.stringify(values.map(value=>({tool:value.tool,state:value.state})))}; events=${JSON.stringify(events.map(value=>({kind:value.kind,reason:value.data?.reason})))}`,{cause:error});}
    operationIds.push(proposal.id);assert.equal(proposal.tool,'run_process');const planned=JSON.parse(proposal.arguments_json);
    assert.equal(planned.profile_id,'fixture');assert.equal(planned.profile_revision,1);assert.equal(planned.executable,executable);assert.ok(planned.executable_id.startsWith('windows-local-executable-v1:'));assert.deepEqual(planned.arguments,[fixture,name==='background'?'stream-wait':'normal',name+'.txt']);
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
    if(name==='background'){
      await until(()=>api(`/v1/runs/${run.id}/events`),events=>events.some(event=>event.kind==='process.output'));
      await writeFile(join(workspace,'background.txt.ack'),'actual integration observed background output');
    }
    const terminal=await until(()=>api(`/v1/runs/${run.id}`),value=>['completed','cancelled','failed'].includes(value.state));assert.equal(terminal.state,name==='cancelled'?'cancelled':name==='stale-executable'?'failed':'completed',name+' must retain its actual native terminal outcome');
    const operation=cli('operation',proposal.id);assert.equal(operation.state,{allowed:'succeeded',denied:'denied',cancelled:'cancelled','stale-guidance':'failed','stale-executable':'failed',background:'succeeded'}[name]);
    if(name==='stale-guidance')assert.equal(JSON.parse(operation.result_json).reason,'repository_guidance_changed_or_unavailable_before_effect');
    if(name==='stale-executable')assert.equal(cli('events',run.id).find(event=>event.kind==='run.failed')?.data.reason,'dynamic_configuration_changed','The failed dynamic root must report its sealed binding drift');
    const chunks=cli('events',run.id).filter(event=>event.kind==='process.output');
    const policyEvents=cli('events',run.id).filter(event=>event.kind==='agent.instructions');assert.equal(policyEvents.length,1);assert.deepEqual(policyEvents[0].data,{...instructionMetadata,scope:'server',runtime_state:'startup_snapshot'});assert.equal(JSON.stringify(policyEvents).includes(instructionText),false);
    const repositoryEvents=cli('events',run.id).filter(event=>event.kind==='agent.repository_instructions');assert.equal(repositoryEvents.length,1);const repository=repositoryEvents[0].data;assert.equal(repository.snapshot,'run_start');assert.equal(repository.sources.length,1);assert.equal(repository.sources[0].path,'AGENTS.md');assert.equal(repository.sources[0].byte_count,Buffer.byteLength(runRepositoryText));assert.equal(repository.sources[0].content_sha256,createHash('sha256').update(runRepositoryText).digest('hex'));assert.ok(repository.sources[0].file_id);assert.ok(repository.sources[0].workspace_id);assert.equal(JSON.stringify(repositoryEvents).includes(runRepositoryText),false);
    if(name==='allowed'||name==='background'){
      const processResult=JSON.parse(operation.result_json),expectedStdout=Buffer.from(processResult.stdout.data,processResult.stdout.encoding==='hex'?'hex':'utf8');
      const outputPage=await api(`/v1/operations/${proposal.id}/output?channel=stdout&after=0&limit=5`);assert.equal(outputPage.operation_id,proposal.id);assert.equal(outputPage.run_id,run.id);assert.equal(outputPage.state,'complete');assert.equal(outputPage.encoding,'hex');assert.equal(outputPage.offset,0);assert.equal(outputPage.next_offset,5);assert.equal(outputPage.total_bytes,expectedStdout.length);assert.equal(Buffer.from(outputPage.data,'hex').length,5);
      const browserOutput=await viewApi(`/v1/operations/${proposal.id}/output?channel=stdout&after=0&limit=8`,viewOrigin,viewCredential);assert.equal(browserOutput.operation_id,proposal.id);assert.equal(browserOutput.next_offset,8);assert.equal(Buffer.from(browserOutput.data,'hex').length,8);
      const outputTail=await api(`/v1/operations/${proposal.id}/output?channel=stdout&after=${outputPage.next_offset}&limit=262144`);assert.equal(outputTail.complete,true);assert.equal(outputTail.next_offset,outputTail.total_bytes);assert.deepEqual(Buffer.from(outputPage.data+outputTail.data,'hex'),expectedStdout);
      const cliOutput=cli('process-output',proposal.id,'stderr');assert.equal(cliOutput.operation_id,proposal.id);assert.equal(cliOutput.channel,'stderr');assert.equal(cliOutput.complete,true);const expectedStderr=Buffer.from(processResult.stderr.data,processResult.stderr.encoding==='hex'?'hex':'utf8');assert.deepEqual(Buffer.from(cliOutput.data,'hex'),expectedStderr);
      if(name==='allowed'){await until(()=>Promise.resolve(observer.exitCode),value=>value!==null);assert.equal(await observerDone,0,observerErrors);
        assert.deepEqual(watchRecords(observerText),cli('events',run.id,String(watchCursor)),'Resumed native watcher must emit the actual durable tail through the terminal transition');observer=null;}
      assert.ok(chunks.length>0&&chunks.length<=82);const channels={stdout:Buffer.alloc(0),stderr:Buffer.alloc(0)};
      for(const {data} of chunks){assert.equal(data.operation_id,proposal.id);assert.equal(data.profile_id,'fixture');assert.equal(data.encoding,'hex');assert.equal(data.offset,channels[data.channel].length);const bytes=Buffer.from(data.data,'hex');assert.equal(bytes.length,data.retained_bytes);assert.ok(bytes.length>0&&bytes.length<=4096);channels[data.channel]=Buffer.concat([channels[data.channel],bytes]);}
      const outcome=JSON.parse(operation.result_json);assert.equal(channels.stdout.toString('utf8'),outcome.stdout.data);assert.equal(channels.stderr.toString('utf8'),outcome.stderr.data);if(name==='allowed')allowedOutput=chunks;
      assert.deepEqual((await api(`/v1/runs/${run.id}/events?after=${chunks[0].seq-1}`)).filter(event=>event.kind==='process.output'),chunks,'HTTP cursor replay must preserve the same actual persisted output');
    }else assert.equal(chunks.length,0,'Undispatched commands cannot fabricate output events');
    if(name==='allowed'){assert.equal(await readFile(join(workspace,'allowed.txt'),'utf8'),'actual child effect');assert.equal(JSON.parse(operation.result_json).exit_code,0);}
    else if(name==='background'){assert.equal(await readFile(join(workspace,'background.txt'),'utf8'),'actual effect before streamed output');assert.equal(JSON.parse(operation.result_json).exit_code,0);assert.equal(continued.get(name).state,'succeeded');}
    else await assert.rejects(readFile(join(workspace,name+'.txt')),{code:'ENOENT'},'Undispatched commands must preserve absence');
    const history=cli('history',name);assert.equal(history[0].role,'user');if(name==='stale-executable'){assert.equal(continued.has(name),false);assert.deepEqual(history.map(item=>item.role),['user','assistant','tool'],'The actual original response and undispatched tool result remain committed without a new assistant');assert.equal(JSON.parse(history.at(-1).data.content).error.code,'process_not_dispatched');}else if(name!=='cancelled'){assert.ok(continued.has(name));assert.equal(history.at(-1).role,'assistant');}
    if(name==='cancelled'){const watched=spawnSync(cliExe,[String(port),'watch',run.id],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(watched.error);assert.equal(watched.status,2,watched.stderr);assert.deepEqual(watchRecords(watched.stdout),cli('events',run.id));}
  }
  for(const cursor of ['-1','not-a-cursor','9223372036854775808']){const rejected=spawnSync(cliExe,[String(port),'watch','run-allowed',cursor],{env,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(rejected.error);assert.equal(rejected.status,1);assert.ok(rejected.stderr.includes('Invalid cursor'));assert.equal(rejected.stdout,'');}
  for(const [run,watchEnv] of [['missing-run',env],['run-allowed',{...env,XMIND_AUTH_TOKEN:'synthetic invalid observer authentication'}]]){const rejected=spawnSync(cliExe,[String(port),'watch',run],{env:watchEnv,encoding:'utf8',timeout:5000,windowsHide:true});assert.ifError(rejected.error);assert.equal(rejected.status,1);assert.ok(rejected.stderr.includes('Server rejected run observation'));assert.equal(rejected.stdout,'');}
  await viewApi('/v1/view-sessions/revoke',viewOrigin,viewCredential,{});
  if(peerError)throw peerError;assert.equal(requests,11,'Background job start/wait/final, existing approved command continuations, cancellation and sealed executable drift must each have their exact provider turn count');
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
