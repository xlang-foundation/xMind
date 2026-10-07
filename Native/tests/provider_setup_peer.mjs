// Synthetic provider only; actual native runtime, encrypted storage and faults.
import assert from 'node:assert/strict';import {createServer} from 'node:http';import {spawn} from 'node:child_process';import {mkdtemp,rm,readFile} from 'node:fs/promises';import {join,resolve,dirname,basename} from 'node:path';import {tmpdir} from 'node:os';
import {execFile} from 'node:child_process';import {promisify} from 'node:util';
const execute=promisify(execFile);
const [binary,modules,stdlib,cli]=process.argv.slice(2),root=await mkdtemp(join(tmpdir(),'xmind-provider-setup-'));let response,released=false,failure,requests=0,discoveries=0,responsesRequests=0;
if(!cli)throw new Error('Actual native CLI executable is required for discovery interoperability');
async function cliDiscovery(port){
  const env={...process.env,XMIND_AUTH_TOKEN:'synthetic-provider-setup-server-access-token',FIXTURE_DISCOVERY_KEY:'synthetic-provider-setup-key'};
  for(const args of [['provider-models'],['provider-models','FIXTURE_DISCOVERY_KEY','2']]){
    const result=await execute(cli,[port,...args],{env,windowsHide:true,timeout:10000});assert.deepEqual(JSON.parse(result.stdout),{models:[{id:'fixture-model'},{id:'fixture-other'}]});assert.ok(!result.stdout.includes(env.FIXTURE_DISCOVERY_KEY));
  }
  const interactiveExecution=execute(cli,[port,'chat'],{env,windowsHide:true,timeout:10000});interactiveExecution.child.stdin.end('/provider-models\n/exit\n');const interactive=await interactiveExecution;
  const interactiveRecords=interactive.stdout.trim().split('\n').map(line=>JSON.parse(line));assert.equal(interactiveRecords.length,1);assert.equal(interactiveRecords[0].type,'provider_models');assert.equal(interactiveRecords[0].provider_revision,2);assert.deepEqual(interactiveRecords[0].catalogue,{models:[{id:'fixture-model'},{id:'fixture-other'}]});assert.ok(!interactive.stdout.includes(env.FIXTURE_DISCOVERY_KEY));
  for(const args of [['provider-models','FIXTURE_DISCOVERY_KEY','1'],['provider-models','XMIND_AUTH_TOKEN','2']]){
    let rejected;try{await execute(cli,[port,...args],{env,windowsHide:true,timeout:10000});}catch(error){rejected=error;}assert.equal(rejected?.code,1,'Stale revision/auth-token substitution must fail actual CLI discovery');assert.ok(!rejected.stdout.includes(env.FIXTURE_DISCOVERY_KEY));assert.ok(!rejected.stderr.includes(env.XMIND_AUTH_TOKEN));
  }
}
function finish(){if(response&&released){response.writeHead(200,{'Content-Type':'text/event-stream'});response.end(`data: ${JSON.stringify({choices:[{index:0,delta:{content:'Synthetic response over actual configured native transport'},finish_reason:'stop'}]})}\n\ndata: [DONE]\n\n`);response=undefined;}}
const peer=createServer((request,reply)=>{let raw='';request.on('data',data=>raw+=data);request.on('end',()=>{try{
  if(request.url==='/chat/responses'){
    responsesRequests++;assert.equal(request.headers.authorization,'Bearer synthetic-provider-setup-key');assert.ok(!raw.includes('synthetic-provider-setup-key'));
    const body=JSON.parse(raw);assert.equal(body.model,'gpt-5.6-sol');assert.equal(body.store,false);assert.equal(body.stream,true);assert.ok(Array.isArray(body.input));assert.ok(!Object.hasOwn(body,'messages'));assert.ok(!Object.hasOwn(body,'reasoning_effort'));
    assert.equal(body.input.findLast(item=>item.role==='user').content[0].text,'Responses enrollment fixture');
    reply.writeHead(200,{'Content-Type':'text/event-stream'});let sequence=0;const send=(type,fields)=>reply.write(`event: ${type}\ndata: ${JSON.stringify({type,sequence_number:sequence++,...fields})}\n\n`);
    const id='resp_setup_fixture',itemId='msg_setup_fixture',part={type:'output_text',text:'Synthetic Responses enrollment reply',annotations:[]},item={id:itemId,type:'message',role:'assistant',status:'completed',content:[part]},base={output_index:0,item_id:itemId,content_index:0};
    send('response.created',{response:{id,model:body.model,status:'in_progress'}});send('response.output_item.added',{output_index:0,item:{...item,status:'in_progress',content:[]}});send('response.content_part.added',{...base,part:{...part,text:''}});send('response.output_text.delta',{...base,delta:part.text});send('response.output_text.done',{...base,text:part.text});send('response.content_part.done',{...base,part});send('response.output_item.done',{output_index:0,item});send('response.completed',{response:{id,model:body.model,status:'completed',output:[item],usage:{input_tokens:11,output_tokens:7,total_tokens:18}}});reply.end();return;
  }
  if(request.url==='/chat/models'){
    ++discoveries;assert.equal(request.method,'GET');assert.equal(raw,'');assert.equal(request.headers.accept,'application/json');
    if(request.headers.authorization==='Bearer fixture-rejected-key'){reply.writeHead(401,{'Content-Type':'application/json'});reply.end(JSON.stringify({error:{type:'authentication_error',code:'invalid_api_key',param:'fixture-rejected-key',message:'fixture-rejected-key'}}));return;}
    reply.writeHead(200,{'Content-Type':'application/json; charset=utf-8'});
    if(request.headers.authorization==='Bearer fixture-malformed-key'){reply.end('{"object":"list","data":[{"object":"model","id":"fixture-a","id":"fixture-malformed-key"}]}');return;}
    assert.equal(request.headers.authorization,'Bearer synthetic-provider-setup-key');reply.end(JSON.stringify({object:'list',private_field:'synthetic-provider-setup-key',data:[{object:'model',id:'fixture-other'},{object:'model',id:'fixture-model'},{object:'model',id:'fixture-other'}]}));return;
  }
  ++requests;assert.equal(request.headers.authorization,'Bearer synthetic-provider-setup-key');assert.equal(JSON.parse(raw).model,'fixture-model');response=reply;finish();
}catch(error){failure=error;reply.writeHead(500);reply.end('Synthetic provider fixture rejected request');}});});
try{await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));const child=spawn(binary,[root,modules,stdlib,`http://127.0.0.1:${peer.address().port}/chat`],{windowsHide:true});let out='',err='',cliStarted=false;child.stdout.on('data',data=>{out+=data;if(out.includes('fixture-release')){released=true;finish();}const port=/fixture-cli-port:(\d+)/.exec(out);if(port&&!cliStarted){cliStarted=true;cliDiscovery(port[1]).then(()=>child.stdin.write('fixture-cli-done\n'),error=>{failure=error;child.stdin.write('fixture-cli-failed\n');});}});child.stderr.on('data',data=>err+=data);const timer=setTimeout(()=>child.kill(),30000);const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});clearTimeout(timer);if(failure)throw failure;assert.equal(code,0,err);assert.equal(requests,1);assert.equal(responsesRequests,2);assert.equal(discoveries,8);const database=await readFile(join(root,'provider.sqlite'));assert.ok(!database.includes(Buffer.from('synthetic-provider-setup-key')),'Stored DB must not contain fixture key in plaintext');process.stdout.write(out);}finally{peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-provider-setup-'));await rm(root,{recursive:true,force:true});}
