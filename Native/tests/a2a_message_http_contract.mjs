// Real native A2A admission, agent workers and embedded-xlang3 persistence.
// Provider responses are explicitly synthetic protocol fixtures, not live inference.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn} from 'node:child_process';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
import {randomBytes} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
const [server,modules,stdlib]=process.argv.slice(2),root=await mkdtemp(join(tmpdir(),'xmind-a2a-message-'));
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;
let child,port,peerError;const requests=[];
const peer=createServer((request,response)=>{
 let raw='';request.on('data',bytes=>raw+=bytes);request.on('end',()=>{try{
  const body=JSON.parse(raw);requests.push(body);assert.equal(body.model,'synthetic-a2a-model');
  const content=body.messages.findLast(message=>message.role==='user').content;
  response.writeHead(200,{'Content-Type':'text/event-stream'});
  response.end(`data: ${JSON.stringify({choices:[{index:0,delta:{content:'Synthetic peer reply: '+content},finish_reason:null}]})}\n\ndata: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:'stop'}]})}\n\ndata: [DONE]\n\n`);
 }catch(error){peerError=error;response.writeHead(500);response.end('Synthetic peer error');}});
});
async function start(model=true){
 const args=['--db',join(root,'state.sqlite'),'--modules',modules,'--stdlib',stdlib,'--port','0'];
 if(model)args.push('--model','synthetic-a2a-model','--model-endpoint',`http://127.0.0.1:${peer.address().port}/chat`);
 child=spawn(server,args,{env,windowsHide:true});let errors='';child.stderr.on('data',bytes=>errors+=bytes);
 port=await new Promise((yes,no)=>{let output='';const timer=setTimeout(()=>no(new Error('Native readiness deadline: '+errors)),15000);child.once('error',error=>{clearTimeout(timer);no(error);});child.once('exit',code=>{clearTimeout(timer);no(new Error('Native exit '+code+': '+errors));});child.stdout.on('data',bytes=>{output+=bytes;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timer);yes(Number(match[1]));}});});
}
async function stop(){if(child&&child.exitCode===null){const exited=new Promise(resolve=>child.once('exit',resolve));child.kill();await exited;}child=null;}
async function api(path,body){const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+token,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(10000)});assert.ok(response.ok,await response.clone().text());return response.status===204?undefined:response.json();}
async function rpc(method,params){const value=await api('/a2a',{jsonrpc:'2.0',id:'message-contract',method,params});assert.equal(value.id,'message-contract');return value;}
function message(id,text,context){return {message:{kind:'message',role:'user',messageId:id,parts:[{kind:'text',text}],...(context?{contextId:context}:{})},configuration:{blocking:false,historyLength:8}};}
async function finished(id){const deadline=Date.now()+10000;while(Date.now()<deadline){if(peerError)throw peerError;const value=await rpc('tasks/get',{id,historyLength:8});assert.ok(value.result,JSON.stringify(value));if(value.result.status.state==='completed')return value.result;if(value.result.status.state==='failed')throw new Error(JSON.stringify(value));await delay(10);}throw new Error('Native task completion deadline');}
try{
 await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));await start();
 const invalid=[
  [{...message('push','a'),configuration:{pushNotificationConfig:{}}},-32003],
  [{message:{...message('file','a').message,parts:[{kind:'file',file:{bytes:'AA=='}}]}},-32005],
  [{...message('modes','a'),configuration:{acceptedOutputModes:['image/png']}},-32005],
  [{message:{...message('continue','a').message,taskId:'other'}},-32004]
 ];
 for(const [input,code] of invalid)assert.equal((await rpc('message/send',input)).error.code,code);
 assert.deepEqual(await api('/v1/sessions'),[],'Unsupported requests must not admit a context');
 const input=message('durable-first','first input');const sent=await Promise.all(Array.from({length:8},()=>rpc('message/send',input)));
 assert.ok(sent.every(value=>value.result),JSON.stringify(sent));const first=sent[0].result;
 assert.ok(sent.every(value=>value.result.id===first.id),'Concurrent retries must reuse one task');
 const completed=await finished(first.id);assert.equal(requests.length,1);assert.equal(completed.history.length,2);
 assert.equal(completed.history[0].messageId,'durable-first');assert.equal(completed.history[0].parts[0].text,'first input');
 assert.equal(completed.artifacts[0].parts[0].text,'Synthetic peer reply: first input');
 assert.equal((await rpc('message/send',input)).result.id,first.id);assert.equal(requests.length,1);
 assert.equal((await rpc('message/send',message('durable-first','first input',first.contextId))).result.id,first.id,'Retry may supply the returned context');
 assert.equal((await rpc('message/send',message('durable-first','changed input'))).error.code,-32004);
 assert.equal((await rpc('message/send',message('durable-first','first input','different-context'))).error.code,-32004);
 const next=message('durable-second','second input',first.contextId);next.message.parts=[{kind:'text',text:''},{kind:'text',text:'second input'}];
 const second=(await rpc('message/send',next)).result;assert.ok(second);assert.equal(second.contextId,first.contextId);assert.notEqual(second.id,first.id);
 const secondDone=await finished(second.id);assert.equal(requests.length,2);
 assert.ok(requests[1].messages.some(item=>item.role==='assistant'&&item.content==='Synthetic peer reply: first input'),'New task must retain model conversation context');
 assert.equal(secondDone.history.length,2);assert.equal(secondDone.history[0].parts[0].text,'\nsecond input');
 assert.ok(!secondDone.history.some(item=>item.parts[0].text==='first input'),'Task history must exclude earlier context prompts');
 assert.equal((await rpc('tasks/get',{id:first.id,historyLength:1})).result.history.length,1);
 const sessions=await api('/v1/sessions');assert.equal(sessions.length,1);assert.equal((await api('/v1/sessions/'+first.contextId+'/history')).length,4);
 await stop();await start(false);
 assert.equal((await rpc('message/send',input)).result.id,first.id,'Completed durable retry must work without an available agent');
 assert.equal((await rpc('message/send',next)).result.id,second.id);assert.equal(requests.length,2);
 assert.equal((await rpc('message/send',message('new-unavailable','new'))).error.code,-32004);assert.equal((await api('/v1/sessions')).length,1);
 console.log('Native A2A message contract passed real admission, concurrent durable retries, default model transport, context reuse, isolated history, rejected requests and restart reuse. Provider replies are synthetic; full A2A compliance is not established.');
}finally{await stop();await new Promise(resolve=>peer.close(resolve));assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-a2a-message-'));await rm(root,{recursive:true,force:true});}
