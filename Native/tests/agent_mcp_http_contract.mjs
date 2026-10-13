// Synthetic inference, actual compiled server/admin/CLI, encrypted xlang3
// persistence and external MCP subprocess/file effects. No live-model claim.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {randomBytes} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
const [serverExe,cliExe,adminExe,modules,stdlib,sdkEra,programMode]=process.argv.slice(2);
const unified=programMode==='unified';
assert.ok(programMode===undefined||unified,'Unknown native program mode');
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-mcp-')),workspace=join(folder,'workspace');
const db=join(folder,'state.sqlite'),effect=join(workspace,'effect.txt'),marker=join(folder,'marker');
if(sdkEra&&!['modern','legacy'].includes(sdkEra))throw new Error('Unknown SDK fixture era');
const fixture=fileURLToPath(new URL(sdkEra?'./sdk/peer.mjs':'./mcp_effect_peer.mjs',import.meta.url));
const secret='synthetic-mcp-credential-fixture',token=randomBytes(32).toString('hex');
const env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;delete env.XMIND_SETUP_SECRET;
let child,port,errors='',peerError,requests=0;const continuations=new Map();
const peer=createServer((request,response)=>{
  let source='';request.on('data',chunk=>{source+=chunk;});request.on('end',()=>{
    try {
      requests++;const body=JSON.parse(source),prompt=body.messages.findLast(message=>message.role==='user').content;
      const names=body.tools.map(tool=>tool.function.name),mcpNames=names.filter(name=>/^mcp_[a-f0-9]{48}$/.test(name));
      assert.deepEqual(names.slice(0,8),['read_repository_instructions','read_file','list_files','search_files','find_files','glob_files','list_skills','load_skill']);assert.equal(mcpNames.length,1,JSON.stringify(names));assert.ok(names.includes('delegate_tasks'));
      const allowedExtras=new Set(['run_process','wait_process','plan_tasks','revise_plan','inspect_plan','delegate_tasks',mcpNames[0]]);assert.ok(names.slice(8).every(name=>allowedExtras.has(name)),JSON.stringify(names));
      if(names.includes('run_process'))assert.ok(names.includes('wait_process'),'Process execution must expose its paired join tool');
      const planningNames=['plan_tasks','revise_plan','inspect_plan'].filter(name=>names.includes(name));assert.ok(planningNames.length===0||planningNames.length===3,'Planning tools must be exposed as a complete set');
      assert.equal(new Set(names).size,names.length,'Tool names must be unique');assert.equal(body.stream_options.include_usage,true);
      assert.equal(source.includes(secret),false);const tool=body.messages.findLast(message=>message.role==='tool');let delta,finish;
      if(tool){
        const outcome=JSON.parse(tool.content);continuations.set(prompt,outcome);
        if(prompt==='allowed'){assert.equal(outcome.acknowledged_by_peer,true);assert.equal(outcome.independently_verified,false);assert.equal(JSON.parse(outcome.response_json).result.structuredContent.bytes,7);}
        else assert.equal(outcome.error.code,'permission_denied');
        delta={content:'Synthetic model continuation after actual MCP outcome'};finish='stop';
      }else{delta={tool_calls:[{index:0,id:`fixture-${prompt}`,type:'function',function:{name:mcpNames[0],arguments:`{"body":${JSON.stringify(prompt)},"decimal":1.00000000000000000001}`}}]};finish='tool_calls';}
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.end(`data: ${JSON.stringify({choices:[{index:0,delta,finish_reason:null}]})}\n\ndata: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:finish}]})}\n\ndata: ${JSON.stringify({choices:[],usage:{prompt_tokens:13,completion_tokens:7,total_tokens:20}})}\n\ndata: [DONE]\n\n`);
    }catch(error){peerError=error;response.writeHead(500);response.end('synthetic fixture failed');}
  });
});
async function api(path,body){const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:`Bearer ${token}`,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(5000)});assert.ok(response.ok,await response.clone().text());return response.json();}
function cli(...args){const result=spawnSync(cliExe,[...(unified?['--port']:[]),String(port),...args],{env,encoding:'utf8',windowsHide:true,timeout:5000});assert.equal(result.status,0,result.stderr);assert.equal(result.stdout.includes(secret),false);return JSON.parse(result.stdout);}
function admin(args,privateEnv=env){const result=spawnSync(adminExe,[...(unified?['admin']:[]),'--db',db,'--modules',modules,'--stdlib',stdlib,...args],{env:privateEnv,encoding:'utf8',windowsHide:true,timeout:10000});assert.equal(result.status,0,result.stderr);assert.equal((result.stdout+result.stderr).includes(secret),false);return JSON.parse(result.stdout);}
async function until(read,predicate){const deadline=Date.now()+10000;while(Date.now()<deadline){const value=await read();if(predicate(value))return value;if(peerError)throw peerError;await delay(10);}throw new Error(`State deadline: ${errors}`);}
async function start(model=true){
  const args=['--db',db,'--modules',modules,'--stdlib',stdlib,'--port','0'];
  if(model)args.push('--model','synthetic-mcp-protocol-model','--model-stream-usage','supported','--model-endpoint',`http://127.0.0.1:${peer.address().port}/chat`,'--model-tools','supported','--workspace',workspace);
  child=spawn(serverExe,unified?['serve',...args]:args,{env,windowsHide:true});child.stderr.on('data',data=>{errors+=data;});
  port=await new Promise((resolve,reject)=>{let output='';const timer=setTimeout(()=>reject(new Error(`Readiness deadline: ${errors}`)),10000);child.on('error',error=>{clearTimeout(timer);reject(error);});child.once('exit',code=>{clearTimeout(timer);reject(new Error(`Server exited ${code}: ${errors}`));});child.stdout.on('data',data=>{output+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timer);resolve(Number(match[1]));}});});
}
async function stop(){if(child&&child.exitCode===null){const exited=new Promise(resolve=>child.once('exit',resolve));child.kill();await exited;}}
async function pendingOperations(runId){const items=await api(`/v1/runs/${runId}/operations`);const run=await api(`/v1/runs/${runId}`);if(run.state==='failed')throw new Error(`Native run failed before approval: ${JSON.stringify(run)}; events: ${JSON.stringify(await api(`/v1/runs/${runId}/events?after=0`))}`);return items;}
async function configure(mode){const path=join(folder,'config.json');await writeFile(path,JSON.stringify({servers:[{id:'fixture',transport:'stdio',executable:process.execPath,working_directory:folder,arguments:[fixture,mode,effect,marker,'credential-fixture',...(sdkEra?[sdkEra]:[])],credentials:[{name:'MCP_TEST_KEY',scope:'server',id:`credential-${mode}`}]}]}));return admin(['import-mcp',path]);}
try{
  await mkdir(workspace);await writeFile(effect,'');await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
  assert.equal((await configure(sdkEra?'normal':'subscription-no-change')).servers[0].revision,1);
  admin(['put-mcp-credential','fixture','MCP_TEST_KEY','XMIND_SETUP_SECRET'],{...env,XMIND_SETUP_SECRET:secret});
  await start();const metadata={servers:[{id:'fixture',revision:1,enabled:true,transport:'stdio'}],runtime_state:'per_run'};
  assert.deepEqual(cli('mcp-servers'),metadata);
  for(const name of ['allowed','denied','cancelled']){
    await api('/v1/sessions',{id:name,title:'Synthetic MCP protocol fixture'});const run=cli('run',name,name);
    const [operation]=await until(()=>pendingOperations(run.id),items=>items.some(item=>item.state==='awaiting_approval'));
    const events=await api(`/v1/runs/${run.id}/events?after=0`);
    const connected=events.filter(event=>event.kind==='mcp.connected');assert.equal(connected.length,1);
    assert.equal(connected[0].data.protocol_version,sdkEra==='legacy'?'2025-11-25':'2026-07-28');
    assert.equal(events.find(event=>event.kind==='mcp.discovered').data.tool_count,1);
    if(name==='allowed'&&!sdkEra){const observed=await api('/v1/runs/'+run.id+'/events?after=0');assert.ok(observed.some(event=>event.kind==='mcp.subscription.acknowledged'));assert.equal(observed.filter(event=>event.kind==='mcp.subscription.notification').length,0,'A quiet subscription must not invent catalogue changes');}
    assert.equal(await readFile(effect,'utf8'),name==='allowed'?'':'allowed');
    assert.ok(operation.arguments_json.includes('1.00000000000000000001'));
    if(name==='cancelled')cli('cancel',run.id);else cli('decide',operation.id,name==='denied'?'deny':'allow');
    await until(()=>api(`/v1/runs/${run.id}`),value=>value.state===(name==='cancelled'?'cancelled':'completed'));
    const terminalEvents=await api(`/v1/runs/${run.id}/events?after=0`);
    if(!sdkEra){
      const closed=terminalEvents.find(event=>event.kind==='mcp.subscription.closed');
      assert.ok(closed,'The MCP observer must persist its shutdown before the run becomes terminal');
      assert.equal(closed.data.outcome,'stopped');
      if(name==='allowed')assert.equal(await readFile(marker+'.subscription-closed','utf8'),'peer observed listener EOF');
    }
    assert.equal((await api(`/v1/operations/${operation.id}`)).state,{allowed:'succeeded',denied:'denied',cancelled:'cancelled'}[name]);
    assert.equal(await readFile(effect,'utf8'),'allowed');
    const history=cli('history',name);assert.equal(JSON.stringify(history).includes(secret),false);
    if(name!=='cancelled'){assert.equal(history.length,4);assert.deepEqual(history[3].data.usage,{prompt_tokens:13,completion_tokens:7,total_tokens:20});}
  }
  const history=cli('history','allowed');await stop();await start();assert.deepEqual(cli('history','allowed'),history);assert.deepEqual(cli('mcp-servers'),metadata);assert.equal(await readFile(effect,'utf8'),'allowed');
  await stop();assert.equal((await configure('disconnect')).servers[0].revision,2);
  admin(['put-mcp-credential','fixture','MCP_TEST_KEY','XMIND_SETUP_SECRET'],{...env,XMIND_SETUP_SECRET:secret});await start();
  await api('/v1/sessions',{id:'uncertain',title:'Actual external effect followed by lost reply'});const run=cli('run','uncertain','uncertain');
  const [operation]=await until(()=>api(`/v1/runs/${run.id}/operations`),items=>items.some(item=>item.state==='awaiting_approval'));cli('decide',operation.id,'allow');
  await until(()=>api(`/v1/runs/${run.id}`),value=>value.state==='failed');assert.equal((await api(`/v1/operations/${operation.id}`)).state,'uncertain');assert.equal(await readFile(effect,'utf8'),'alloweduncertain');assert.equal(continuations.has('uncertain'),false);
  await stop();await start(false);assert.deepEqual(cli('mcp-servers'),{servers:[{id:'fixture',revision:2,enabled:true,transport:'stdio'}],runtime_state:'per_run'});assert.equal((await api(`/v1/operations/${operation.id}`)).state,'uncertain');assert.equal(await readFile(effect,'utf8'),'alloweduncertain');
  await stop();assert.equal((await configure('resource-catalog')).servers[0].revision,3);admin(['put-mcp-credential','fixture','MCP_TEST_KEY','XMIND_SETUP_SECRET'],{...env,XMIND_SETUP_SECRET:secret});await start(false);
  const resourceResponse=cli('mcp-resources');assert.deepEqual(resourceResponse.servers,[{id:'fixture',revision:3,enabled:true,state:'connected',resource_count:2,template_count:1}]);assert.deepEqual(resourceResponse.resources.map(item=>item.resource.uri),['fixture://readme','fixture://second']);assert.deepEqual(resourceResponse.templates.map(item=>item.template.uriTemplate),['fixture://{name}']);assert.equal(JSON.stringify(resourceResponse).includes(secret),false);
  const unauthorized=await fetch(`http://127.0.0.1:${port}/v1/mcp/resources`);assert.equal(unauthorized.status,401,'Resource catalogs require local backend authentication');
  const invalidQuery=await fetch(`http://127.0.0.1:${port}/v1/mcp/resources?cursor=anything`,{headers:{Authorization:`Bearer ${token}`}});assert.equal(invalidQuery.status,400,'Resource pagination is performed natively, not selected by clients');
  await stop();assert.equal((await configure('resource-secret')).servers[0].revision,4);admin(['put-mcp-credential','fixture','MCP_TEST_KEY','XMIND_SETUP_SECRET'],{...env,XMIND_SETUP_SECRET:secret});await start(false);
  const reflected=cli('mcp-resources');assert.deepEqual(reflected.servers,[{id:'fixture',revision:4,enabled:true,state:'unavailable'}]);assert.deepEqual(reflected.resources,[]);assert.equal(JSON.stringify(reflected).includes(secret),false,'A peer that reflects a configured secret must not publish its catalog');
  assert.equal(requests,6);if(peerError)throw peerError;
  console.log('Native MCP agent/server/admin/CLI contract passed: encrypted configured environment, exact arguments, actual approved file effect, denial/cancellation, result continuation, persisted uncertainty without replay, and authenticated paginated resource discovery with credential-reflection suppression. Inference is synthetic.');
}finally{await stop();peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));assert.equal(dirname(resolve(folder)),resolve(tmpdir()));assert.ok(folder.includes('xmind-agent-mcp-'));await rm(folder,{recursive:true,force:true});}
