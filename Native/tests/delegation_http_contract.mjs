// Actual native parent/leaf agents, xlang3 SQLite, CLI approval and file effects.
// The local provider protocol replies and objectives are synthetic fixtures;
// no live provider, rendered browser or IDE acceptance is claimed here.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
import {randomBytes} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
const {BackendClient}=createRequire(import.meta.url)('../../extensions/vscode/client.js');
const {BrowserController}=createRequire(import.meta.url)('../../views/browser/browser.js');
const [serverExe,cliExe,modules,stdlib]=process.argv.slice(2);
const root=await mkdtemp(join(tmpdir(),'xmind-delegation-http-')),workspace=join(root,'workspace'),database=join(root,'state.sqlite');
const token=randomBytes(32).toString('hex'),key='synthetic-delegation-provider-key',created='Actual native create after delegated file reads\n';
const env={...process.env,XMIND_AUTH_TOKEN:token,XMIND_API_KEY:key};let child,port,peerError,requests=0;const calls=new Map(),joined=[];
const reads=['read_repository_instructions','read_file','list_files','search_files','glob_files','list_skills','load_skill'];
const call=(id,name,args)=>({index:0,id,type:'function',function:{name,arguments:JSON.stringify(args)}});
const peer=createServer((request,response)=>{
 let source='';request.on('data',bytes=>source+=bytes);request.on('end',()=>{
  try{
   ++requests;assert.equal(request.method,'POST');assert.equal(request.url,'/chat');assert.equal(request.headers.authorization,'Bearer '+key);
   const body=JSON.parse(source);assert.equal(body.model,'synthetic-delegation-model');assert.equal(body.stream_options.include_usage,true);
   const objective=body.messages.findLast(value=>value.role==='user').content,leaf=objective.includes('[leaf-left]')?'left':objective.includes('[leaf-right]')?'right':undefined;
   const names=body.tools.map(value=>value.function.name);assert.deepEqual(names,leaf?reads:[...reads,'edit_file','create_file','delegate_tasks','plan_tasks','revise_plan','inspect_plan']);
   const foreign=objective==='Scope-only foreign-root response';calls.set(leaf||(foreign?'foreign':'parent'),(calls.get(leaf||(foreign?'foreign':'parent'))||0)+1);const tool=body.messages.findLast(value=>value.role==='tool');let delta,finish;
   if(foreign){assert.equal(tool,undefined);delta={content:'Synthetic scope-only foreign-root response'};finish='stop';}
   else if(leaf){
    assert.ok(!names.includes('delegate_tasks')&&!names.includes('create_file'),'Leaf preset must not widen parent coding authority');
    if(!tool){delta={tool_calls:[leaf==='left'?call(leaf+'-read','glob_files',{pattern:'left.txt'}):call(leaf+'-read','read_file',{path:leaf+'.txt'})]};finish='tool_calls';}
    else{assert.equal(tool.tool_call_id,leaf+'-read');const result=JSON.parse(tool.content);if(leaf==='left'){assert.deepEqual(result.paths,['left.txt']);assert.equal(result.truncated,false);}else assert.equal(result.content,'Actual '+leaf+' fixture bytes\n');delta={content:'Observed actual '+leaf+' fixture file'};finish='stop';}
   }else if(!tool){
    delta={tool_calls:[call('parent-delegate','delegate_tasks',{tasks:[{id:'inspect-left',objective:'[leaf-left] Discover left.txt and report its path.',preset:'workspace.inspect'},{id:'inspect-right',objective:'[leaf-right] Read right.txt and report its contents.',preset:'workspace.inspect'}]})]};finish='tool_calls';
   }else if(tool.tool_call_id==='parent-delegate'){
    const value=JSON.parse(tool.content);assert.equal(value.source,'native_delegation');assert.equal(value.children.length,2);
    for(const result of value.children){assert.equal(result.child_state,'completed');assert.equal(result.preset_id,'workspace.inspect');assert.equal(result.preset_revision,1);assert.ok(['Observed actual left fixture file','Observed actual right fixture file'].includes(result.content));joined.push(result.child_run_id);}
    delta={tool_calls:[call('parent-create','create_file',{path:'created.txt',content:created})]};finish='tool_calls';
   }else{assert.equal(tool.tool_call_id,'parent-create');const value=JSON.parse(tool.content);assert.ok(value.operation_id&&value.content_sha256);delta={content:'Observed both delegated reads and the approved native file creation'};finish='stop';}
   response.writeHead(200,{'Content-Type':'text/event-stream'});response.end('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:null}]})+'\n\ndata: '+JSON.stringify({choices:[{index:0,delta:{},finish_reason:finish}]})+'\n\ndata: '+JSON.stringify({choices:[],usage:{prompt_tokens:leaf?11:19,completion_tokens:leaf?5:7,total_tokens:leaf?16:26}})+'\n\ndata: [DONE]\n\n');
  }catch(error){peerError=error;response.writeHead(500);response.end('Synthetic delegation provider protocol fixture failed');}
 });
});
async function start(withModel=true){
 const args=['--db',database,'--modules',modules,'--stdlib',stdlib,'--port','0','--workspace',workspace,'--workspace-edits','approved','--workers','2'];if(withModel)args.push('--model','synthetic-delegation-model','--model-endpoint','http://127.0.0.1:'+peer.address().port+'/chat','--model-tools','supported','--model-stream-usage','supported');
 child=spawn(serverExe,args,{env:withModel?env:{...env,XMIND_API_KEY:''},windowsHide:true});let output='',errors='';child.stderr.on('data',bytes=>errors+=bytes);
 port=await new Promise((yes,no)=>{const timer=setTimeout(()=>no(new Error('Native delegation readiness timed out')),10000);child.once('error',error=>{clearTimeout(timer);no(error);});child.once('exit',code=>{clearTimeout(timer);no(new Error('Native delegation server exited '+code+': '+errors));});child.stdout.on('data',bytes=>{output+=bytes;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timer);yes(Number(match[1]));}});});
}
async function stop(){if(!child||child.exitCode!==null)return;const ended=new Promise(yes=>child.once('exit',yes));child.kill();await ended;child=undefined;}
async function api(path,body,headers={}){const response=await fetch('http://127.0.0.1:'+port+path,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+token,...(body===undefined?{}:{'Content-Type':'application/json'}),...headers},body:body===undefined?undefined:JSON.stringify(body),redirect:'error',signal:AbortSignal.timeout(5000)});return {status:response.status,data:await response.json()};}
async function wait(action,predicate){const end=Date.now()+15000;for(;;){if(peerError)throw peerError;const value=await action();if(predicate(value))return value;if(Date.now()>=end)throw new Error('Actual native delegation observation timed out');await delay(10);}}
function cli(command,...args){const result=spawnSync(cliExe,[String(port),command,...args],{env,encoding:'utf8',windowsHide:true,timeout:12000});assert.ifError(result.error);assert.equal(result.status,0,result.stderr);assert.ok(!result.stdout.includes(key));return command==='watch'?result.stdout.trim().split('\n').filter(Boolean).map(line=>JSON.parse(line)):JSON.parse(result.stdout);}
try{
 await mkdir(workspace);for(const side of ['left','right'])await writeFile(join(workspace,side+'.txt'),'Actual '+side+' fixture bytes\n');await new Promise(yes=>peer.listen(0,'127.0.0.1',yes));await start();
 const client=new BackendClient('http://127.0.0.1:'+port,()=>token),health=await client.health();assert.equal(health.agent_delegation,true);assert.equal(health.owned_child_observation,true);
 const metadata=await client.delegation();assert.equal(metadata.enabled,true);assert.deepEqual(metadata.presets,[{id:'workspace.inspect',revision:1,readonly:true,tools:['read_file','list_files','search_files','glob_files','read_repository_instructions','list_skills','load_skill']}]);assert.equal(metadata.limits.total_children,8);assert.equal(metadata.limits.parallel_children,2);assert.equal(metadata.limits.depth,1);assert.ok(!JSON.stringify(metadata).includes(workspace));
 const session=await client.createSession('Actual Agent delegation contract'),run=await client.run(session.id,'Delegate two fixture investigations, observe both and create the reviewed file');assert.equal(run.graph_root,false);
 const pending=await wait(()=>client.operations(run.id),value=>value.some(item=>item.state==='awaiting_approval')),operation=pending.find(value=>value.state==='awaiting_approval');assert.equal(operation.tool,'create_file');await assert.rejects(readFile(join(workspace,'created.txt')),{code:'ENOENT'});
 const before=await client.ownedChildren(run.id);assert.equal(before.length,2);assert.ok(before.every(value=>value.kind==='delegated_leaf'&&value.run.state==='completed'&&value.run.parent_id===run.id&&value.run.session_id===session.id));assert.deepEqual(new Set(before.map(value=>value.run.id)),new Set(joined));
 cli('decide',operation.id,'allow');await wait(()=>client.status(run.id),value=>value.state==='completed');assert.equal(await readFile(join(workspace,'created.txt'),'utf8'),created);assert.equal((await client.operation(operation.id)).state,'succeeded');assert.equal(requests,7);assert.deepEqual(Object.fromEntries(calls),{parent:3,left:2,right:2});
 assert.deepEqual(cli('children',run.id),before);for(const record of before){const history=await client.ownedChildHistory(run.id,record.run.id);assert.ok(history.some(value=>value.role==='tool'));assert.equal(history.at(-1).data.usage.prompt_tokens,11);assert.equal(history.at(-1).data.usage.completion_tokens,5);assert.deepEqual(cli('child-history',run.id,record.run.id),history);assert.deepEqual(await client.operations(record.run.id),[]);}
 const all=[];let cursor=0;for(;;){const page=await client.treeEvents(run.id,cursor);assert.ok(page.length<=256);if(!page.length)break;for(const event of page){assert.ok(event.seq>cursor);cursor=event.seq;all.push(event);}if(page.length<256)break;}
 const owned=new Set([run.id,...before.map(value=>value.run.id)]);assert.ok(all.every(value=>owned.has(value.run_id)));assert.ok(all.some(value=>value.kind==='delegation.child.settled'));assert.ok(all.some(value=>value.kind==='operation.succeeded'));assert.deepEqual(cli('watch',run.id),all);assert.deepEqual(cli('tree-events',run.id,String(all[2].seq)),all.filter(value=>value.seq>all[2].seq).slice(0,256));
 const foreign=await client.createSession('Foreign parent boundary');const foreignRun=await client.run(foreign.id,'Scope-only foreign-root response');await wait(()=>client.status(foreignRun.id),value=>value.state==='completed');assert.equal(calls.get('foreign'),1);assert.equal(requests,8);assert.deepEqual(await client.operations(foreignRun.id),[]);assert.deepEqual(await client.ownedChildren(foreignRun.id),[]);
 assert.equal((await api('/v1/runs/'+foreignRun.id+'/children/'+before[0].run.id+'/history')).status,404);
 const count=(await client.ownedChildren(run.id)).length;assert.equal((await api('/v1/runs/'+run.id+'/children',{})).status,404);assert.equal((await client.ownedChildren(run.id)).length,count);
 for(const query of ['?after=-1','?after=1&after=2','?count=1'])assert.equal((await api('/v1/runs/'+run.id+'/tree-events'+query)).status,400);
 const issued=await api('/v1/view-sessions',{origin:'http://127.0.0.1:65321'});assert.equal(issued.status,200);const viewHeaders={Authorization:'View '+issued.data.credential,'X-XMind-View-Origin':'http://127.0.0.1:65321'};assert.deepEqual((await api('/v1/runs/'+run.id+'/children',undefined,viewHeaders)).data,before);assert.equal((await api('/v1/runs/'+run.id+'/children',{},viewHeaders)).status,401);
 const posted=[],browser=new BrowserController(client,value=>posted.push(value));browser.health=health;browser.session=session.id;browser.runId=run.id;try{await browser.poll();assert.equal(browser.graphId,undefined);assert.equal(browser.graph,undefined);assert.equal(posted.findLast(value=>value.type==='owned-children').children.length,2);assert.ok(posted.some(value=>value.type==='owned-event'));assert.ok(!posted.some(value=>value.type==='graph'));assert.ok(!JSON.stringify(posted).includes(key));}finally{browser.dispose();}
 const histories=await Promise.all(before.map(value=>client.ownedChildHistory(run.id,value.run.id))),parentHistory=await client.history(session.id),requestCount=requests;await stop();await start(false);
 const reopened=new BackendClient('http://127.0.0.1:'+port,()=>token);assert.equal((await reopened.delegation()).enabled,false);assert.deepEqual((await reopened.delegation()).presets,[]);assert.deepEqual(await reopened.ownedChildren(run.id),before);for(let index=0;index<before.length;++index)assert.deepEqual(await reopened.ownedChildHistory(run.id,before[index].run.id),histories[index]);assert.deepEqual(await reopened.history(session.id),parentHistory);assert.deepEqual(cli('watch',run.id),all);assert.equal(requests,requestCount,'Read/reopen/cursor reconnection cannot relaunch native children');
 if(peerError)throw peerError;console.log('Native delegation HTTP/CLI/shared-browser contract passed actual parent/leaf tool loop, independent real file reads, observed join, controller-approved native create, read-only child authority, private metrics/history, scoped native/view child reads, tree cursor replay and model-free reopen without child replay. Provider replies are synthetic; no live provider or rendered IDE claim.');
}finally{await stop();peer.closeAllConnections();await new Promise(yes=>peer.close(yes));assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-delegation-http-'));await rm(root,{recursive:true,force:true});}
