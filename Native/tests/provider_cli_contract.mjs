// Actual compiled server/CLI and encrypted xlang3 storage. The key/model are
// synthetic fixture data; this contract never requests external inference.
import assert from 'node:assert/strict';import {spawn,spawnSync} from 'node:child_process';import {mkdtemp,rm,readFile,writeFile} from 'node:fs/promises';import {tmpdir} from 'node:os';import {join,resolve,dirname,basename} from 'node:path';import {randomBytes} from 'node:crypto';
const [server,cli,modules,stdlib]=process.argv.slice(2),root=await mkdtemp(join(tmpdir(),'xmind-provider-cli-')),token=randomBytes(32).toString('hex'),secret='synthetic-cli-provider-key',database=join(root,'state.sqlite'),env={...process.env,XMIND_AUTH_TOKEN:token};let backend,port;
async function start(){backend=spawn(server,['--db',database,'--graphs-config',join(root,'graphs.json'),'--modules',modules,'--stdlib',stdlib,'--port','0'],{env,windowsHide:true});let out='',err='';backend.stderr.on('data',data=>err+=data);port=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('Provider CLI server readiness timed out')),10000);backend.once('error',error=>{clearTimeout(timer);reject(error);});backend.once('exit',code=>{clearTimeout(timer);reject(new Error(`Provider CLI server exited ${code}: ${err}`));});backend.stdout.on('data',data=>{out+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(out);if(match){clearTimeout(timer);resolve(Number(match[1]));}});});}
async function stop(){if(!backend)return;if(backend.exitCode===null){const exited=new Promise(resolve=>backend.once('exit',resolve));backend.kill();await exited;}backend=undefined;}
function invoke(args,extra={},status=0){const result=spawnSync(cli,[String(port),...args],{env:{...env,...extra},encoding:'utf8',windowsHide:true,timeout:15000});if(result.error)throw result.error;assert.equal(result.status,status,result.stderr);assert.ok(!result.stdout.includes(secret)&&!result.stderr.includes(secret),'CLI output must not disclose fixture key');return status===0?JSON.parse(result.stdout):result;}
function interactive(input){const result=spawnSync(cli,[String(port),'chat'],{input,env,encoding:'utf8',windowsHide:true,timeout:15000});if(result.error)throw result.error;assert.equal(result.status,0,result.stderr);assert.ok(!result.stdout.includes(secret)&&!result.stderr.includes(secret));return {records:result.stdout.split(/\r?\n/).filter(Boolean).map(line=>JSON.parse(line)),stderr:result.stderr};}
import {createServer} from 'node:http';
import {setTimeout as delay} from 'node:timers/promises';
async function admissionRace(profiles){
 const session=invoke(['create-session','Synthetic provider admission race']);
 const catalogue=invoke(['graphs']);const graph=catalogue.graphs.find(item=>item.id==='review');assert.ok(graph);
 let changed=false;const bodies=[];let proxyError;
 const proxy=createServer(async(request,response)=>{
  try{
   assert.equal(request.headers.authorization,`Bearer ${token}`);
   let body='';for await(const chunk of request)body+=chunk;
   if(request.method==='POST'&&request.url==='/v1/graph-runs'){
    bodies.push(JSON.parse(body));
    if(!changed){changed=true;await profiles('/select',{id:'claude',expected_revision:4});}
   }
   const upstream=await fetch(`http://127.0.0.1:${port}${request.url}`,{method:request.method,headers:{Authorization:`Bearer ${token}`,...(body?{'Content-Type':'application/json'}:{})},...(body?{body}:{})});
   response.writeHead(upstream.status,{'Content-Type':'application/json'});response.end(await upstream.text());
  }catch(error){proxyError=error;response.writeHead(500);response.end('{}');}
 });
 await new Promise(resolve=>proxy.listen(0,'127.0.0.1',resolve));
 async function submit(status){
  const child=spawn(cli,[String(proxy.address().port),'graph-run',session.id,'review',String(graph.revision),'Synthetic human review'],{env,windowsHide:true});let out='',err='';
  child.stdout.on('data',data=>out+=data);child.stderr.on('data',data=>err+=data);
  const code=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{child.kill();reject(new Error('Provider admission CLI timed out'));},15000);child.once('error',error=>{clearTimeout(timer);reject(error);});child.once('exit',code=>{clearTimeout(timer);resolve(code);});});
  if(proxyError)throw proxyError;assert.equal(code,status,err);assert.ok(!out.includes(secret)&&!err.includes(secret));return status===0?JSON.parse(out):undefined;
 }
 try{
  await submit(1);assert.equal(bodies[0].provider_profile_id,'openai');assert.equal(bodies[0].expected_provider_revision,4);
  assert.deepEqual(invoke(['runs',session.id]),[]);assert.deepEqual(invoke(['history',session.id]),[],'Stale profile rejection cannot append the task');
  const accepted=await submit(0);assert.equal(bodies[1].provider_profile_id,'claude');assert.equal(bodies[1].expected_provider_revision,5);
  let paused=false;for(let attempt=0;attempt<100;attempt++){if(invoke(['status',accepted.id]).state==='paused'){paused=true;break;}await delay(20);}assert.ok(paused,'Current profile must admit the actual native human graph');
  invoke(['cancel',accepted.id]);let cancelled=false;for(let attempt=0;attempt<100;attempt++){if(invoke(['status',accepted.id]).state==='cancelled'){cancelled=true;break;}await delay(20);}assert.ok(cancelled);
 }finally{await new Promise(resolve=>proxy.close(resolve));}
}
try{
 await writeFile(join(root,'graphs.json'),JSON.stringify({graphs:[{id:'review',spec:{nodes:[{id:'review',type:'human',prompt:'Synthetic review'}]}}]}));
 async function profiles(path='',body){const response=await fetch(`http://127.0.0.1:${port}/v1/provider/profiles${path}`,{method:body?'POST':'GET',headers:{Authorization:`Bearer ${token}`,...(body?{'Content-Type':'application/json'}:{})},...(body?{body:JSON.stringify(body)}:{})});assert.equal(response.status,200);const value=await response.json();assert.ok(!JSON.stringify(value).includes(secret));assert.ok(!JSON.stringify(value).includes('synthetic-cli-claude-key'));return value;}
 await start();const initial=invoke(['provider']);assert.equal(initial.configured,false);assert.equal(initial.revision,0);invoke(['configure-provider','fixture-model','FIXTURE_PROVIDER_KEY','0'],{},1);assert.equal(invoke(['provider']).revision,0);for(const variable of ['XMIND_AUTH_TOKEN','xmind_auth_token','XMIND_UI_BOOTSTRAP_TOKEN'])invoke(['configure-provider','fixture-model',variable,'0'],{},1);
 const configured=invoke(['configure-provider','fixture-model','FIXTURE_PROVIDER_KEY','0'],{FIXTURE_PROVIDER_KEY:secret});assert.equal(configured.configured,true);assert.equal(configured.revision,1);assert.equal(configured.model,'fixture-model');assert.equal(invoke(['health']).agent_execution,true);assert.deepEqual(invoke(['models']).models,[{id:'fixture-model'}]);invoke(['configure-provider','fixture-alternate','FIXTURE_PROVIDER_KEY','0'],{FIXTURE_PROVIDER_KEY:secret},1);assert.equal(invoke(['provider']).model,'fixture-model');assert.deepEqual(invoke(['sessions']),[],'Enrollment must not fabricate a conversation or response');
 const emptyNavigation=interactive('/sessions\n/new\n/history\n/exit\n');assert.deepEqual(emptyNavigation.records[0].sessions,[]);assert.deepEqual(invoke(['sessions']),[],'Navigating a new draft cannot create an empty conversation');
 const selected=invoke(['create-session','Synthetic CLI navigation fixture']);invoke(['append-message',selected.id,'Synthetic persisted navigation message']);const savedHistory=invoke(['history',selected.id]);
 const navigation=interactive('/sessions\n/session missing\n/session ../invalid\n/session '+selected.id+'\n/title Synthetic renamed navigation\n/history\n/new\n/history\n/sessions\n/exit\n');
 assert.match(navigation.stderr,/Session was not found/);assert.match(navigation.stderr,/Invalid session ID/);assert.deepEqual(navigation.records.filter(record=>record.type==='session').map(record=>record.session_id),[selected.id,'']);
 assert.deepEqual(navigation.records.filter(record=>record.type==='history').map(record=>record.history),[savedHistory,savedHistory,[],[]]);assert.equal(navigation.records.at(-1).selected_session,'');
 assert.deepEqual(navigation.records.find(record=>record.type==='session_renamed').session,{id:selected.id,title:'Synthetic renamed navigation'});
 invoke(['rename-session',selected.id,'Stale title attempt',selected.title],{},1);assert.equal(invoke(['sessions'])[0].title,'Synthetic renamed navigation');
 invoke(['rename-session',selected.id,'','Synthetic renamed navigation'],{},1);const updated=invoke(['rename-session',selected.id,'Synthetic persisted rename','Synthetic renamed navigation']);assert.equal(updated.id,selected.id);assert.equal(updated.title,'Synthetic persisted rename');
 assert.equal(invoke(['sessions']).length,1);assert.deepEqual(invoke(['history',selected.id]),savedHistory);assert.deepEqual(invoke(['runs',selected.id]),[],'Session navigation must not admit a run or request inference');assert.equal(invoke(['provider']).revision,1);
 await stop();assert.ok(!(await readFile(database)).includes(Buffer.from(secret)),'Actual native credential DB must not contain plaintext fixture key');await start();assert.equal(invoke(['provider']).revision,1);assert.equal(invoke(['health']).agent_execution,true);assert.equal(invoke(['sessions'])[0].title,'Synthetic persisted rename');assert.deepEqual(invoke(['history',selected.id]),savedHistory);
 const restored=await profiles();assert.equal(restored.active,'openai');assert.equal(restored.profiles.length,1);assert.deepEqual(restored.routes.map(route=>route.id),['openai.chat','openai.responses','anthropic.messages']);
 const added=await profiles('',{id:'claude',route_id:'anthropic.messages',model:'fixture-claude',api_key:'synthetic-cli-claude-key',expected_revision:1});assert.equal(added.revision,2);assert.equal(invoke(['provider']).model,'fixture-model');
 await profiles('/select',{id:'claude',expected_revision:2});assert.equal(invoke(['provider']).provider,'anthropic');assert.equal(invoke(['provider']).wire,'anthropic-messages');assert.deepEqual(invoke(['models']).models,[{id:'fixture-claude'}]);
 await profiles('/select',{id:'openai',expected_revision:3});assert.equal(invoke(['provider']).model,'fixture-model');await stop();await start();assert.equal((await profiles()).profiles.length,2);assert.equal(invoke(['provider']).revision,4);assert.deepEqual(invoke(['history',selected.id]),savedHistory);await admissionRace(profiles);await stop();
 process.stdout.write('Native provider CLI passed authenticated compatibility/profile enrollment and selection, private environment input, actual encrypted DB storage, model restore and conversation navigation with preserved history. Key/model/history are labelled fixtures; no inference or live provider acceptance claimed.\n');
}finally{await stop();assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-provider-cli-'));await rm(root,{recursive:true,force:true});}
