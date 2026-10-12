// End-to-end HTTP MCP AgentRunner subscription lifecycle using native binaries.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {randomBytes} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';

const [serverExe,cliExe,adminExe,modules,stdlib]=process.argv.slice(2);
for(const [name,value] of Object.entries({serverExe,cliExe,adminExe,modules,stdlib}))assert.ok(value,`${name} is required`);
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-http-subscription-'));
const workspace=join(folder,'workspace'),db=join(folder,'state.sqlite'),effect=join(workspace,'effect.txt');
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};
delete env.XMIND_API_KEY;delete env.XMIND_SETUP_SECRET;
let backend,backendPort,backendErrors='',providerError,mcpError,providerRequests=0,mcpMethods=[];
let listenerClosedResolve;const listenerClosed=new Promise(resolve=>listenerClosedResolve=resolve);
const provider=createServer((request,response)=>{
  let source='';request.on('data',chunk=>source+=chunk);request.on('end',()=>{
    try{
      providerRequests++;const body=JSON.parse(source),names=body.tools.map(item=>item.function.name);
      const mcpNames=names.filter(name=>/^mcp_[a-f0-9]{48}$/.test(name));assert.equal(mcpNames.length,1,JSON.stringify(names));
      assert.equal(source.includes('http-subscription-credential'),false);
      const tool=body.messages.findLast(message=>message.role==='tool');let delta,finish;
      if(tool){const result=JSON.parse(tool.content);assert.equal(result.acknowledged_by_peer,true);assert.equal(result.independently_verified,false);delta={content:'HTTP MCP result consumed by AgentRunner'};finish='stop';}
      else{delta={tool_calls:[{index:0,id:'http-mcp-effect-1',type:'function',function:{name:mcpNames[0],arguments:'{"body":"http-approved"}'}}]};finish='tool_calls';}
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.end(`data: ${JSON.stringify({choices:[{index:0,delta,finish_reason:null}]})}\n\ndata: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:finish}]})}\n\ndata: ${JSON.stringify({choices:[],usage:{prompt_tokens:17,completion_tokens:9,total_tokens:26}})}\n\ndata: [DONE]\n\n`);
    }catch(error){providerError=error;response.writeHead(500);response.end('provider fixture failed');}
  });
});
const mcp=createServer((request,response)=>{
  let source='';request.on('data',chunk=>source+=chunk);request.on('end',async()=>{
    try{
      assert.equal(request.method,'POST');const message=JSON.parse(source);assert.equal(message.jsonrpc,'2.0');
      if(!Object.hasOwn(message,'id')){assert.equal(message.method,'notifications/initialized');response.writeHead(202);response.end();return;}
      mcpMethods.push(message.method);let result;
      if(message.method==='server/discover'){
        assert.equal(message.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
        result={supportedVersions:['2026-07-28'],capabilities:{tools:{listChanged:true}},_meta:{'io.modelcontextprotocol/serverInfo':{name:'agentflow-http-subscription-fixture',version:'1'}}};
      }else if(message.method==='tools/list'){
        result={resultType:'complete',tools:[{name:'write_effect',description:'Write one approved fixture effect',inputSchema:{type:'object',properties:{body:{type:'string'}},required:['body'],additionalProperties:false}}]};
      }else if(message.method==='subscriptions/listen'){
        assert.equal(request.headers['mcp-method'],'subscriptions/listen');
        assert.deepEqual(message.params.notifications,{toolsListChanged:true});
        response.writeHead(200,{'Content-Type':'text/event-stream','Cache-Control':'no-cache'});
        const meta={'io.modelcontextprotocol/subscriptionId':message.id};
        const event=value=>`data: ${JSON.stringify(value)}\r\n\r\n`;
        response.write(event({jsonrpc:'2.0',method:'notifications/subscriptions/acknowledged',params:{_meta:meta,notifications:{toolsListChanged:true}}}));
        for(let index=0;index<70;index++)response.write(event({jsonrpc:'2.0',method:'notifications/tools/list_changed',params:{_meta:meta,untrustedPayload:'http-subscription-credential-'+index}}));
        response.on('close',()=>{if(!response.writableEnded){writeFile(join(folder,'http-listener-closed'),'peer observed HTTP listener disconnect').then(listenerClosedResolve).catch(error=>{mcpError=error;listenerClosedResolve();});}});
        return;
      }else if(message.method==='tools/call'){
        assert.equal(message.params.name,'write_effect');assert.deepEqual(message.params.arguments,{body:'http-approved'});
        await writeFile(effect,'http-approved');result={resultType:'complete',isError:false,content:[{type:'text',text:'effect applied'}],structuredContent:{bytes:13},_meta:{'io.modelcontextprotocol/effectAcknowledged':true,'io.modelcontextprotocol/effectIndependentlyVerified':false}};
      }else throw new Error(`Unexpected MCP request ${message.method}`);
      response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({jsonrpc:'2.0',id:message.id,result}));
    }catch(error){mcpError=error;response.writeHead(500);response.end('MCP fixture failed');}
  });
});
async function until(read,predicate,label){const deadline=Date.now()+15000;while(Date.now()<deadline){if(providerError)throw providerError;if(mcpError)throw mcpError;const value=await read();if(predicate(value))return value;await delay(20);}throw new Error(`Timed out waiting for ${label}; backend: ${backendErrors}`);}
async function api(path,body){const response=await fetch(`http://127.0.0.1:${backendPort}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:`Bearer ${token}`,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(5000)});assert.ok(response.ok,await response.clone().text());return response.json();}
function cli(...args){const child=spawnSync(cliExe,[String(backendPort),...args],{env,encoding:'utf8',windowsHide:true,timeout:10000});assert.equal(child.status,0,`${args.join(' ')} failed: ${child.stderr} ${child.stdout}`);assert.equal(child.stdout.includes('http-subscription-credential'),false);return JSON.parse(child.stdout);}
function admin(...args){const child=spawnSync(adminExe,['--db',db,'--modules',modules,'--stdlib',stdlib,...args],{env,encoding:'utf8',windowsHide:true,timeout:10000});assert.equal(child.status,0,child.stderr);return JSON.parse(child.stdout);}
async function stop(){if(backend&&backend.exitCode===null){const exited=new Promise(resolve=>backend.once('exit',resolve));backend.kill();await exited;}}
try{
  await mkdir(workspace);await new Promise(resolve=>provider.listen(0,'127.0.0.1',resolve));await new Promise(resolve=>mcp.listen(0,'127.0.0.1',resolve));
  const config=join(folder,'mcp.json'),mcpEndpoint=`http://127.0.0.1:${mcp.address().port}/mcp`;
  await writeFile(config,JSON.stringify({servers:[{id:'http-fixture',transport:'http',endpoint:mcpEndpoint}]}));
  assert.equal(admin('import-mcp',config).servers[0].revision,1);
  backend=spawn(serverExe,['--db',db,'--modules',modules,'--stdlib',stdlib,'--port','0','--model','synthetic-http-subscription-model','--model-stream-usage','supported','--model-endpoint',`http://127.0.0.1:${provider.address().port}/chat`,'--model-tools','supported','--workspace',workspace],{env,windowsHide:true});
  backend.stderr.on('data',data=>backendErrors+=data);let stdout='';
  backendPort=await new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(new Error(`Backend startup timed out: ${backendErrors}`)),15000);backend.once('error',reject);backend.once('exit',code=>reject(new Error(`Backend exited ${code}: ${backendErrors}`)));backend.stdout.on('data',data=>{stdout+=data;const found=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(stdout);if(found){clearTimeout(timeout);resolve(Number(found[1]));}});});
  const serverList=cli('mcp-servers');assert.deepEqual(serverList.servers,[{id:'http-fixture',revision:1,enabled:true,transport:'http'}]);
  await api('/v1/sessions',{id:'http-subscription',title:'HTTP MCP subscription lifecycle'});
  const run=cli('run','http-subscription','Use the configured HTTP MCP tool.');
  const [operation]=await until(async()=>api(`/v1/runs/${run.id}/operations`),items=>items.some(item=>item.state==='awaiting_approval'),'MCP approval request');
  assert.ok(operation.id,`Approval operation has no ID: ${JSON.stringify(operation)}`);
  const observed=await until(()=>api(`/v1/runs/${run.id}/events?after=0`),events=>events.some(event=>event.kind==='mcp.subscription.overflow'),'HTTP notification overflow guard');
  assert.ok(observed.some(event=>event.kind==='mcp.subscription.acknowledged'));
  assert.equal(observed.filter(event=>event.kind==='mcp.subscription.notification').length,64);
  assert.equal(observed.filter(event=>event.kind==='mcp.subscription.overflow').length,1);
  assert.equal(JSON.stringify(observed).includes('http-subscription-credential'),false,'HTTP peer notification payload must not be persisted');
  assert.ok(mcpMethods.includes('subscriptions/listen'));
  assert.equal(await readFile(effect,'utf8').catch(()=>''),'','HTTP MCP effect must wait for approval');
  cli('decide',operation.id,'allow');
  const completed=await until(()=>api(`/v1/runs/${run.id}`),value=>value.state==='completed','completed HTTP MCP AgentRunner turn');
  assert.equal(completed.state,'completed');assert.equal(await readFile(effect,'utf8'),'http-approved');
  await new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(new Error('HTTP MCP listener did not observe cancellation')),5000);listenerClosed.then(()=>{clearTimeout(timeout);resolve();},reject);});
  assert.equal(await readFile(join(folder,'http-listener-closed'),'utf8'),'peer observed HTTP listener disconnect');
  const finalEvents=await api(`/v1/runs/${run.id}/events?after=0`),closed=finalEvents.find(event=>event.kind==='mcp.subscription.closed');
  assert.ok(closed,'HTTP listener closure must be durable before AgentRunner terminal state');
  assert.equal(closed.data.outcome,'stopped');
  assert.equal(finalEvents.at(-1).kind,'run.completed');
  assert.equal(JSON.stringify(finalEvents).includes('http-subscription-credential'),false);
  const history=cli('history','http-subscription');assert.equal(history.length,4);assert.deepEqual(history[3].data.usage,{prompt_tokens:17,completion_tokens:9,total_tokens:26});
  assert.deepEqual(mcpMethods,['server/discover','tools/list','server/discover','subscriptions/listen','tools/call']);
  console.log('Native HTTP MCP AgentRunner subscription lifecycle passed: acknowledgement, bounded sanitized events, approval-gated tool effect, listener cancellation and durable pre-terminal closure. Provider response is synthetic; HTTP MCP peer exercises native transport.');
}finally{
  await stop();provider.closeAllConnections();mcp.closeAllConnections();await Promise.all([new Promise(resolve=>provider.close(resolve)),new Promise(resolve=>mcp.close(resolve))]);
  assert.equal(resolve(folder).startsWith(resolve(tmpdir())),true);await rm(folder,{recursive:true,force:true});
}
