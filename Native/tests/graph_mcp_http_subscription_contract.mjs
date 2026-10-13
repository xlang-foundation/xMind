// Native graph child, direct HTTP MCP effect, held-open subscription, SQLite journal.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {execFile,spawn} from 'node:child_process';
import {promisify} from 'node:util';
import {createServer} from 'node:http';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
import {randomBytes} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';

const {BackendClient}=createRequire(import.meta.url)('../../extensions/vscode/client.js');
const [serverExe,cliExe,adminExe,modules,stdlib]=process.argv.slice(2),execute=promisify(execFile);
for(const [name,value] of Object.entries({serverExe,cliExe,adminExe,modules,stdlib}))assert.ok(value,`${name} is required`);
const root=await mkdtemp(join(tmpdir(),'xmind-graph-http-subscription-')),workspace=join(root,'work'),database=join(root,'state.sqlite');
const config=join(root,'mcp.json'),graphs=join(root,'graphs.json'),effect=join(workspace,'effect.txt'),closedFile=join(root,'listener-closed');
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token},body='Graph HTTP effect after approval';
let child,client,port,backendErrors='',peerError;const methods=[];let listenerClosedResolve;
const listenerClosed=new Promise(resolve=>listenerClosedResolve=resolve);
const peer=createServer((request,response)=>{
  let source='';request.on('data',chunk=>source+=chunk);request.on('end',async()=>{
    try{
      assert.equal(request.method,'POST');assert.equal(request.url,'/mcp');const message=JSON.parse(source);assert.equal(message.jsonrpc,'2.0');
      if(!Object.hasOwn(message,'id')){assert.equal(message.method,'notifications/initialized');response.writeHead(202);response.end();return;}
      methods.push(message.method);let result;
      if(message.method==='server/discover'){
        assert.equal(message.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
        result={supportedVersions:['2026-07-28'],capabilities:{tools:{listChanged:true}},_meta:{'io.modelcontextprotocol/serverInfo':{name:'graph-http-subscription-peer',version:'1'}}};
      }else if(message.method==='tools/list'){
        result={resultType:'complete',tools:[{name:'write_effect',description:'Write one approved graph effect',inputSchema:{type:'object',properties:{body:{type:'string'}},required:['body'],additionalProperties:false}}]};
      }else if(message.method==='subscriptions/listen'){
        assert.equal(request.headers['mcp-method'],'subscriptions/listen');assert.deepEqual(message.params.notifications,{toolsListChanged:true});
        response.writeHead(200,{'Content-Type':'text/event-stream','Cache-Control':'no-cache'});
        const meta={'io.modelcontextprotocol/subscriptionId':message.id},event=value=>`data: ${JSON.stringify(value)}\r\n\r\n`;
        response.write(event({jsonrpc:'2.0',method:'notifications/subscriptions/acknowledged',params:{_meta:meta,notifications:{toolsListChanged:true}}}));
        for(let index=0;index<70;index++)response.write(event({jsonrpc:'2.0',method:'notifications/tools/list_changed',params:{_meta:meta,untrustedPayload:'graph-http-private-peer-payload',index}}));
        response.on('close',()=>{if(!response.writableEnded)writeFile(closedFile,'peer observed HTTP listener disconnect').then(listenerClosedResolve).catch(error=>{peerError=error;listenerClosedResolve();});});
        return;
      }else if(message.method==='tools/call'){
        assert.equal(message.params.name,'write_effect');assert.deepEqual(message.params.arguments,{body});
        assert.ok(source.includes(JSON.stringify(body)));
        await writeFile(effect,body);
        result={resultType:'complete',isError:false,content:[{type:'text',text:'effect applied'}],structuredContent:{bytes:Buffer.byteLength(body)},_meta:{'io.modelcontextprotocol/effectAcknowledged':true,'io.modelcontextprotocol/effectIndependentlyVerified':false}};
      }else throw new Error(`Unexpected MCP method ${message.method}`);
      response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({jsonrpc:'2.0',id:message.id,result}));
    }catch(error){peerError=error;response.writeHead(500);response.end('graph MCP HTTP fixture failed');}
  });
});
async function admin(...args){const output=await execute(adminExe,['--db',database,'--modules',modules,'--stdlib',stdlib,...args],{env,windowsHide:true,timeout:20000});return JSON.parse(output.stdout);}
async function cli(...args){const output=await execute(cliExe,[String(port),...args],{env,windowsHide:true,timeout:10000});return JSON.parse(output.stdout);}
async function api(path,body){const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:`Bearer ${token}`,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(5000)});assert.ok(response.ok,await response.clone().text());return response.json();}
async function wait(read,predicate,label){const deadline=Date.now()+15000;while(Date.now()<deadline){if(peerError)throw peerError;const value=await read();if(predicate(value))return value;await delay(15);}throw new Error(`Timed out waiting for ${label}: ${backendErrors}`);}
async function state(id,wanted){return wait(()=>client.graph(id),value=>value.run.state===wanted,`graph ${wanted}`);}
async function stop(){if(child&&child.exitCode===null){const done=new Promise(resolve=>child.once('exit',resolve));child.kill();await done;}child=undefined;}
try{
  await mkdir(workspace);await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
  const setting={id:'graph-http',transport:'http',enabled:true,endpoint:`http://127.0.0.1:${peer.address().port}/mcp`};
  await writeFile(config,JSON.stringify({servers:[setting]}));assert.equal((await admin('import-mcp',config)).servers[0].revision,1);
  const catalogue=await admin('discover-mcp',setting.id,workspace);assert.equal(catalogue.tool_dispatch_performed,false);assert.equal(catalogue.tools.length,1);
  const tool=catalogue.tools[0],spec={nodes:[{id:'external',type:'tool',tool:tool.alias,mcp:{server_id:setting.id,config_revision:1},arguments_json:JSON.stringify({body})}]};
  await writeFile(graphs,JSON.stringify({graphs:[{id:'mcp.http',spec}]}));assert.equal((await admin('import-graphs',graphs)).graphs[0].revision,1);
  child=spawn(serverExe,['--db',database,'--modules',modules,'--stdlib',stdlib,'--port','0','--workspace',workspace,'--workers','1'],{env,windowsHide:true});child.stderr.on('data',data=>backendErrors+=data);let output='';
  port=await new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(new Error(`Backend readiness timed out: ${backendErrors}`)),12000);child.once('error',reject);child.once('exit',code=>reject(new Error(`Backend exited ${code}: ${backendErrors}`)));child.stdout.on('data',data=>{output+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timeout);resolve(Number(match[1]));}});});
  client=new BackendClient(`http://127.0.0.1:${port}`,()=>token);assert.ok((await client.graphs()).graphs.find(value=>value.id==='mcp.http').executable);
  const session=await client.createSession('Native graph HTTP MCP subscription');const run=await client.graphRun(session.id,'mcp.http',1,'Execute the configured HTTP graph tool.');
  const pending=await wait(async()=>{const graph=await client.graph(run.id);for(const item of await client.graphChildren(run.id)){const operation=(await client.operations(item.id)).find(value=>value.state==='awaiting_approval');if(operation)return {child:item,operation};if(item.state==='failed'||graph.run.state==='failed')return {child:item,failed:true};}},Boolean,'catalogue invalidation or approval');
  if(pending.operation)await cli('decide',pending.operation.id,'allow');
  const failed=await state(run.id,'failed');assert.equal(failed.run.state,'failed');
  const proposal={child:pending.child};
  const events=await wait(()=>client.graphEvents(run.id,0),items=>items.some(value=>value.run_id===proposal.child.id&&value.kind==='mcp.catalogue.invalidated'),'graph catalogue invalidation');
  const journal=events.filter(value=>value.run_id===proposal.child.id&&value.kind.startsWith('mcp.subscription.'));
  assert.ok(journal.some(value=>value.kind==='mcp.subscription.acknowledged'));
  assert.ok(journal.filter(value=>value.kind==='mcp.subscription.notification').length>=1);
  assert.ok(journal.filter(value=>value.kind==='mcp.subscription.notification').length<=64);
  assert.ok(journal.filter(value=>value.kind==='mcp.subscription.overflow').length<=1);
  assert.equal(JSON.stringify(journal).includes('graph-http-private-peer-payload'),false);
  await assert.rejects(readFile(effect),error=>error.code==='ENOENT');
  await new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(new Error('Graph HTTP listener did not close')),5000);listenerClosed.then(()=>{clearTimeout(timeout);resolve();},reject);});
  assert.equal(await readFile(closedFile,'utf8'),'peer observed HTTP listener disconnect');
  const terminalEvents=await client.graphEvents(run.id,0),closed=terminalEvents.find(value=>value.run_id===proposal.child.id&&value.kind==='mcp.subscription.closed'),failedEvent=terminalEvents.find(value=>value.run_id===proposal.child.id&&value.kind==='run.failed');
  assert.ok(closed);assert.equal(closed.data.outcome,'stopped');assert.ok(failedEvent);assert.ok(closed.seq<failedEvent.seq);
  const invalidated=terminalEvents.find(value=>value.run_id===proposal.child.id&&value.kind==='mcp.catalogue.invalidated');assert.ok(invalidated);assert.equal(invalidated.data.effect_dispatched,false);
  assert.ok(methods.includes('subscriptions/listen'));assert.equal(methods.includes('tools/call'),false,'A pinned graph MCP alias must fail closed when its catalogue changes');
  if(pending.operation)assert.equal((await client.operation(pending.operation.id)).state,'failed');
  console.log('Native graph HTTP MCP subscription contract passed: an in-run list-change invalidates the pinned alias, retires any approved-but-undispatched operation, and closes the listener before the graph child fails. No peer tool effect is sent.');
}finally{
  await stop();peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));
  assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-graph-http-subscription-'));await rm(root,{recursive:true,force:true});
}
