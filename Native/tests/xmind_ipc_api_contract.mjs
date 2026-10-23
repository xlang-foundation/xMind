import assert from 'node:assert/strict';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {randomBytes} from 'node:crypto';
import {createServer} from 'node:http';
const [binary,modules,stdlib]=process.argv.slice(2);assert(binary&&modules&&stdlib);
const root=await mkdtemp(join(tmpdir(),'xmind-root-api-')),token=randomBytes(32).toString('hex');
let modelRequests=0,peerFailure;
const peer=createServer((request,response)=>{let body='';request.on('data',b=>body+=b);request.on('end',()=>{try{++modelRequests;assert.equal(request.url,'/chat');assert.equal(request.headers.authorization,'Bearer controlled-ipc-fixture-key');const input=JSON.parse(body);assert.equal(input.model,'sdk-fixture');assert(input.stream);const prompt=input.messages.filter(m=>m.role==='user').at(-1)?.content;response.writeHead(200,{'Content-Type':'text/event-stream'});if(prompt==='Wait for scoped cancellation'){response.write(': waiting\n\n');return;}assert.equal(prompt,'Complete through the captured SDK binding');response.end('data: '+JSON.stringify({choices:[{index:0,delta:{content:'Captured SDK run completed.'},finish_reason:'stop'}]})+'\n\ndata: [DONE]\n\n');}catch(error){peerFailure=error;response.writeHead(500);response.end();}});});
await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
const env={...process.env,XMIND_AUTH_TOKEN:token,XMIND_API_KEY:'controlled-ipc-fixture-key'};let stdout='',stderr='',endpoint,passed=false;
const backend=spawn(binary,['serve','--db',join(root,'state.sqlite'),'--modules',modules,'--stdlib',stdlib,'--port','0','--model','sdk-fixture','--model-endpoint',`http://127.0.0.1:${peer.address().port}/chat`],{env,windowsHide:true,stdio:['ignore','pipe','pipe']});
const exited=new Promise(resolve=>backend.once('exit',(code,signal)=>resolve({code,signal})));
backend.stdout.on('data',b=>{stdout+=b;endpoint=stdout.match(/xMind API listening on (lrpc:\d+)/)?.[1];});backend.stderr.on('data',b=>stderr+=b);
const run=promisify(execFile);const clients=[];
async function client(args,auth=token){const child=run(binary,['ipc','--endpoint',endpoint,...args],{env:{...env,XMIND_AUTH_TOKEN:auth},windowsHide:true,timeout:10000,maxBuffer:1024*1024});clients.push(child.child.pid);const result=await child;return JSON.parse(result.stdout);}
try{
    const deadline=Date.now()+15000;while(!endpoint||!stdout.includes('xMind Server listening on')){assert(Date.now()<deadline,'Root API startup timed out');assert.equal(backend.exitCode,null,'Backend exited during startup');await new Promise(resolve=>setTimeout(resolve,25));}
    assert.deepEqual(await client(['host']),{api_version:1,transport:'xlang3-lrpc'});
    await assert.rejects(client(['connections','list'],'0'.repeat(64)));
    const local=await client(['connections','list']);assert.equal(local.revision,1);assert.equal(local.active_id,'local');assert.equal(local.profiles.length,1);
    const profiles=[{id:'local',name:'Local',kind:'local',origin:''},{id:'company',name:'Company 雪',kind:'nexus',origin:'https://nexus.example.test/xmind'}];
    const saved=await client(['connections','save','1',JSON.stringify(profiles)]);assert.equal(saved.revision,2);assert.equal(saved.profiles[1].name,'Company 雪');assert.equal(saved.profiles[1].authenticated,false);
    const origin=stdout.match(/xMind Server listening on (http:\/\/127\.0\.0\.1:\d+)/)?.[1];assert(origin);
    async function httpCatalog(){const response=await fetch(origin+'/v1/connections',{headers:{Authorization:'Bearer '+token}});assert.equal(response.status,200);return response.json();}
    assert.deepEqual(await httpCatalog(),saved);
    const changed=name=>JSON.stringify(profiles.map(p=>p.id==='company'?{...p,name}:p));
    const concurrent=await Promise.allSettled([client(['connections','save','2',changed('First')]),client(['connections','save','2',changed('Second')])]);assert.equal(concurrent.filter(r=>r.status==='fulfilled').length,1);assert.equal(concurrent.filter(r=>r.status==='rejected').length,1);
    const current=await client(['connections','list']);assert.equal(current.revision,3);assert.equal(current.active_id,'local');assert.deepEqual(await httpCatalog(),current);
    await assert.rejects(client(['connections','select','company','3']));await assert.rejects(client(['connections','select','missing','3']));
    await assert.rejects(client(['connections','save','3',JSON.stringify([{...profiles[0],access_token:'must-not-persist'}])]));
    await assert.rejects(client(['connections','save','2',JSON.stringify(profiles)]));
    assert.deepEqual(await client(['connections','select','local','3']),current);assert.deepEqual(await httpCatalog(),current);
    const identity=await client(['connections','open','local','1']);assert.equal(identity.connection_id,'local');assert.match(identity.server_id,/^local-[0-9a-f]{64}$/);assert.equal(identity.protocol_version,1);
    const bound=await client(['connections','session-create','local','1','bound-ipc-session','Captured 雪']);assert.deepEqual(bound.binding,identity);assert.equal(bound.session.title,'Captured 雪');assert.deepEqual(await client(['connections','session-open','local','1','bound-ipc-session']),bound);
    await assert.rejects(client(['connections','run-open','local','1','absent-captured-run']));
    await assert.rejects(client(['connections','run-start','local','1','unauthorized-captured-run','bound-ipc-session','must not dispatch'],'0'.repeat(64)));
    await assert.rejects(client(['connections','run-submit','local','1',JSON.stringify({id:'invalid-sdk-options',session_id:'bound-ipc-session',prompt:'must not dispatch',unknown_option:true})]));assert.equal(modelRequests,0);
    const started=await client(['connections','run-submit','local','1',JSON.stringify({id:'captured-sdk-completed',session_id:'bound-ipc-session',prompt:'Complete through the captured SDK binding',model_id:'sdk-fixture'})]);assert.deepEqual(started.binding,identity);assert.equal(started.run.id,'captured-sdk-completed');
    async function terminal(id,state){const until=Date.now()+10000;while(Date.now()<until){const receipt=await client(['connections','run-open','local','1',id]);assert.deepEqual(receipt.binding,identity);assert.equal(receipt.run.session_id,'bound-ipc-session');if(receipt.run.state===state)return receipt;assert(!['completed','failed','cancelled'].includes(receipt.run.state),'Unexpected terminal run state');await new Promise(resolve=>setTimeout(resolve,25));}throw Error('Scoped run did not reach '+state);}
    await terminal('captured-sdk-completed','completed');
    const events=await client(['connections','events','local','1','bound-ipc-session','captured-sdk-completed','null']);assert.equal(events.cursor.run_id,'captured-sdk-completed');assert(events.events.some(e=>e.kind==='run.completed'));
    await client(['connections','run-start','local','1','captured-sdk-cancelled','bound-ipc-session','Wait for scoped cancellation']);
    const dispatchDeadline=Date.now()+10000;while(modelRequests<2){assert(Date.now()<dispatchDeadline,'Cancellation fixture must reach actual provider stream');await new Promise(resolve=>setTimeout(resolve,25));}
    assert.deepEqual(await client(['connections','run-cancel','local','1','captured-sdk-cancelled']),{binding:identity,run_id:'captured-sdk-cancelled',cancel_requested:true});await terminal('captured-sdk-cancelled','cancelled');assert.equal(modelRequests,2);if(peerFailure)throw peerFailure;
    const httpOpen=await fetch(origin+'/v1/connection-bindings',{method:'POST',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:JSON.stringify({id:'local',expected_profile_revision:1})});assert.equal(httpOpen.status,201);const httpBinding=await httpOpen.json();assert.deepEqual(httpBinding.identity,identity);const httpBase=origin+'/v1/connection-bindings/'+httpBinding.binding_id;
    const httpSession=await fetch(httpBase+'/sessions/bound-ipc-session',{headers:{Authorization:'Bearer '+token}});assert.equal(httpSession.status,200);assert.deepEqual(await httpSession.json(),bound);

    await assert.rejects(client(['connections','open','company','1']));await assert.rejects(client(['connections','open','local','0']));
    const renamed=profiles.map(p=>p.id==='local'?{...p,name:'Renamed Local'}:p);const renamedCatalog=await client(['connections','save','3',JSON.stringify(renamed)]);assert.equal(renamedCatalog.profiles[0].revision,2);
    await assert.rejects(client(['connections','session-create','local','1','rejected-stale-binding','must not exist']));await assert.rejects(client(['session','open','rejected-stale-binding']));
    await assert.rejects(client(['connections','run-start','local','1','stale-captured-run','bound-ipc-session','must not dispatch']));await assert.rejects(client(['run','open','stale-captured-run']));assert.equal(modelRequests,2);
    const fresh=await client(['connections','session-open','local','2','bound-ipc-session']);assert.equal(fresh.binding.server_id,identity.server_id);assert.equal(fresh.binding.profile_revision,2);assert.equal(fresh.session.title,'Captured 雪');assert.deepEqual(await httpCatalog(),renamedCatalog);
    const httpStale=await fetch(httpBase+'/sessions',{method:'POST',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:JSON.stringify({id:'http-stale-after-sdk-edit',title:'must not exist'})});assert.equal(httpStale.status,409);const httpRetained=await fetch(httpBase+'/sessions/bound-ipc-session',{headers:{Authorization:'Bearer '+token}});assert.equal(httpRetained.status,200);assert.deepEqual(await httpRetained.json(),bound);assert.equal((await fetch(httpBase,{method:'DELETE',headers:{Authorization:'Bearer '+token}})).status,200);



    assert.deepEqual(await client(['session','create','integration-session','Native API 雪']),{id:'integration-session',title:'Native API 雪',parent_id:''});
    const opened=await Promise.all([client(['session','open','integration-session']),client(['session','open','integration-session'])]);assert(opened.every(s=>s.title==='Native API 雪'));
    await assert.rejects(client(['session','create','integration-session','duplicate']));
    await assert.rejects(client(['session','create','unauthorized-session','must not exist'],'0'.repeat(64)));
    await assert.rejects(client(['session','open','unauthorized-session']));
    await assert.rejects(client(['run','open','absent-run']));
    assert.equal((await client(['session','open','integration-session'])).title,'Native API 雪');
    assert(clients.every(pid=>pid&&pid!==backend.pid));assert(!stdout.includes(token)&&!stderr.includes(token));
    passed=true;console.log(JSON.stringify({backendPid:backend.pid,clientPids:clients,apiVersion:1,transport:'xlang3-lrpc',actualUnifiedExecutable:true,verified:['Root registered at backend startup','Native Host/Connections/Sessions/Runs subobjects','Distinct xmind.exe clients call SDK methods directly','Exact UTF-8 persisted session survives independent clients','Duplicate create and wrong authentication rejected','No unauthorized session created','Unknown run rejected','Captured run start/open/events and live cancellation use actual SDK service subobjects','Wrong-token and stale captured run admissions dispatch no model','SDK value catalog agrees with HTTP and persists UTF-8 metadata','Independent clients race one catalog revision; exactly one succeeds','Unauthenticated selection, wrong token, stale revision and credential fields rejected'],limitations:['No worker migration, model/tool/planning IPC or old dispatcher removal established by this gate','No live provider or rendering acceptance'],passed}));
}finally{backend.kill();await exited;peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));if(passed)await rm(root,{recursive:true,force:true});else console.error('Retained root API fixture: '+root+'; backend output: '+stdout+'; '+stderr);}
