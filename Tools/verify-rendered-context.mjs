// Manual browser acceptance harness. Native context service owns every lifecycle
// transition. The browser must click Compact once and verify reload; this peer
// supplies bounded Responses fixtures, never a paid model or live credential.
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {setTimeout as delay} from 'node:timers/promises';
import {pathToFileURL} from 'node:url';
// Optional final argument runs independent xmind SDK clients instead of UI.
const [binary,modules,stdlib,assets,out,sdkClient]=process.argv.slice(2);
assert(binary&&modules&&stdlib&&assets&&out,'Expected native contract, modules, stdlib, assets and fresh output');
const fixtureBase=path.resolve('D:/CantorAI2026/TestProj/xmind-delivery-validation');
const relative=path.relative(fixtureBase,path.resolve(out));
assert(relative&&!relative.startsWith('..')&&!path.isAbsolute(relative),'Use a fresh fixture below the designated TestProj validation directory');
const sha=b=>createHash('sha256').update(b).digest('hex');
await fs.mkdir(out);
const nativeBefore=sha(await fs.readFile(binary)),counts={inference:0,count:0,compact:0},originals=new Map();
let failure,windowBytes,child,view,stdout='',stderr='';
const token='synthetic-context-control-access-token-32-bytes',key='synthetic-context-control-provider-key-not-live';
const text=item=>(item.content??[]).filter(c=>c.type==='input_text'||c.type==='output_text').map(c=>c.text).join('');
function inference(response,model){
 const id='rendered_'+counts.inference,reasoning={id:'rs_'+id,type:'reasoning',status:'completed',summary:[],content:[],encrypted_content:'controlled-private-'+id};
 const message={id:'msg_'+id,type:'message',role:'assistant',status:'completed',phase:null,content:[{type:'output_text',text:'Controlled native context seed '+counts.inference+'.',annotations:[]}]};
 originals.set(reasoning.id,reasoning);originals.set(message.id,message);
 response.writeHead(200,{'Content-Type':'text/event-stream'});let sequence=0;
 const emit=(type,fields)=>response.write('event: '+type+'\ndata: '+JSON.stringify({type,sequence_number:sequence++,...fields})+'\n\n');
 emit('response.created',{response:{id:'resp_'+id,model,status:'in_progress'}});
 emit('response.output_item.added',{output_index:0,item:{id:reasoning.id,type:'reasoning',summary:[]}});emit('response.output_item.done',{output_index:0,item:reasoning});
 emit('response.output_item.added',{output_index:1,item:{id:message.id,type:'message',role:'assistant',content:[]}});
 const position={output_index:1,item_id:message.id,content_index:0};emit('response.content_part.added',{...position,part:{type:'output_text',text:''}});emit('response.output_text.delta',{...position,delta:message.content[0].text});emit('response.output_text.done',{...position,text:message.content[0].text});emit('response.content_part.done',{...position,part:message.content[0]});emit('response.output_item.done',{output_index:1,item:message});
 emit('response.completed',{response:{id:'resp_'+id,model,status:'completed',output:[reasoning,message],usage:{input_tokens:37,output_tokens:5,total_tokens:42,output_tokens_details:{reasoning_tokens:1}}}});response.end();
}
const peer=createServer((request,response)=>{let raw='';request.on('data',b=>raw+=b);request.on('end',async()=>{try{
 assert.equal(request.method,'POST');assert.equal(request.headers.authorization,'Bearer '+key);
 const kind=request.url==='/rendered/responses'?'inference':request.url==='/rendered/responses/input_tokens'?'count':request.url==='/rendered/responses/compact'?'compact':null;assert(kind);
 const body=JSON.parse(raw);assert.equal(body.model,'synthetic-control-a');assert(Array.isArray(body.input));if(kind==='compact')assert.equal(body.tools,undefined);else assert(body.tools===undefined||Array.isArray(body.tools));
 for(const item of body.input)if(originals.has(item.id))assert.deepEqual(item,originals.get(item.id));
 const ordinal=++counts[kind];await fs.appendFile(out+'/peer-audit.jsonl',JSON.stringify({kind,scope:'owned',model:body.model,ordinal})+'\n');
 if(kind==='inference'){assert(ordinal<=3);assert(text(body.input.filter(i=>i.role==='user').at(-1)).endsWith(':seed:'+(ordinal-1)));return inference(response,body.model);}
 assert.deepEqual(Object.keys(body).sort(),body.tools===undefined?['input','model']:['input','model','tools']);assert.equal(request.headers.accept,'application/json');response.writeHead(200,{'Content-Type':'application/json'});
 if(kind==='count'){assert(ordinal<=2);const projected=body.input.some(i=>i.type==='compaction');assert.equal(projected,ordinal===2);if(projected)assert(raw.includes(windowBytes.slice(1,-1)));return response.end(JSON.stringify({object:'response.input_tokens',input_tokens:projected?80:400}));}
 assert.equal(ordinal,1);const users=body.input.filter(i=>i.role==='user');assert.equal(users.length,3);
 windowBytes=JSON.stringify([...users.map((item,i)=>({...item,id:item.id??'controlled_user_'+i,status:'completed'})),{id:'cmp_rendered',type:'compaction',encrypted_content:'controlled-private-compaction'}]);
 response.end('{"id":"compact_rendered","object":"response.compaction","created_at":1764967971,"output":'+windowBytes+',"usage":{"input_tokens":31,"output_tokens":7,"total_tokens":38}}');
 }catch(error){failure??=String(error.stack);await fs.writeFile(out+'/private-peer-failure.log',failure);if(!response.headersSent)response.writeHead(500);response.end('Controlled context peer rejected request');}});});
await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
const originalFetch=globalThis.fetch,routes=[];
globalThis.fetch=async(input,options)=>{const response=await originalFetch(input,options),pathname=new URL(input).pathname;if(pathname.includes('/connection-bindings')&&pathname.includes('/context')&&(response.headers.get('content-type')??'').startsWith('application/json')){routes.push({path:pathname,method:options?.method??'GET',status:response.status,receipt:await response.clone().json()});await fs.writeFile(out+'/routes.json',JSON.stringify(routes,null,2)+'\n');}return response;};
try{
 child=spawn(binary,[out,modules,stdlib,'http://127.0.0.1:'+peer.address().port,'rendered'],{windowsHide:true,stdio:['ignore','pipe','pipe']});
 const terminal=new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',(code,signal)=>resolve({code,signal}));});child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);
 const deadline=Date.now()+20000;let ready;while(Date.now()<deadline){assert.equal(child.exitCode,null,stderr);try{ready=JSON.parse(await fs.readFile(out+'/rendered-ready.json'));break;}catch(e){if(e.code!=='ENOENT')throw e;}await delay(25);}assert(ready,stderr);assert(!failure,failure);
 if(sdkClient){
  const execute=promisify(execFile),calls=[];
  const ipc=async(...args)=>{const result=await execute(sdkClient,['ipc','--endpoint',ready.endpoint,'connections',...args],{windowsHide:true,env:{...process.env,XMIND_AUTH_TOKEN:token},timeout:15000});const receipt=JSON.parse(result.stdout);calls.push({args,receipt});return receipt;};
  const before=await ipc('context-open','local','1','owned','synthetic-control-a');assert.equal(before.context.head_revision,0);
  const id='independent-sdk-compaction',admitted=await ipc('context-compact','local','1','owned',JSON.stringify({id,model_id:'synthetic-control-a',expected_head_revision:0}));assert.equal(admitted.request.id,id);
  let status;const until=Date.now()+15000;do{status=await ipc('context-request','local','1','owned',id,'synthetic-control-a');if(status.request.state==='completed')break;assert(['pending','claimed'].includes(status.request.state));await delay(20);}while(Date.now()<until);assert.equal(status.request.state,'completed');
  const after=await ipc('context-open','local','1','owned','synthetic-control-a');assert.equal(after.context.head_revision,1);assert.deepEqual(after.context.checkpoint.usage,{input_tokens:31,output_tokens:7,total_tokens:38});
  await fs.writeFile(out+'/sdk-calls.json',JSON.stringify(calls,null,2)+'\n',{flag:'wx'});await fs.writeFile(out+'/rendered-complete.json',JSON.stringify({passed:true,transport:'Separate xmind.exe processes using registered xlang3 root/Contexts subobject methods',request:id,headRevision:1,neverReplay:true})+'\n',{flag:'wx'});
 }else{
  const {createNativeBrowserFixture}=await import(pathToFileURL(path.resolve('views/browser/tests/native-host.mjs')).href);view=await createNativeBrowserFixture({modules,stdlib,backend:ready.origin,assetRoot:assets,localAccessToken:token});const origin=await view.listen(0);
  await fs.writeFile(out+'/live.json',JSON.stringify({pid:child.pid,browser:origin+'/ui/',...ready},null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({pid:child.pid,browser:origin+'/ui/',session:ready.session}));
 }
 const result=await terminal;await fs.writeFile(out+'/native-terminal.json',JSON.stringify(result,null,2)+'\n',{flag:'wx'});assert.equal(result.code,0,stderr);assert(!failure,failure);
 assert.deepEqual(counts,{inference:3,count:2,compact:1});const mutations=routes.filter(r=>r.method==='POST');assert.equal(mutations.length,sdkClient?0:1);if(!sdkClient){assert.equal(mutations[0].status,202);assert(mutations[0].path.endsWith('/sessions/owned/context/compact'));}
 assert.equal(sha(await fs.readFile(binary)),nativeBefore);
 await fs.writeFile(out+'/gate.json',JSON.stringify({passed:true,scope:sdkClient?'Actual separate xmind.exe clients invoke standard registered xlang3 root and Contexts methods; real native execution/maintenance/SQLite, controlled provider; test service host.':'Actual rendered browser invokes captured native context service with three genuine native seed runs, controlled Responses peer, real SQLite; test service host, not installed production provider configuration.',counts,routes,contractSha256:nativeBefore,paidInference:false,installed:false,neverReplay:true,ownedNativeExited:true},null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({passed:true,counts}));
}finally{
 await fs.writeFile(out+'/native-stdout.log',stdout);await fs.writeFile(out+'/native-stderr.log',stderr);if(view)await view.close();
 if(child?.exitCode===null&&child?.signalCode===null){console.log(JSON.stringify({retained:true,pid:child.pid,reason:'Native acceptance has not retired; preserve current task'}));}
 peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));
}
