// Actual native server/agent/storage/SSE. Provider replies are explicitly
// synthetic protocol fixtures, with held responses to verify live behavior.
import assert from 'node:assert/strict';import {createServer} from 'node:http';import {spawn} from 'node:child_process';
import {mkdtemp,writeFile,rm} from 'node:fs/promises';import {tmpdir} from 'node:os';import {join,resolve,dirname,basename} from 'node:path';
import {randomBytes} from 'node:crypto';import {setTimeout as delay} from 'node:timers/promises';
import {ClientFactory,DefaultAgentCardResolver,JsonRpcTransportFactory} from './sdk/node_modules/@a2a-js/sdk/dist/client/index.js';
import {Role,TaskState} from './sdk/node_modules/@a2a-js/sdk/dist/index.js';
const [server,modules,stdlib]=process.argv.slice(2),root=await mkdtemp(join(tmpdir(),'xmind-a2a-stream-'));
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;
let child,port,peerError;const requests=[],held=new Map(),streams=[];
const delta=text=>`data: ${JSON.stringify({choices:[{index:0,delta:{content:text},finish_reason:null}]})}\n\n`;
const finish=`data: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:'stop'}]})}\n\ndata: [DONE]\n\n`;
const peer=createServer((request,response)=>{let raw='';request.on('data',bytes=>raw+=bytes);request.on('end',()=>{try{
 const body=JSON.parse(raw),input=body.messages.findLast(message=>message.role==='user').content;requests.push(body);
 assert.equal(body.model,'synthetic-a2a-stream-model');
 if(input==='many-events'){response.writeHead(200,{'Content-Type':'text/event-stream'});response.end(delta('x').repeat(300)+finish);return;}
 if(input==='refusal'){response.writeHead(200,{'Content-Type':'text/event-stream'});response.end(`data: ${JSON.stringify({choices:[{index:0,delta:{refusal:'Synthetic refusal fixture'},finish_reason:null}]})}\n\n`+finish);return;}
 if(input==='tool-turn'){
  const tool=body.messages.findLast(message=>message.role==='tool');response.writeHead(200,{'Content-Type':'text/event-stream'});
  if(tool){assert.equal(JSON.parse(tool.content).content,'Actual workspace bytes for streamed tool result');response.end(delta('Synthetic final after native tool')+finish);}
  else response.end(`data: ${JSON.stringify({choices:[{index:0,delta:{content:'Synthetic interim tool explanation',tool_calls:[{index:0,id:'fixture-read',type:'function',function:{name:'read_file',arguments:JSON.stringify({path:'observed.txt'})}}]},finish_reason:null}]})}\n\ndata: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:'tool_calls'}]})}\n\ndata: [DONE]\n\n`);return;
 }
 if(input==='fail'){response.writeHead(503);response.end('DO_NOT_ECHO_PROVIDER_BODY');return;}
 response.writeHead(200,{'Content-Type':'text/event-stream'});
 if(['slow','cancel','sdk-slow'].includes(input)){response.write(delta('Synthetic prefix 🌍 '));held.set(input,response);}
 else response.end(delta('Synthetic complete reply')+finish);
 }catch(error){peerError=error;response.writeHead(500);response.end('Synthetic peer error');}});});
async function start(model=true){const args=['--db',join(root,'state.sqlite'),'--modules',modules,'--stdlib',stdlib,'--port','0','--graphs-config',join(root,'graphs.json')];if(model)args.push('--model','synthetic-a2a-stream-model','--model-endpoint',`http://127.0.0.1:${peer.address().port}/chat`,'--workspace',root,'--model-tools','supported');child=spawn(server,args,{env,windowsHide:true});let errors='';child.stderr.on('data',bytes=>errors+=bytes);port=await new Promise((yes,no)=>{let output='';const timer=setTimeout(()=>no(new Error('Readiness deadline: '+errors)),15000);child.once('error',error=>{clearTimeout(timer);no(error);});child.once('exit',code=>{clearTimeout(timer);no(new Error('Native exit '+code+': '+errors));});child.stdout.on('data',bytes=>{output+=bytes;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timer);yes(Number(match[1]));}});});}
async function stop(){if(child&&child.exitCode===null){const ended=new Promise(resolve=>child.once('exit',resolve));child.kill();await ended;}child=null;}
async function response(path,body,headers={},signal=AbortSignal.timeout(10000)){return fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+token,...(body===undefined?{}:{'Content-Type':'application/json'}),...headers},body:body===undefined?undefined:JSON.stringify(body),signal});}
async function api(path,body){const value=await response(path,body);assert.ok(value.ok,await value.clone().text());return value.status===204?undefined:value.json();}
async function rpc(method,params,id='control'){return api('/a2a',{jsonrpc:'2.0',id,method,params});}
function input(id,text){return {message:{kind:'message',role:'user',messageId:id,parts:[{kind:'text',text}]},configuration:{blocking:false,historyLength:8}};}
async function until(read,predicate){const deadline=Date.now()+10000;while(Date.now()<deadline){if(peerError)throw peerError;const value=await read();if(predicate(value))return value;await delay(10);}throw new Error('Actual SSE observation deadline');}
async function open(method,params,id){
 const controller=new AbortController(),value=await response('/a2a',{jsonrpc:'2.0',id,method,params},{},controller.signal);assert.equal(value.status,200);
 if(!value.headers.get('content-type')?.startsWith('text/event-stream'))return {error:await value.json()};
 const stream={controller,records:[],done:false,error:null};streams.push(stream);
 stream.pump=(async()=>{const reader=value.body.getReader(),decoder=new TextDecoder();let pending='';try{for(;;){const chunk=await reader.read();if(chunk.done)break;pending+=decoder.decode(chunk.value,{stream:true});for(;;){const index=pending.indexOf('\n\n');if(index<0)break;const frame=pending.slice(0,index);pending=pending.slice(index+2);const data=frame.split('\n').filter(line=>line.startsWith('data: ')).map(line=>line.slice(6)).join('\n');if(data){const parsed=JSON.parse(data);assert.equal(parsed.id,id);assert.equal(parsed.jsonrpc,'2.0');stream.records.push(parsed);}}}assert.equal(pending,'');}catch(error){if(!controller.signal.aborted)stream.error=error;}finally{stream.done=true;reader.releaseLock();}})();return stream;
}
async function task(stream){assert.ok(!stream.error,JSON.stringify(stream.error));await until(()=>stream.records.length,count=>count>0);assert.equal(stream.records[0].result.kind,'task');return stream.records[0].result;}
async function close(stream){if(stream&&!stream.done){stream.controller.abort();await stream.pump;}}
async function final(stream,state){await until(()=>stream.done,value=>value);if(stream.error)throw stream.error;assert.equal(stream.records.at(-1).result.kind,'status-update');assert.equal(stream.records.at(-1).result.final,true);assert.equal(stream.records.at(-1).result.status.state,state);assert.ok(stream.records.slice(0,-1).every(item=>!item.result?.final));}
function assembled(stream){let result=stream.records[0]?.result?.artifacts?.[0]?.parts?.[0]?.text||'';for(const item of stream.records)if(item.result?.kind==='artifact-update'){const event=item.result;if(event.append)result+=event.artifact.parts[0].text;else result=event.artifact.parts[0].text;}return result;}
try{
 await writeFile(join(root,'observed.txt'),'Actual workspace bytes for streamed tool result');
 await writeFile(join(root,'graphs.json'),JSON.stringify({graphs:[{id:'human',spec:{nodes:[{id:'answer',type:'human',prompt:'Actual input needed'}]}}]}));await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));await start();
 assert.equal((await response('/.well-known/agent-card.json',undefined,{Authorization:''})).status,401);assert.equal((await response('/.well-known/agent-card.json',undefined,{Origin:'https://outside.invalid'})).status,403);
 const card=await api('/.well-known/agent-card.json');assert.equal(card.protocolVersion,'0.3.0');assert.equal(card.url,`http://127.0.0.1:${port}/a2a`);assert.equal(card.preferredTransport,'JSONRPC');assert.equal(card.capabilities.streaming,true);assert.equal(card.capabilities.pushNotifications,false);assert.equal(card.supportsAuthenticatedExtendedCard,false);assert.equal(card.securitySchemes.ownerToken.scheme,'bearer');assert.deepEqual(card.skills.map(skill=>skill.id),['native.agent']);assert.ok(!JSON.stringify(card).includes(token));
 const notification=await response('/a2a',{jsonrpc:'2.0',method:'message/stream',params:input('notification','unused')});assert.equal(notification.status,204);assert.deepEqual(await api('/v1/sessions'),[],'Streaming notification cannot admit an unobservable task');
 const first=await open('message/stream',input('slow-message','slow'),'stream-1'),run=await task(first);
 await until(()=>assembled(first),text=>text==='Synthetic prefix 🌍 ');assert.equal(first.done,false,'Partial output must arrive before provider completion');
 const second=await open('tasks/resubscribe',{id:run.id},'resubscribe-2');assert.equal((await task(second)).id,run.id);await until(()=>assembled(second),text=>text==='Synthetic prefix 🌍 ');
 assert.equal((await rpc('message/stream',input('capacity-rejected','unused'))).error.code,-32004);assert.equal((await api('/v1/sessions')).length,1,'Capacity rejection must precede admission');
 assert.equal((await api('/v1/health')).agent_execution,true);assert.equal((await rpc('tasks/get',{id:run.id})).result.status.state,'working','Control requests must remain available with two active streams');
 await close(first);let third;await until(async()=>{const candidate=await open('tasks/resubscribe',{id:run.id},'resubscribe-3');if(!candidate.error)third=candidate;return candidate;},candidate=>!candidate.error);await task(third);
 await close(second);assert.equal((await rpc('tasks/get',{id:run.id})).result.status.state,'working','Disconnecting viewers must not cancel execution');
 held.get('slow').end(delta('suffix')+finish);await final(third,'completed');assert.equal(assembled(third),'Synthetic prefix 🌍 suffix');assert.equal(requests.length,1,'Reconnect must not repeat inference');
 const completed=await open('message/stream',input('slow-message','slow'),'completed-retry');assert.equal((await task(completed)).id,run.id);await final(completed,'completed');assert.equal(assembled(completed),'Synthetic prefix 🌍 suffix');assert.equal(requests.length,1);
 const failed=await open('message/stream',input('failure-message','fail'),'failure');await task(failed);await final(failed,'failed');assert.ok(!JSON.stringify(failed.records).includes('DO_NOT_ECHO_PROVIDER_BODY'));
 const cancelled=await open('message/stream',input('cancel-message','cancel'),'cancellation'),cancelRun=await task(cancelled);await until(()=>assembled(cancelled),text=>text==='Synthetic prefix 🌍 ');await rpc('tasks/cancel',{id:cancelRun.id});await final(cancelled,'canceled');
 await api('/v1/sessions',{id:'human-context',title:'Actual graph stream'});const graph=await api('/v1/graph-runs',{session_id:'human-context',graph_id:'human',graph_revision:1,prompt:'Wait for actual human input'});await until(()=>rpc('tasks/get',{id:graph.id}),value=>value.result.status.state==='input-required');
 const human=await open('tasks/resubscribe',{id:graph.id},'human');await task(human);await final(human,'input-required');assert.equal((await rpc('tasks/get',{id:graph.id})).result.status.state,'input-required');await rpc('tasks/cancel',{id:graph.id});
 // Official SDK 1.3.0 uses its explicit v0.3 compatibility transport. Native
 // v1.0 wire support is not inferred from translated client-side objects.
 const origin=`http://127.0.0.1:${port}`;
 const authenticatedFetch=(url,init={})=>{assert.equal(new URL(url).origin,origin);const headers=new Headers(init.headers);headers.set('Authorization','Bearer '+token);if(new URL(url).pathname==='/.well-known/agent-card.json')headers.set('A2A-Version','0.3');return fetch(url,{...init,headers,redirect:'error',signal:init.signal?AbortSignal.any([init.signal,AbortSignal.timeout(10000)]):AbortSignal.timeout(10000)});};
 const factory=new ClientFactory({cardResolver:new DefaultAgentCardResolver({fetchImpl:authenticatedFetch,legacyCompat:{enabled:true}}),transports:[new JsonRpcTransportFactory({fetchImpl:authenticatedFetch,legacyCompat:{enabled:true}})]});
 const client=await factory.createFromUrl(origin),sdkRecords=[];let sdkError;
 const sdkPump=(async()=>{for await(const event of client.sendMessageStream({message:{messageId:'sdk-message',role:Role.ROLE_USER,parts:[{content:{$case:'text',value:'sdk-slow'}}]},configuration:{returnImmediately:true,historyLength:8}}))sdkRecords.push(event);})().catch(error=>{sdkError=error;});
 await until(()=>{if(sdkError)throw sdkError;return sdkRecords;},records=>records.some(event=>event.payload?.$case==='artifactUpdate'));
 assert.equal(sdkRecords[0].payload.$case,'task');const sdkId=sdkRecords[0].payload.value.id;
 assert.equal((await client.getTask({id:sdkId,historyLength:8})).status.state,TaskState.TASK_STATE_WORKING);
 held.get('sdk-slow').end(delta('suffix')+finish);await sdkPump;if(sdkError)throw sdkError;
 assert.equal(sdkRecords.at(-1).payload.$case,'statusUpdate');assert.equal(sdkRecords.at(-1).payload.value.status.state,TaskState.TASK_STATE_COMPLETED);
 const sdkTask=await client.getTask({id:sdkId,historyLength:8});assert.equal(sdkTask.artifacts[0].parts[0].content.value,'Synthetic prefix 🌍 suffix');
 const toolStream=await open('message/stream',input('tool-message','tool-turn'),'tool-turn');await task(toolStream);await final(toolStream,'completed');assert.equal(assembled(toolStream),'Synthetic final after native tool','New assistant turns replace interim text rather than concatenating it into the final artifact');
 assert.ok(toolStream.records.some(item=>item.result?.kind==='artifact-update'&&item.result.artifact.parts[0].text==='Synthetic interim tool explanation'));
 const paged=await open('message/stream',input('paged-message','many-events'),'paged-events');await task(paged);await final(paged,'completed');assert.equal(assembled(paged),'x'.repeat(300));assert.ok(paged.records.filter(item=>item.result?.kind==='artifact-update').length>=301,'Stream must drain multiple bounded event pages before its final status');
 const refusal=await open('message/stream',input('refusal-message','refusal'),'refusal');const refusalRun=await task(refusal);await final(refusal,'completed');assert.equal(assembled(refusal),'Synthetic refusal fixture');const refusedTask=(await rpc('tasks/get',{id:refusalRun.id,historyLength:8})).result;assert.equal(refusedTask.artifacts[0].parts[0].text,'Synthetic refusal fixture');assert.equal(refusedTask.history.at(-1).parts[0].text,'Synthetic refusal fixture');
 await stop();await start(false);assert.deepEqual((await api('/.well-known/agent-card.json')).skills,[],'Unavailable execution must not advertise an executable skill');
 const restarted=await open('tasks/resubscribe',{id:run.id},null);await task(restarted);await final(restarted,'completed');assert.equal(assembled(restarted),'Synthetic prefix 🌍 suffix');assert.equal(requests.length,8);
 console.log('Native A2A streaming passed actual persisted partial output, Task-first JSON-RPC SSE, reconnect/backfill without reexecution, bounded stream capacity and control fairness, disconnect isolation, terminal/failure/cancel/input-required closure, authenticated discovery, real file-tool turn replacement, multi-page event draining, refusal preservation and restart. Official SDK 1.3.0 passed explicit v0.3 discovery/stream/get interoperability. Provider replies are synthetic; full A2A compliance and native v1 wire support remain unproven.');
}finally{for(const stream of streams)await close(stream);await stop();for(const reply of held.values())reply.destroy();await new Promise(resolve=>peer.close(resolve));assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-a2a-stream-'));await rm(root,{recursive:true,force:true});}
