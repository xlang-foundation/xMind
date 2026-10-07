// Synthetic inference, actual compiled server/admin/CLI, encrypted xlang3
// persistence and external MCP subprocess/file effects. No live-model claim.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {randomBytes} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
const [serverExe,cliExe,adminExe,modules,stdlib]=process.argv.slice(2);
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-mcp-')),workspace=join(folder,'workspace');
const db=join(folder,'state.sqlite'),effect=join(workspace,'effect.txt'),marker=join(folder,'marker');
const fixture=fileURLToPath(new URL('./mcp_effect_peer.mjs',import.meta.url));
const secret='synthetic-mcp-credential-fixture',token=randomBytes(32).toString('hex');
const env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;delete env.XMIND_SETUP_SECRET;
let child,port,errors='',peerError,requests=0;const continuations=new Map();
const peer=createServer((request,response)=>{
  let source='';request.on('data',chunk=>{source+=chunk;});request.on('end',()=>{
    try {
      requests++;const body=JSON.parse(source),prompt=body.messages.findLast(message=>message.role==='user').content;
      const names=body.tools.map(tool=>tool.function.name);assert.deepEqual(names.slice(0,3),['read_file','list_files','search_files']);
      assert.equal(names.length,4);assert.match(names[3],/^mcp_[a-f0-9]{48}$/);assert.equal(body.stream_options.include_usage,true);
      assert.equal(source.includes(secret),false);const tool=body.messages.findLast(message=>message.role==='tool');let delta,finish;
      if(tool){
        const outcome=JSON.parse(tool.content);continuations.set(prompt,outcome);
        if(prompt==='allowed'){assert.equal(outcome.acknowledged_by_peer,true);assert.equal(outcome.independently_verified,false);assert.equal(JSON.parse(outcome.response_json).result.structuredContent.bytes,7);}
        else assert.equal(outcome.error.code,'permission_denied');
        delta={content:'Synthetic model continuation after actual MCP outcome'};finish='stop';
      }else{delta={tool_calls:[{index:0,id:`fixture-${prompt}`,type:'function',function:{name:names[3],arguments:`{"body":${JSON.stringify(prompt)},"decimal":1.00000000000000000001}`}}]};finish='tool_calls';}
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.end(`data: ${JSON.stringify({choices:[{index:0,delta,finish_reason:null}]})}\n\ndata: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:finish}]})}\n\ndata: ${JSON.stringify({choices:[],usage:{prompt_tokens:13,completion_tokens:7,total_tokens:20}})}\n\ndata: [DONE]\n\n`);
    }catch(error){peerError=error;response.writeHead(500);response.end('synthetic fixture failed');}
  });
});
async function api(path,body){const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:`Bearer ${token}`,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(5000)});assert.ok(response.ok,await response.clone().text());return response.json();}
function cli(...args){const result=spawnSync(cliExe,[String(port),...args],{env,encoding:'utf8',windowsHide:true,timeout:5000});assert.equal(result.status,0,result.stderr);assert.equal(result.stdout.includes(secret),false);return JSON.parse(result.stdout);}
function admin(args,privateEnv=env){const result=spawnSync(adminExe,['--db',db,'--modules',modules,'--stdlib',stdlib,...args],{env:privateEnv,encoding:'utf8',windowsHide:true,timeout:10000});assert.equal(result.status,0,result.stderr);assert.equal((result.stdout+result.stderr).includes(secret),false);return JSON.parse(result.stdout);}
async function until(read,predicate){const deadline=Date.now()+10000;while(Date.now()<deadline){const value=await read();if(predicate(value))return value;if(peerError)throw peerError;await delay(10);}throw new Error(`State deadline: ${errors}`);}
async function start(model=true){
  const args=['--db',db,'--modules',modules,'--stdlib',stdlib,'--port','0'];
  if(model)args.push('--model','synthetic-mcp-protocol-model','--model-stream-usage','supported','--model-endpoint',`http://127.0.0.1:${peer.address().port}/chat`,'--model-tools','supported','--workspace',workspace);
  child=spawn(serverExe,args,{env,windowsHide:true});child.stderr.on('data',data=>{errors+=data;});
  port=await new Promise((resolve,reject)=>{let output='';const timer=setTimeout(()=>reject(new Error(`Readiness deadline: ${errors}`)),10000);child.on('error',error=>{clearTimeout(timer);reject(error);});child.once('exit',code=>{clearTimeout(timer);reject(new Error(`Server exited ${code}: ${errors}`));});child.stdout.on('data',data=>{output+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timer);resolve(Number(match[1]));}});});
}
async function stop(){if(child&&child.exitCode===null){const exited=new Promise(resolve=>child.once('exit',resolve));child.kill();await exited;}}
async function configure(mode){const path=join(folder,'config.json');await writeFile(path,JSON.stringify({servers:[{id:'fixture',transport:'stdio',executable:process.execPath,working_directory:folder,arguments:[fixture,mode,effect,marker,'credential-fixture'],credentials:[{name:'MCP_TEST_KEY',scope:'server',id:`credential-${mode}`}]}]}));return admin(['import-mcp',path]);}
try{
  await mkdir(workspace);await writeFile(effect,'');await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
  assert.equal((await configure('normal')).servers[0].revision,1);
  admin(['put-mcp-credential','fixture','MCP_TEST_KEY','XMIND_SETUP_SECRET'],{...env,XMIND_SETUP_SECRET:secret});
  await start();const metadata={servers:[{id:'fixture',revision:1,enabled:true,transport:'stdio'}],runtime_state:'per_run'};
  assert.deepEqual(cli('mcp-servers'),metadata);
  for(const name of ['allowed','denied','cancelled']){
    await api('/v1/sessions',{id:name,title:'Synthetic MCP protocol fixture'});const run=cli('run',name,name);
    const [operation]=await until(()=>api(`/v1/runs/${run.id}/operations`),items=>items.some(item=>item.state==='awaiting_approval'));
    assert.equal(await readFile(effect,'utf8'),name==='allowed'?'':'allowed');
    assert.ok(operation.arguments_json.includes('1.00000000000000000001'));
    if(name==='cancelled')cli('cancel',run.id);else cli('decide',operation.id,name==='denied'?'deny':'allow');
    await until(()=>api(`/v1/runs/${run.id}`),value=>value.state===(name==='cancelled'?'cancelled':'completed'));
    assert.equal((await api(`/v1/operations/${operation.id}`)).state,{allowed:'succeeded',denied:'denied',cancelled:'cancelled'}[name]);
    assert.equal(await readFile(effect,'utf8'),'allowed');
    const history=cli('history',name);assert.equal(JSON.stringify(history).includes(secret),false);
    if(name!=='cancelled'){assert.equal(history.length,4);assert.deepEqual(history[3].data.usage,{prompt_tokens:13,completion_tokens:7,total_tokens:20});}
  }
  const history=cli('history','allowed');await stop();await start();assert.deepEqual(cli('history','allowed'),history);assert.deepEqual(cli('mcp-servers'),metadata);assert.equal(await readFile(effect,'utf8'),'allowed');
  await stop();assert.equal((await configure('disconnect')).servers[0].revision,2);
  admin(['put-mcp-credential','fixture','MCP_TEST_KEY','XMIND_SETUP_SECRET'],{...env,XMIND_SETUP_SECRET:secret});await start();
  await api('/v1/sessions',{id:'uncertain',title:'Actual external effect followed by lost reply'});const run=cli('run','uncertain','uncertain');
  const [operation]=await until(()=>api(`/v1/runs/${run.id}/operations`),items=>items.some(item=>item.state==='awaiting_approval'));cli('decide',operation.id,'allow');
  await until(()=>api(`/v1/runs/${run.id}`),value=>value.state==='failed');assert.equal((await api(`/v1/operations/${operation.id}`)).state,'uncertain');assert.equal(await readFile(effect,'utf8'),'alloweduncertain');assert.equal(continuations.has('uncertain'),false);
  await stop();await start(false);assert.deepEqual(cli('mcp-servers'),{servers:[{id:'fixture',revision:2,enabled:true,transport:'stdio'}],runtime_state:'per_run'});assert.equal((await api(`/v1/operations/${operation.id}`)).state,'uncertain');assert.equal(await readFile(effect,'utf8'),'alloweduncertain');
  assert.equal(requests,6);if(peerError)throw peerError;
  console.log('Native MCP agent/server/admin/CLI contract passed: encrypted configured environment, exact arguments, actual approved file effect, denial/cancellation, result continuation and persisted uncertainty without replay. Inference is synthetic.');
}finally{await stop();peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));assert.equal(dirname(resolve(folder)),resolve(tmpdir()));assert.ok(folder.includes('xmind-agent-mcp-'));await rm(folder,{recursive:true,force:true});}
