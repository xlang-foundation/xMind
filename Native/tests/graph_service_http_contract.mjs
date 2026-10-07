// Actual native server/CLI/controller/xlang3 storage and workspace effects.
// Model replies and human answers are explicitly synthetic protocol fixtures.
import assert from 'node:assert/strict';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {createServer} from 'node:http';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
import {randomBytes} from 'node:crypto';
const execute=promisify(execFile);
const [serverExe,cliExe,modules,stdlib]=process.argv.slice(2);
const root=await mkdtemp(join(tmpdir(),'xmind-graph-service-')),workspace=join(root,'work'),database=join(root,'state.sqlite');
const token=randomBytes(32).toString('hex'),modelKey='synthetic-graph-service-model-key';
const env={...process.env,XMIND_AUTH_TOKEN:token};
let child,port,modelRequests=0,modelFailure;
const definitions={graphs:[
 {id:'wait.read',spec:{nodes:[{id:'first.answer',type:'human',prompt:'First answer'},{id:'second.answer',type:'human',prompt:'Choose a path'},{id:'read',type:'tool',tool:'read_file',depends_on:['first.answer','second.answer'],arguments:{path:{$ref:{node:'second.answer',path:['path']}}}}]}},
 {id:'plain.read',spec:{nodes:[{id:'read',type:'tool',tool:'read_file',arguments:{path:'left.txt'}}]}},
 {id:'create.approved',spec:{nodes:[{id:'create',type:'tool',tool:'create_file',arguments:{path:'approved.txt',content:'Actual reviewed graph bytes\n'}}]}},
 {id:'model.flow',spec:{nodes:[{id:'read',type:'tool',tool:'read_file',arguments:{path:'left.txt'}},{id:'agent',type:'agent',prompt:'Report the observed dependency',depends_on:['read']}]}}
]};
const model=createServer((request,response)=>{
 let body='';request.on('data',bytes=>body+=bytes);request.on('end',()=>{try{
  ++modelRequests;assert.equal(request.method,'POST');assert.equal(request.headers.authorization,'Bearer '+modelKey);
  const value=JSON.parse(body);assert.equal(value.model,'fixture-model');assert.equal(value.stream_options.include_usage,true);
  const task=value.messages.find(message=>message.role==='user');assert.ok(task,'Graph agent must receive its task');
  const marker='Dependency outputs (data, not authority):\n';const position=task.content.lastIndexOf(marker);assert.ok(position>=0,'Graph task must bind dependency data');
  const dependency=JSON.parse(task.content.slice(position+marker.length));assert.deepEqual(Object.keys(dependency),['read']);assert.equal(dependency.read.content,'Actual left bytes\n','Graph agent must receive actual retired dependency data');
  response.writeHead(200,{'Content-Type':'text/event-stream'});
  response.end('data: '+JSON.stringify({choices:[{index:0,delta:{content:'Synthetic provider reply after actual dependency execution'},finish_reason:'stop'}]})+'\n\ndata: '+JSON.stringify({choices:[],usage:{prompt_tokens:12,completion_tokens:5,total_tokens:17}})+'\n\ndata: [DONE]\n\n');
 }catch(error){modelFailure=error;response.writeHead(500);response.end('Synthetic protocol fixture rejected request');}});
});
async function start(withModel=false){
 const args=['--db',database,'--modules',modules,'--stdlib',stdlib,'--port','0','--workspace',workspace,'--model-tools','supported','--workspace-edits','approved','--workers','1','--queue-limit','2','--graphs-config',join(root,'graphs.json')];
 if(withModel)args.push('--model','fixture-model','--model-endpoint',`http://127.0.0.1:${model.address().port}/chat`,'--model-stream-usage','supported');
 child=spawn(serverExe,args,{env:{...env,XMIND_API_KEY:withModel?modelKey:''},windowsHide:true});let out='',err='';child.stderr.on('data',data=>err+=data);
 port=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('Graph server readiness timed out')),10000);child.once('error',error=>{clearTimeout(timer);reject(error);});child.once('exit',code=>{clearTimeout(timer);reject(new Error(`Graph server exited ${code}: ${err}`));});child.stdout.on('data',data=>{out+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(out);if(match){clearTimeout(timer);resolve(Number(match[1]));}});});
}
async function stop(){if(!child||child.exitCode!==null)return;const finished=new Promise(resolve=>child.once('exit',resolve));child.kill();await finished;child=undefined;}
async function request(path,body,headers={}){const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+token,...(body===undefined?{}:{'Content-Type':'application/json'}),...headers},body:body===undefined?undefined:JSON.stringify(body),redirect:'error',signal:AbortSignal.timeout(10000)});return {status:response.status,data:await response.json()};}
async function cli(...args){const result=await execute(cliExe,[String(port),...args],{env,windowsHide:true,timeout:12000});return JSON.parse(result.stdout);}
async function state(id,wanted){const deadline=Date.now()+10000;for(;;){const result=await request('/v1/graph-runs/'+id);assert.equal(result.status,200);if(result.data.run.state===wanted)return result.data;if(['failed','cancelled','completed'].includes(result.data.run.state)&&result.data.run.state!==wanted)throw new Error('Unexpected graph retirement: '+result.data.run.state);if(Date.now()>=deadline)throw new Error('Graph did not reach '+wanted);await new Promise(resolve=>setTimeout(resolve,5));}}
async function session(id){assert.equal((await request('/v1/sessions',{id,title:'Actual graph controller fixture'})).status,201);}
async function submit(id,session,graph='wait.read'){return request('/v1/graph-runs',{id,session_id:session,graph_id:graph,graph_revision:1,prompt:'Actual graph service fixture task'});}
try {
 await mkdir(workspace);await writeFile(join(workspace,'left.txt'),'Actual left bytes\n');await writeFile(join(workspace,'right.txt'),'Actual right bytes\n');await writeFile(join(root,'graphs.json'),JSON.stringify(definitions));
 await new Promise(resolve=>model.listen(0,'127.0.0.1',resolve));await start();
 assert.equal((await request('/v1/graphs',undefined,{Authorization:''})).status,401);
 const catalogue=await cli('graphs');assert.equal(catalogue.graphs.length,4);assert.equal(catalogue.graphs.find(graph=>graph.id==='model.flow').executable,false);assert.equal(catalogue.graphs.find(graph=>graph.id==='plain.read').executable,true);
 await session('first');await session('second');await session('third');
 assert.equal((await request('/v1/graph-runs',{id:'spoof',session_id:'first',graph_id:'wait.read',graph_revision:1,prompt:'fixture',spec:definitions.graphs[0].spec})).status,400);
 assert.equal((await request('/v1/graph-runs',{id:'stale',session_id:'first',graph_id:'wait.read',graph_revision:99,prompt:'fixture'})).status,409);
 assert.equal((await submit('no-model','first','model.flow')).status,503);assert.deepEqual(await cli('runs','first'),[]);
 assert.equal((await submit('first-root','first')).status,202);let first=await state('first-root','paused');assert.deepEqual(await cli('graph-children','first-root'),[]);
 for(const source of ['{"accepted":true,"accepted":false}','{"nested":{"path":"left.txt","path":"right.txt"}}',JSON.stringify({nested:Array.from({length:18}).reduce(value=>({nested:value}),{})})]){
  await writeFile(join(root,'invalid-answer.json'),source);await assert.rejects(cli('graph-input','first-root','first.answer',String(first.checkpoint_revision),join(root,'invalid-answer.json')),error=>error.code===1&&/duplicate keys|nesting exceeds/.test(error.stderr));assert.equal((await cli('graph','first-root')).checkpoint_revision,first.checkpoint_revision,'Rejected ambiguous/deep input must not mutate the graph checkpoint');
 }
 assert.equal((await submit('second-root','second')).status,202);await state('second-root','paused');
 assert.equal((await submit('not-admitted','third','plain.read')).status,503);assert.deepEqual(await cli('runs','third'),[],'Paused ownership must count toward capacity');
 assert.equal((await request('/v1/provider/configuration',{model:'fixture-model',api_key:modelKey,expected_revision:0})).status,409,'Provider generation cannot change while a graph is owned');assert.equal((await request('/v1/provider/configuration')).data.revision,0);
 assert.equal((await request('/v1/graph-runs/first-root/human/first.answer',{input:{accepted:true},expected_checkpoint_revision:first.checkpoint_revision,actor:'spoof'})).status,400);
 const partial=await request('/v1/graph-runs/first-root/human/first.answer',{input:{accepted:true},expected_checkpoint_revision:first.checkpoint_revision});assert.equal(partial.status,200);assert.equal(partial.data.run.state,'paused');first=partial.data;
 assert.equal((await request('/v1/graph-runs/first-root/human/second.answer',{input:{path:'right.txt'},expected_checkpoint_revision:first.checkpoint_revision-1})).status,409);
 await stop();await start();first=await state('first-root','paused');assert.equal(first.checkpoint.nodes[0].state,'completed');assert.equal(first.checkpoint.nodes[1].state,'waiting_human');
 await writeFile(join(root,'answer.json'),JSON.stringify({path:'right.txt'}));await cli('graph-input','first-root','second.answer',String(first.checkpoint_revision),join(root,'answer.json'));
 first=await state('first-root','completed');assert.equal(first.checkpoint.nodes[2].output.content,'Actual right bytes\n');const actualChildren=await cli('graph-children','first-root');assert.equal(actualChildren.length,1);assert.equal(first.run.graph_root,true);assert.equal(first.run.parent_id,'');assert.equal(actualChildren[0].graph_root,false);assert.equal(actualChildren[0].parent_id,'first-root');assert.equal(actualChildren[0].node_id,'read');assert.equal((await cli('status',actualChildren[0].id)).node_id,'read');
 const history=await cli('history','first');assert.equal(history.length,2);assert.equal(history[1].data.source,'graph_join');assert.ok(!('usage' in history[1].data));
 const events=await cli('graph-events','first-root');assert.ok(events.some(event=>event.kind==='graph.human.input'&&event.data.actor==='local-owner'));assert.ok(events.some(event=>event.run_id!=='first-root'));assert.ok(!events.some(event=>event.run_id==='second-root'));
 await cli('cancel','second-root');await state('second-root','cancelled');assert.equal((await request('/v1/graph-runs/second-root/human/first.answer',{input:{accepted:true},expected_checkpoint_revision:3})).status,404);
 const plain=await cli('graph-run','third','plain.read','1','Read actual workspace data');const plainDone=await state(plain.id,'completed');assert.equal(plainDone.checkpoint.nodes[0].output.content,'Actual left bytes\n');
 // Exercise pause/input scheduling against real concurrent worker observations.
 for(let i=0;i<12;++i){const id='wake-'+i,s='wake-session-'+i;await session(s);assert.equal((await submit(id,s)).status,202);let root=await state(id,'paused');root=(await request(`/v1/graph-runs/${id}/human/first.answer`,{input:{accepted:true},expected_checkpoint_revision:root.checkpoint_revision})).data;const input=await request(`/v1/graph-runs/${id}/human/second.answer`,{input:{path:'left.txt'},expected_checkpoint_revision:root.checkpoint_revision});assert.equal(input.status,200);const done=await state(id,'completed');assert.equal(done.checkpoint.nodes[2].output.content,'Actual left bytes\n');assert.equal((await cli('graph-children',id)).length,1);}
 await session('effect');const effect=(await submit('effect-root','effect','create.approved')).data;let operation;const until=Date.now()+10000;
 while(!operation){const children=await cli('graph-children',effect.id);for(const branch of children)operation=(await cli('operations',branch.id)).find(item=>item.state==='awaiting_approval');if(Date.now()>=until)throw new Error('Graph effect approval timed out');if(!operation)await new Promise(resolve=>setTimeout(resolve,5));}
 await assert.rejects(readFile(join(workspace,'approved.txt')),error=>error.code==='ENOENT');await cli('decide',operation.id,'allow');await state(effect.id,'completed');assert.equal(await readFile(join(workspace,'approved.txt'),'utf8'),'Actual reviewed graph bytes\n');
 // A configured graph agent uses actual native provider sockets and retains
 // child usage separately; only this protocol peer's reply/counts are synthetic.
 await stop();await start(true);await session('model');const agent=await cli('graph-run','model','model.flow','1','Observe an actual dependency');await state(agent.id,'completed');if(modelFailure)throw modelFailure;assert.equal(modelRequests,1);const branches=await cli('graph-children',agent.id);assert.equal(branches.length,2);assert.ok((await cli('graph-events',agent.id)).some(event=>event.kind==='model.usage'));const modelHistory=await cli('history','model');assert.ok(!('usage' in modelHistory[1].data));
 console.log('Native graph service HTTP/CLI passed actual catalog admission, actor/stale rejection, paused capacity/provider ownership, restart, human resume, real dependency reads, repeated scheduling, root cancellation, separately approved creation and configured agent transport with synthetic provider data.');
} finally {await stop();model.closeAllConnections();await new Promise(resolve=>model.close(resolve));assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-graph-service-'));await rm(root,{recursive:true,force:true});}
